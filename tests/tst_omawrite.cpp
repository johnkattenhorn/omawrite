#include <QtTest>
#include <QClipboard>
#include <QColor>
#include <QSaveFile>
#include <QStandardPaths>
#include <QQuickTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextDocument>
#include "buffersession.h"
#include "workspacesession.h"
#include "remote.h"
#include "windowmanager.h"
#include <QTextLayout>
#include <QQuickItem>
#include <QFont>
#include <QQmlProperty>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QQuickWindow>

#include "cli.h"
#include "agentsession.h"
#include "backend.h"
#include "markdownhighlighter.h"

class OmawriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    // Every test in the run shares one AppDataLocation, so without this each one
    // inherits every tab the tests before it opened -- and a Backend that was
    // meant to start untitled comes up holding someone else's document.
    void init() {
        const QString appData =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QFile::remove(QDir(appData).filePath(QStringLiteral("session.json")));
        QSettings().clear();
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void printsInPointsNotPixels() {
        QFont editorFont;
        editorFont.setPixelSize(20);
        const QFont printed = Backend::printFont(editorFont, 96.0);
        QCOMPARE(printed.pixelSize(), -1);
        QCOMPARE(printed.pointSizeF(), 15.0);
        QCOMPARE(Backend::printFont(editorFont, 0.0).pointSizeF(), 15.0);

        QFont pointFont;
        pointFont.setPointSizeF(11.0);
        QCOMPARE(Backend::printFont(pointFont, 144.0).pointSizeF(), 11.0);
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
        QCOMPARE(markup.at(2).target, QStringLiteral("https://example.com"));
    }

    void findsWikilinks() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("See [[other-note|the class]] and [[John 3:16]]."));
        QCOMPARE(markup.size(), 2);
        QCOMPARE(markup.at(0).kind, MarkdownHighlighter::InlineKind::WikiLink);
        QCOMPARE(markup.at(0).target, QStringLiteral("other-note"));
        QCOMPARE(markup.at(0).content.length, QStringLiteral("the class").size());
        QCOMPARE(markup.at(1).target, QStringLiteral("John 3:16"));
    }

    void ignoresWikilinksInCode() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("Use `[[not-a-link]]` in code"));
        QCOMPARE(markup.size(), 0);
    }

    void clickableSpansCoverWholeMarkup() {
        const QString text = QStringLiteral(
            "See [[Note|alias]] and [site](https://example.com) "
            "plus https://bare.example and <https://auto.example>");
        const auto spans = MarkdownHighlighter::clickableSpans(text);
        QCOMPARE(spans.size(), 4);

        const QString wiki = QStringLiteral("[[Note|alias]]");
        QCOMPARE(spans.at(0).span.start, text.indexOf(wiki));
        QCOMPARE(spans.at(0).span.length, wiki.size());
        QCOMPARE(spans.at(0).target, QStringLiteral("Note"));

        const QString md = QStringLiteral("[site](https://example.com)");
        QCOMPARE(spans.at(1).span.start, text.indexOf(md));
        QCOMPARE(spans.at(1).span.length, md.size());
        QCOMPARE(spans.at(1).target, QStringLiteral("https://example.com"));

        const QString autolink = QStringLiteral("<https://auto.example>");
        QCOMPARE(spans.at(2).span.start, text.indexOf(autolink));
        QCOMPARE(spans.at(2).span.length, autolink.size());
        QCOMPARE(spans.at(2).target, QStringLiteral("https://auto.example"));

        const QString bare = QStringLiteral("https://bare.example");
        QCOMPARE(spans.at(3).span.start, text.indexOf(bare));
        QCOMPARE(spans.at(3).span.length, bare.size());
        QCOMPARE(spans.at(3).target, bare);
    }

    void resolvesWikilinksNextToTheFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString current = dir.filePath(QStringLiteral("current.md"));
        const QString target = dir.filePath(QStringLiteral("other-note.md"));
        QVERIFY(QFile(current).open(QIODevice::WriteOnly));
        QFile out(target);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Text));
        out.write("# Other\n");
        out.close();
        QCOMPARE(Backend::resolveWikilinkPath(current, QStringLiteral("other-note")),
                 QFileInfo(target).canonicalFilePath());
        QVERIFY(Backend::resolveWikilinkPath(current, QStringLiteral("missing-note")).isEmpty());
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();

        // Someone else's write is noticed, and with no local changes to weigh it
        // against it is taken rather than asked about. What lands in the editor
        // is covered by takesAnOutsideEditWhenNothingLocalIsAtStake.
        QTest::qWait(200);
        QCOMPARE(externalChangeSpy.count(), 0);
    }

    void listsOnlyDocumentsAndFolders() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QVERIFY(QDir(folder.path()).mkdir(QStringLiteral("archive")));
        for (const QString &name : {QStringLiteral("second.md"),
                                    QStringLiteral("first.markdown"),
                                    QStringLiteral("notes.txt"),
                                    QStringLiteral(".hidden.md")}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));
        QCOMPARE(backend.folderName(), QDir(folder.path()).dirName());
        QVERIFY(backend.folderHasParent());

        // Folders first, then the documents Omawrite can open, by name.
        // Plain text and dotfiles are not writing in this app's sense.
        const QVariantList entries = backend.folderEntries();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries.at(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("archive"));
        QVERIFY(entries.at(0).toMap().value(QStringLiteral("isDir")).toBool());
        QCOMPARE(entries.at(1).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("first.markdown"));
        QVERIFY(!entries.at(1).toMap().value(QStringLiteral("isDir")).toBool());
        QCOMPARE(entries.at(2).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("second.md"));
        QCOMPARE(entries.at(2).toMap().value(QStringLiteral("url")).toUrl(),
                 QUrl::fromLocalFile(folder.filePath(QStringLiteral("second.md"))));

        // An empty folder still lists, so it can be walked out of again.
        backend.setFolder(entries.at(0).toMap().value(QStringLiteral("url")).toUrl());
        QVERIFY(backend.folderEntries().isEmpty());
        backend.openParentFolder();
        QCOMPARE(backend.folderUrl(), QUrl::fromLocalFile(folder.path()));
    }

    void browsesTheOpenDocumentsFolder() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("draft.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        Backend backend;
        QSignalSpy folderSpy(&backend, &Backend::folderChanged);
        backend.open(QUrl::fromLocalFile(path));
        QCOMPARE(backend.folderUrl(), QUrl::fromLocalFile(folder.path()));
        QCOMPARE(folderSpy.count(), 1);
    }

    void noticesDocumentsWrittenElsewhere() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));
        QVERIFY(backend.folderEntries().isEmpty());

        QSignalSpy folderSpy(&backend, &Backend::folderChanged);
        QFile file(folder.filePath(QStringLiteral("written-elsewhere.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        QTRY_VERIFY(folderSpy.count() > 0);
        QCOMPARE(backend.folderEntries().size(), 1);
    }


    void createsDocumentsAndFoldersWhereItBrowses() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        // A typed name is cleaned the same way a saved document's is, and
        // gains the extension when it is missing.
        const QUrl created = backend.createDocument(QStringLiteral("Field notes"));
        QCOMPARE(created, QUrl::fromLocalFile(folder.filePath(QStringLiteral("Field notes.md"))));
        QVERIFY(QFileInfo::exists(created.toLocalFile()));
        QCOMPARE(Backend::sanitizedEntryName(QStringLiteral("a/b")), QStringLiteral("a-b"));

        // An existing name is reported, never overwritten.
        QVERIFY(backend.createDocument(QStringLiteral("Field notes.md")).isEmpty());
        QCOMPARE(backend.status(), QStringLiteral("Field notes.md already exists."));

        backend.createFolder(QStringLiteral("archive"));
        QVERIFY(QFileInfo(folder.filePath(QStringLiteral("archive"))).isDir());

        const QVariantList entries = backend.folderEntries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("archive"));
        QCOMPARE(entries.at(1).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Field notes.md"));
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> editor(createJsHarness(component, R"QML(
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
        )QML", QStringLiteral("MutationHarness")));
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void nestsListItemsWithTab() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> editor(createListEditor(component));
        QVERIFY2(editor, qPrintable(component.errorString()));

        const auto indent = [&](const QString &source, int from, int to, int direction) {
            QVariant handled;
            QMetaObject::invokeMethod(editor.data(), "indent", Q_RETURN_ARG(QVariant, handled),
                                      Q_ARG(QVariant, source), Q_ARG(QVariant, from),
                                      Q_ARG(QVariant, to), Q_ARG(QVariant, direction));
            return handled.toBool();
        };
        const auto result = [&] { return editor->property("resultText").toString(); };
        const auto caret = [&] { return editor->property("resultCursor").toInt(); };

        // A bullet nests under the item above it, and the caret rides along.
        QVERIFY(indent(QStringLiteral("- one\n- two"), 11, 11, 1));
        QCOMPARE(result(), QStringLiteral("- one\n  - two"));
        QCOMPARE(caret(), 13);

        // Numbers hang off the wider `1. ` marker and renumber around the move.
        QVERIFY(indent(QStringLiteral("1. one\n2. two\n3. three"), 12, 12, 1));
        QCOMPARE(result(), QStringLiteral("1. one\n   1. two\n2. three"));

        // Shift+Tab puts it back beside the item it hung under.
        QVERIFY(indent(QStringLiteral("1. one\n   1. two\n2. three"), 15, 15, -1));
        QCOMPARE(result(), QStringLiteral("1. one\n2. two\n3. three"));

        // Nesting goes as deep as the list does.
        QVERIFY(indent(QStringLiteral("- one\n  - two\n  - three\n- four"), 22, 22, 1));
        QCOMPARE(result(), QStringLiteral("- one\n  - two\n    - three\n- four"));

        // Children follow the item they hang under.
        QVERIFY(indent(QStringLiteral("- one\n  - two\n    - deep\n- four"), 12, 12, -1));
        QCOMPARE(result(), QStringLiteral("- one\n- two\n  - deep\n- four"));

        // A selection nests every item it touches, keeping their relative depth.
        QVERIFY(indent(QStringLiteral("- one\n- two\n- three"), 8, 18, 1));
        QCOMPARE(result(), QStringLiteral("- one\n  - two\n  - three"));

        // A list that deliberately starts at 3 keeps its numbering.
        QVERIFY(indent(QStringLiteral("3. a\n4. b\n5. c"), 8, 8, 1));
        QCOMPARE(result(), QStringLiteral("3. a\n   1. b\n4. c"));

        // Nothing to nest under, nothing to lift out of, and no lists inside a
        // fence: Tab is left to whatever it did before.
        QVERIFY(!indent(QStringLiteral("- one"), 5, 5, 1));
        QVERIFY(!indent(QStringLiteral("- one\n- two"), 11, 11, -1));
        QVERIFY(!indent(QStringLiteral("just text"), 4, 4, 1));
        QVERIFY(!indent(QStringLiteral("```\n- one\n- two\n"), 15, 15, 1));
    }

    void continuesListsAcrossReturn() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> editor(createListEditor(component));
        QVERIFY2(editor, qPrintable(component.errorString()));

        const auto pressReturn = [&](const QString &source, int cursor) {
            QVariant handled;
            QMetaObject::invokeMethod(editor.data(), "pressReturn",
                                      Q_RETURN_ARG(QVariant, handled),
                                      Q_ARG(QVariant, source), Q_ARG(QVariant, cursor));
            return handled.toBool();
        };
        const auto result = [&] { return editor->property("resultText").toString(); };
        const auto caret = [&] { return editor->property("resultCursor").toInt(); };

        // A new item keeps the depth of the one it follows.
        QVERIFY(pressReturn(QStringLiteral("- one\n  - two"), 13));
        QCOMPARE(result(), QStringLiteral("- one\n  - two\n  - "));
        QCOMPARE(caret(), 18);

        // Inserting into an ordered list renumbers what follows.
        QVERIFY(pressReturn(QStringLiteral("1. one\n2. two\n3. three"), 6));
        QCOMPARE(result(), QStringLiteral("1. one\n2. \n3. two\n4. three"));
        QCOMPARE(caret(), 10);

        // Return on an empty nested item lifts it a level instead of ending the
        // list; the outermost level is left to drop out of the list entirely.
        QVERIFY(pressReturn(QStringLiteral("- one\n  - "), 10));
        QCOMPARE(result(), QStringLiteral("- one\n- "));
        QCOMPARE(caret(), 8);
        QVERIFY(pressReturn(QStringLiteral("- one\n- "), 8));
        QCOMPARE(result(), QStringLiteral("- one\n\n"));

        // Blockquotes carry their marker over too, but never renumber.
        QVERIFY(pressReturn(QStringLiteral("> quoted"), 8));
        QCOMPARE(result(), QStringLiteral("> quoted\n> "));

        // A task item carries its box onto the next line, always unticked.
        QVERIFY(pressReturn(QStringLiteral("- [x] done"), 10));
        QCOMPARE(result(), QStringLiteral("- [x] done\n- [ ] "));
        QCOMPARE(caret(), 17);

        // An item holding nothing but a box counts as empty and ends the list.
        QVERIFY(pressReturn(QStringLiteral("- [ ] "), 6));
        QCOMPARE(result(), QStringLiteral("\n"));

        // Ordinary prose has no marker to carry, so Return is left alone.
        QVERIFY(!pressReturn(QStringLiteral("just text"), 9));
    }

    void wrapsProseToTheMeasure() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> editor(createListEditor(component));
        QVERIFY2(editor, qPrintable(component.errorString()));

        const auto wrap = [&](const QString &source, int from, int to, int columns) {
            QVariant handled;
            QMetaObject::invokeMethod(editor.data(), "wrap",
                                      Q_RETURN_ARG(QVariant, handled),
                                      Q_ARG(QVariant, source), Q_ARG(QVariant, from),
                                      Q_ARG(QVariant, to), Q_ARG(QVariant, columns));
            return handled.toBool();
        };
        const auto result = [&] { return editor->property("resultText").toString(); };
        const auto longest = [&]() {
            int width = 0;
            const QStringList lines = editor->property("resultText").toString().split(QLatin1Char('\n'));
            for (const QString &line : lines)
                width = qMax(width, int(line.size()));
            return width;
        };

        // A paragraph on one long line comes back on the measure, broken
        // between words.
        QVERIFY(wrap(QStringLiteral("one two three four five six seven eight nine ten"), 0, 0, 20));
        QCOMPARE(result(), QStringLiteral("one two three four\nfive six seven eight\nnine ten"));
        QVERIFY(longest() <= 20);

        // A document wrapped somewhere else is reflowed rather than folded:
        // the lines are joined first, so a file wrapped at 72 comes out at
        // the measure asked for rather than at 72 with the overhang tucked
        // underneath it.
        QVERIFY(wrap(QStringLiteral("one two three\nfour five six seven\neight nine ten"), 0, 0, 20));
        QCOMPARE(result(), QStringLiteral("one two three four\nfive six seven eight\nnine ten"));

        // A list item hangs its continuation under its own text, and a quote
        // carries its marker down.
        QVERIFY(wrap(QStringLiteral("- alpha beta gamma delta epsilon"), 0, 0, 20));
        QCOMPARE(result(), QStringLiteral("- alpha beta gamma\n  delta epsilon"));
        QVERIFY(wrap(QStringLiteral("> alpha beta gamma delta epsilon"), 0, 0, 20));
        QCOMPARE(result(), QStringLiteral("> alpha beta gamma\n> delta epsilon"));

        // Structure keeps its own line breaks however long the line is: a
        // heading split in two stops being a heading.
        QVERIFY(!wrap(QStringLiteral("# A heading that runs well past the measure it is given"),
                      0, 0, 20));
        QVERIFY(!wrap(QStringLiteral("| a column | another column | a third column |"), 0, 0, 20));
        QVERIFY(!wrap(QStringLiteral("```\nsome code that is far too wide for the measure\n```"),
                      0, 0, 20));
        QVERIFY(!wrap(QStringLiteral("---\ntitle: a front matter field that is much too long\n---\n"),
                      0, 0, 20));

        // A word longer than the measure goes on a line of its own rather
        // than being cut in half: a broken URL is not a link any more.
        QVERIFY(wrap(QStringLiteral("see https://example.com/a/very/long/path/indeed now"), 0, 0, 20));
        QCOMPARE(result(), QStringLiteral("see\nhttps://example.com/a/very/long/path/indeed\nnow"));

        // Markdown's two-space hard break belongs to the end of the line it
        // was written on, and stays there.
        QVERIFY(wrap(QStringLiteral("alpha beta gamma delta epsilon  "), 0, 0, 20));
        QCOMPARE(result(), QStringLiteral("alpha beta gamma\ndelta epsilon  "));

        // Nothing to do is said rather than done.
        QVERIFY(!wrap(QStringLiteral("short enough\n\nso is this"), 0, 0, 20));

        // A selection takes the selected lines and leaves the rest alone.
        const QString mixed = QStringLiteral("keep this line as it is\n\none two three four five six");
        QVERIFY(wrap(mixed, mixed.indexOf(QStringLiteral("one two")), mixed.size(), 10));
        QCOMPARE(result(), QStringLiteral("keep this line as it is\n\none two\nthree four\nfive six"));
    }

    void unwrapsHardWrappedLines() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> editor(createListEditor(component));
        QVERIFY2(editor, qPrintable(component.errorString()));

        const auto unwrap = [&](const QString &source, int from, int to) {
            QVariant handled;
            QMetaObject::invokeMethod(editor.data(), "unwrap",
                                      Q_RETURN_ARG(QVariant, handled),
                                      Q_ARG(QVariant, source), Q_ARG(QVariant, from),
                                      Q_ARG(QVariant, to));
            return handled.toBool();
        };
        const auto result = [&] { return editor->property("resultText").toString(); };
        const auto caret = [&] { return editor->property("resultCursor").toInt(); };

        // With no selection the whole document is unwrapped, and the blank
        // line between paragraphs is what tells them apart.
        QVERIFY(unwrap(QStringLiteral("This is a line\nthat was wrapped\nat 72 columns.\n\nA second\nparagraph."),
                       0, 0));
        QCOMPARE(result(), QStringLiteral("This is a line that was wrapped at 72 columns.\n\nA second paragraph."));

        // A line already on its own is left where it is, plan and all.
        QVERIFY(!unwrap(QStringLiteral("One line.\n\nAnother line."), 0, 0));

        // Each list item keeps its line; the text wrapped under one comes up
        // to join it.
        QVERIFY(unwrap(QStringLiteral("- item one that\n  wrapped over\n- item two\n1. ordered one\n   wrapped too"), 0, 0));
        QCOMPARE(result(), QStringLiteral("- item one that wrapped over\n- item two\n1. ordered one wrapped too"));

        // Headings, the underline that makes one, thematic breaks and table
        // rows are structure rather than wrapped prose.
        QVERIFY(unwrap(QStringLiteral("# Heading\ntext under it\nwrapped here\n\nTitle\n=====\n\n| a | b |\n|---|---|\n| 1 | 2 |"), 0, 0));
        QCOMPARE(result(), QStringLiteral("# Heading\ntext under it wrapped here\n\nTitle\n=====\n\n| a | b |\n|---|---|\n| 1 | 2 |"));

        // Code keeps every line it was given, fenced or indented.
        QVERIFY(!unwrap(QStringLiteral("```\nfirst line\nsecond line\n```\n\n    indented one\n    indented two"), 0, 0));

        // Front matter is not Markdown, so its fields stay on their own lines.
        QVERIFY(unwrap(QStringLiteral("---\ntitle: Thing\ntags: [a, b]\n---\n\nbody one\nbody two"), 0, 0));
        QCOMPARE(result(), QStringLiteral("---\ntitle: Thing\ntags: [a, b]\n---\n\nbody one body two"));

        // Two trailing spaces are Markdown's own line break: the break the
        // writer asked for survives.
        QVERIFY(unwrap(QStringLiteral("a deliberate break  \nand then some\nwrapped text"), 0, 0));
        QCOMPARE(result(), QStringLiteral("a deliberate break  \nand then some wrapped text"));

        // A quote's wrapped lines join under one marker; a bare marker is the
        // blank line of the quote and stays.
        QVERIFY(unwrap(QStringLiteral("> quoted one\n> quoted two\n>\n> second part\n> wrapped"), 0, 0));
        QCOMPARE(result(), QStringLiteral("> quoted one quoted two\n>\n> second part wrapped"));

        // A selection unwraps the lines it touches and nothing else.
        QVERIFY(unwrap(QStringLiteral("a one\ntwo three\n\nb one\nb two"), 0, 8));
        QCOMPARE(result(), QStringLiteral("a one two three\n\nb one\nb two"));

        // The caret comes out of the rewrite on the word it went in on: the
        // "d" of "delta", which the join moved from 17 in the source to 17 in
        // one line of prose.
        QVERIFY(unwrap(QStringLiteral("alpha beta\ngamma delta"), 17, 17));
        QCOMPARE(result(), QStringLiteral("alpha beta gamma delta"));
        QCOMPARE(caret(), 17);
    }

    void unwrapsFromTheEditorKeys() {
        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> created(createMainWindow(engine, component, backend));
        QVERIFY2(created, qPrintable(component.errorString()));

        QQuickWindow *window = qobject_cast<QQuickWindow *>(created.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        // Typed in rather than assigned, so undo has the wrapped text to go
        // back to the way it does after a paste.
        QVERIFY(QMetaObject::invokeMethod(editor, "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("one line\nwrapped over\n\nanother\nwrapped"))));
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        QTest::keyClick(window, Qt::Key_J, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(editor->property("text").toString(),
                 QStringLiteral("one line wrapped over\n\nanother wrapped"));

        // The footer says what happened, and one undo puts the lines back.
        QObject *notice = window->findChild<QObject *>(QStringLiteral("footerNotice"));
        QVERIFY(notice);
        QCOMPARE(notice->property("text").toString(), QStringLiteral("Unwrapped 2 lines"));

        QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
        QCOMPARE(editor->property("text").toString(),
                 QStringLiteral("one line\nwrapped over\n\nanother\nwrapped"));
    }

    void copiesTheSelectionWhenTheDragEnds() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        clipboard->clear();

        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> created(createMainWindow(engine, component, backend));
        QVERIFY2(created, qPrintable(component.errorString()));

        QQuickWindow *window = qobject_cast<QQuickWindow *>(created.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QQuickItem *editor = window->findChild<QQuickItem *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        editor->setProperty("text", QStringLiteral("hello there"));
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        const QPointF start = editor->mapToScene(QPointF(1, editor->height() > 8 ? 8 : 1));
        const QPointF end = editor->mapToScene(QPointF(60, editor->height() > 8 ? 8 : 1));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start.toPoint());
        QTest::mouseMove(window, end.toPoint());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end.toPoint());

        const QString selected = editor->property("selectedText").toString();
        QVERIFY(!selected.isEmpty());
        QTRY_COMPARE(clipboard->text(), selected);

        QObject *notice = window->findChild<QObject *>(QStringLiteral("footerNotice"));
        QVERIFY(notice);
        QCOMPARE(notice->property("text").toString(),
                 QStringLiteral("Copied %1 characters").arg(selected.length()));

        clipboard->clear();
    }

    void leavesTheClipboardAloneWhenCopyOnSelectIsOff() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        clipboard->setText(QStringLiteral("something else"));

        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> created(createMainWindow(engine, component, backend));
        QVERIFY2(created, qPrintable(component.errorString()));

        QQuickWindow *window = qobject_cast<QQuickWindow *>(created.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *copier = window->findChild<QObject *>(QStringLiteral("selectionCopier"));
        QVERIFY(editor);
        QVERIFY(copier);
        editor->setProperty("text", QStringLiteral("hello there"));
        QVERIFY(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 5)));

        // Ctrl+Shift+C turns it off, the handler goes with it, and a release
        // with a selection leaves the clipboard where it was.
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(copier->property("enabled").toBool(), false);
        QVERIFY(QMetaObject::invokeMethod(window, "copySelectionOnRelease",
                                          Q_ARG(QVariant, QVariant::fromValue(editor))));
        QCOMPARE(clipboard->text(), QStringLiteral("something else"));

        QObject *notice = window->findChild<QObject *>(QStringLiteral("footerNotice"));
        QVERIFY(notice);
        QCOMPARE(notice->property("text").toString(), QStringLiteral("Copy on select off"));

        // And back on, which copies what is already selected on the next release.
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(copier->property("enabled").toBool(), true);
        QVERIFY(QMetaObject::invokeMethod(window, "copySelectionOnRelease",
                                          Q_ARG(QVariant, QVariant::fromValue(editor))));
        QCOMPARE(clipboard->text(), QStringLiteral("hello"));

        clipboard->clear();
    }

    void indentsListsFromTheEditorKeys() {
        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> created(createMainWindow(engine, component, backend));
        QVERIFY2(created, qPrintable(component.errorString()));

        QQuickWindow *window = qobject_cast<QQuickWindow *>(created.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        editor->setProperty("text", QStringLiteral("- one\n- two"));
        editor->setProperty("cursorPosition", 11);
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));

        QTest::keyClick(window, Qt::Key_Tab);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("- one\n  - two"));

        QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("- one\n- two"));
    }

    void savesAndOpensFromFooterButtons() {
        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> window(createMainWindow(engine, component, backend));
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void togglesMarkdownPreviewWithoutChangingSource() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *preview = window->findChild<QObject *>(QStringLiteral("renderedPreview"));
        QObject *toggle = window->findChild<QObject *>(QStringLiteral("modeToggle"));
        QVERIFY(editor);
        QVERIFY(preview);
        QVERIFY(toggle);
        QVERIFY(!preview->property("visible").toBool());

        editor->setProperty("text", QStringLiteral("# Hello\n\n**world**"));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(preview->property("visible").toBool());
        QTRY_VERIFY(preview->property("text").toString().contains(QStringLiteral("Hello")));
        QVERIFY(!preview->property("text").toString().contains(QLatin1Char('#')));
        QVERIFY(!editor->property("visible").toBool());

        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(editor->property("visible").toBool());
        QCOMPARE(editor->property("text").toString(), QStringLiteral("# Hello\n\n**world**"));
    }

    void refreshesThePreviewWhenAnotherTabComesForward() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir previewTabState;
        QVERIFY(previewTabState.isValid());
        Backend backend(previewTabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *preview = window->findChild<QObject *>(QStringLiteral("renderedPreview"));
        QVERIFY(editor);
        QVERIFY(preview);

        const QString first = backend.activeBufferId();
        editor->setProperty("text", QStringLiteral("# First document"));
        const QString second = backend.newBuffer();
        QVERIFY(!second.isEmpty());
        editor->setProperty("text", QStringLiteral("# Second document"));

        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));
        QVERIFY(preview->property("visible").toBool());
        QTRY_VERIFY(preview->property("text").toString()
                        .contains(QStringLiteral("Second document")));

        // The tab that comes forward is the one the reader is now looking at.
        // The preview used to keep the document that had just left on screen,
        // because the load happens under the restore flag the editor's text
        // handler returns early on.
        QVERIFY(backend.selectBuffer(first));
        QTRY_VERIFY(preview->property("text").toString()
                        .contains(QStringLiteral("First document")));
        QVERIFY(!preview->property("text").toString()
                     .contains(QStringLiteral("Second document")));
    }

    void scalesTextWithDesktopTextSize() {
        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> window(createMainWindow(engine, component, backend));
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        // The document opens at the size the panel beside it is drawn at.
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 12);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 16);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 16);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 9);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 9);
    }

    void keepsTextColumnInsideNarrowWindows() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QObject *viewport = editor->parent();
        QVERIFY(viewport);

        // A resize reaches the window through its platform window, so the
        // bindings only see the new width once the events are delivered.
        const int minimumWidth = window->property("minimumWidth").toInt();
        window->setProperty("width", minimumWidth);
        QCoreApplication::processEvents();
        QCOMPARE(window->property("width").toInt(), minimumWidth);
        QVERIFY(editor->property("x").toInt() > 0);

        // A tiling compositor resizes below the minimum the window asks for.
        window->setProperty("width", 394);
        QCoreApplication::processEvents();
        QCOMPARE(window->property("width").toInt(), 394);
        QVERIFY(editor->property("width").toInt()
                <= viewport->property("width").toInt());
        QVERIFY(editor->property("x").toInt() >= 0);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

    void togglesTheFileSidebar() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QObject *filesButton = window->findChild<QObject *>(QStringLiteral("filesButton"));
        QVERIFY(sidebar);
        QVERIFY(filesButton);

        // Closed on launch: the panel takes no width from the writing area.
        QVERIFY(!window->property("sidebarOpen").toBool());
        QCOMPARE(sidebar->property("width").toReal(), 0.0);

        QVERIFY(QMetaObject::invokeMethod(filesButton, "clicked"));
        QVERIFY(window->property("sidebarOpen").toBool());
        QVERIFY(sidebar->property("width").toReal() > 0.0);

        QVERIFY(QMetaObject::invokeMethod(filesButton, "clicked"));
        QCOMPARE(sidebar->property("width").toReal(), 0.0);
    }


    void closesTheSidebarRatherThanReachingIntoIt() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        for (const QString &name : {QStringLiteral("one.md"), QStringLiteral("two.md")}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(sidebar);
        QVERIFY(editor);

        // Closed: the key puts the panel there and the keyboard in it.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QVERIFY(window->property("sidebarOpen").toBool());
        QVERIFY(sidebar->property("listHasFocus").toBool());

        // Open with the keyboard back in the text — Esc does this, and so does
        // opening a document. The key takes the panel away rather than
        // interrupting the writing to reach into it.
        QVERIFY(QMetaObject::invokeMethod(editor, "forceActiveFocus"));
        QVERIFY(editor->property("activeFocus").toBool());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QVERIFY(!window->property("sidebarOpen").toBool());
        QCOMPARE(sidebar->property("width").toReal(), 0.0);
        QVERIFY(editor->property("activeFocus").toBool());

        // And from inside the panel it closes just the same.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QVERIFY(sidebar->property("listHasFocus").toBool());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QVERIFY(!window->property("sidebarOpen").toBool());
        QVERIFY(editor->property("activeFocus").toBool());
    }

    void putsTheCaretAtTheEndOfAnOpenedDocument() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("long.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Title\n\n");
        // Long enough that the end of it is well off the bottom of the window.
        for (int i = 0; i < 200; ++i)
            file.write(QStringLiteral("body line %1\n").arg(i).toUtf8());
        file.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QObject *flick = window->findChild<QObject *>(QStringLiteral("editorViewport"));
        QVERIFY(editor);
        QVERIFY(flick);

        // Writing carries on where the writing stopped, so an opened
        // document hands over its end rather than its beginning.
        backend.open(QUrl::fromLocalFile(path));
        QCOMPARE(editor->property("cursorPosition").toInt(),
                 editor->property("length").toInt());

        // And it is drawn there. The document is laid out in stages — the
        // text, then the hidden markers shrinking, then the line height —
        // so a caret measured too early sits against a layout that is no
        // longer on screen, halfway up the page.
        const QRectF caret = editor->property("cursorRectangle").toRectF();
        const qreal textHeight = editor->property("implicitHeight").toReal();
        qDebug() << "PROBE caret" << caret << "textHeight" << textHeight
                 << "contentY" << flick->property("contentY")
                 << "contentHeight" << flick->property("contentHeight");
        QVERIFY(textHeight > 0);
        QVERIFY2(caret.y() > textHeight * 0.9,
                 qPrintable(QStringLiteral("caret at %1 of %2")
                            .arg(caret.y()).arg(textHeight)));
        QVERIFY(flick->property("contentY").toReal() > 0);

        // And again switching between documents, which is how it is really
        // met: the layout in place is the previous document's.
        const QString second = folder.filePath(QStringLiteral("second.md"));
        QFile secondFile(second);
        QVERIFY(secondFile.open(QIODevice::WriteOnly));
        for (int i = 0; i < 60; ++i)
            secondFile.write(QStringLiteral("## Heading %1\n\nwith **bold** and `code` in it\n\n").arg(i).toUtf8());
        secondFile.close();
        backend.open(QUrl::fromLocalFile(second));
        const QRectF caret2 = editor->property("cursorRectangle").toRectF();
        const qreal textHeight2 = editor->property("implicitHeight").toReal();
        qDebug() << "PROBE2 caret" << caret2 << "textHeight" << textHeight2
                 << "contentY" << flick->property("contentY")
                 << "contentHeight" << flick->property("contentHeight");
        QVERIFY2(caret2.y() > textHeight2 * 0.9,
                 qPrintable(QStringLiteral("caret at %1 of %2")
                            .arg(caret2.y()).arg(textHeight2)));

        // Saving names the file but does not reload it, so writing is never
        // interrupted by the caret jumping back to the top.
        editor->setProperty("cursorPosition", 12);
        backend.saveAs(QUrl::fromLocalFile(folder.filePath(QStringLiteral("copy.md"))));
        QCOMPARE(editor->property("cursorPosition").toInt(), 12);
    }

    void walksTheSidebarWithTheKeyboard() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QVERIFY(QDir(folder.path()).mkdir(QStringLiteral("archive")));
        for (const QString &name : {QStringLiteral("one.md"), QStringLiteral("two.md")}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QVERIFY(sidebar);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));

        // archive/, one.md, two.md — moving down twice lands on the last row
        // and stays there rather than wrapping.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectNext"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectNext"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectNext"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "activateSelection"));
        QCOMPARE(backend.fileName(), QStringLiteral("two.md"));

        // Enter on a folder walks into it; going up comes back.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectPrevious"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectPrevious"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "activateSelection"));
        QCOMPARE(backend.folderUrl(),
                 QUrl::fromLocalFile(folder.filePath(QStringLiteral("archive"))));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "goUp"));
        QCOMPARE(backend.folderUrl(), QUrl::fromLocalFile(folder.path()));
    }

    void handsTheKeyboardBackWhenADocumentOpens() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QVERIFY(QDir(folder.path()).mkdir(QStringLiteral("archive")));
        for (const QString &name : {QStringLiteral("one.md"), QStringLiteral("two.md")}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(sidebar);
        QVERIFY(editor);

        // Browsing takes the keyboard, and the caret goes out with it.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));
        QVERIFY(sidebar->property("listHasFocus").toBool());
        QVERIFY(!editor->property("activeFocus").toBool());

        // Opening a document hands it straight back, so it can be written in
        // without closing the sidebar first.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectNext"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "activateSelection"));
        QCOMPARE(backend.fileName(), QStringLiteral("one.md"));
        QVERIFY(editor->property("activeFocus").toBool());
        QVERIFY(window->property("sidebarOpen").toBool());

        // Walking into a folder is not opening a document, so it keeps it.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectPrevious"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "activateSelection"));
        QCOMPARE(backend.folderName(), QStringLiteral("archive"));
        QVERIFY(sidebar->property("listHasFocus").toBool());
        QVERIFY(!editor->property("activeFocus").toBool());

        // Coming back up, the panel starts from the document being written.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "goUp"));
        QTRY_COMPARE(sidebar->property("selectedName").toString(),
                     QStringLiteral("one.md"));

        // Unsaved work asks nothing: the document being left is written out
        // on the way, and the keyboard lands in the one that opens.
        editor->setProperty("text", QStringLiteral("a draft"));
        QVERIFY(backend.modified());
        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectNext"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "activateSelection"));
        QCOMPARE(backend.fileName(), QStringLiteral("two.md"));
        QVERIFY(editor->property("activeFocus").toBool());
        QVERIFY(!backend.modified());

        QFile left(folder.filePath(QStringLiteral("one.md")));
        QVERIFY(left.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(left.readAll()), QStringLiteral("a draft"));
        left.close();

        // A document that cannot be opened is never opened, so the keyboard
        // stays with the browsing rather than following a document that never
        // arrived. A name that is merely absent is not that case any more: it
        // is a file the writer means to start. A name under a folder that is
        // not there is, because the first save would have nowhere to land.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));
        const QUrl missing = QUrl::fromLocalFile(
            folder.filePath(QStringLiteral("no-such-folder/missing.md")));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "requestOpen",
                                          Q_ARG(QVariant, QVariant(missing))));
        QCOMPARE(backend.fileName(), QStringLiteral("two.md"));
        QVERIFY(sidebar->property("listHasFocus").toBool());
        QVERIFY(!editor->property("activeFocus").toBool());
    }

    void autosavesOnceTheTypingStops() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("note.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("written and left alone"));
        QVERIFY(backend.modified());

        // A pause in the writing is the save; nothing has to be pressed.
        QTRY_VERIFY(!backend.modified());
        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(written.readAll()),
                 QStringLiteral("written and left alone"));
        written.close();

        // Closing does not wait out the pause, and does not ask either.
        editor->setProperty("text", QStringLiteral("one last thought"));
        QVERIFY(backend.modified());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "close"));
        QVERIFY(!backend.modified());
        QFile onClose(path);
        QVERIFY(onClose.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(onClose.readAll()),
                 QStringLiteral("one last thought"));
    }

    void keepsTheWorkWhenTheFileCannotBeWritten() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("locked.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("on disk");
        seed.close();
        // A real document to switch to, so the refusal is what stops the
        // switch rather than a target that was never openable.
        const QString other = folder.filePath(QStringLiteral("other.md"));
        QFile neighbour(other);
        QVERIFY(neighbour.open(QIODevice::WriteOnly));
        neighbour.write("somewhere else");
        neighbour.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("work that must not vanish"));
        QVERIFY(backend.modified());

        // A directory that cannot be written to is the same to QSaveFile as any
        // other failed write.
        QVERIFY(QFile::setPermissions(folder.path(), QFileDevice::ReadOwner
                                                     | QFileDevice::ExeOwner));

        QVERIFY2(!backend.saveBeforeLeaving(), "a failed save must report itself");

        // Leaving is refused, so the writer is still looking at their own text.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "requestOpen",
                                          Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(other)))));
        QCOMPARE(editor->property("text").toString(),
                 QStringLiteral("work that must not vanish"));
        QVERIFY(backend.modified());

        QVERIFY(QFile::setPermissions(folder.path(), QFileDevice::ReadOwner
                                                     | QFileDevice::WriteOwner
                                                     | QFileDevice::ExeOwner));
    }

    void doesNotAutosaveOverAnUnansweredExternalChange() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("shared.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("original");
        seed.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("my version"));
        QVERIFY(backend.modified());

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);
        QFile outside(path);
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("their version");
        outside.close();
        QTRY_COMPARE(conflict.count(), 1);

        // The file on disk is not ours to overwrite until that is answered, so
        // the work waits in a draft rather than landing on top of it.
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(after.readAll()), QStringLiteral("their version"));
        after.close();
        QVERIFY(backend.modified());

        // Pressing Ctrl+S does not pre-empt the question either: the prompt is
        // on screen asking which version to keep.
        backend.save();
        QFile stillTheirs(path);
        QVERIFY(stillTheirs.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(stillTheirs.readAll()),
                 QStringLiteral("their version"));
        stillTheirs.close();

        // Saving somewhere else is not the contested file, so it goes through.
        const QString copy = folder.filePath(QStringLiteral("copy.md"));
        backend.saveAs(QUrl::fromLocalFile(copy));
        QVERIFY(QFileInfo::exists(copy));

        // Escape and a click outside dismiss it, the way they dismiss a popup
        // anywhere else in Omarchy. Dismissing answers nothing: the guard
        // stays up, so neither copy is touched by it.
        QObject *prompt = window->findChild<QObject *>(
            QStringLiteral("externalChangeDialog"));
        QVERIFY(prompt);
        // Asserted as configuration rather than by pressing Escape: the window
        // is never shown here, so a synthetic key never reaches the popup and
        // the behavioural version of this passes whatever the policy says.
        QCOMPARE(prompt->property("closePolicy").toInt(),
                 0x10 | 0x01);  // Popup.CloseOnEscape | Popup.CloseOnPressOutside

        // A conflict is about one file. Opening another document ends it,
        // rather than following the writer and refusing to save that one too.
        const QString elsewhere = folder.filePath(QStringLiteral("elsewhere.md"));
        QFile other(elsewhere);
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.close();
        backend.open(QUrl::fromLocalFile(elsewhere));
        editor->setProperty("text", QStringLiteral("a different document"));
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));
        QFile unrelated(elsewhere);
        QVERIFY(unrelated.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(unrelated.readAll()),
                 QStringLiteral("a different document"));
        unrelated.close();

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("my version"));

        // Once it is answered, keeping your version saves over it as asked.
        // A second outside change still gets through. This one replaces the
        // file the way another editor's atomic save does, which takes the old
        // inode — and the watched path with it — out from under the watcher.
        QSignalSpy second(&backend, &Backend::externalChangeDetected);
        QSaveFile replacement(path);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write("changed once more");
        QVERIFY(replacement.commit());
        QTRY_COMPARE(second.count(), 1);

        backend.keepExternalVersion();
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));
        QFile kept(path);
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(kept.readAll()), QStringLiteral("my version"));
    }

    void showsTheDocumentTheNewTabOpened() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString first = folder.filePath(QStringLiteral("first.md"));
        const QString second = folder.filePath(QStringLiteral("second.md"));
        QFile a(first);
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write("# First document\n\nalpha alpha alpha");
        a.close();
        QFile b(second);
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.write("# Second document\n\nbeta beta beta");
        b.close();

        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> created(createMainWindow(engine, component, backend));
        QVERIFY2(created, qPrintable(component.errorString()));
        QQuickWindow *window = qobject_cast<QQuickWindow *>(created.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(first));
        QTRY_VERIFY(editor->property("text").toString().contains(QStringLiteral("First document")));

        // Ctrl+T, then the sidebar: a new tab and a document opened into it.
        backend.newBuffer();
        QTRY_COMPARE(editor->property("text").toString(), QString());
        QVERIFY(QMetaObject::invokeMethod(window, "requestOpen",
                                          Q_ARG(QVariant, QUrl::fromLocalFile(second))));

        QCOMPARE(backend.buffers().size(), 2);
        QTRY_VERIFY(editor->property("text").toString().contains(QStringLiteral("Second document")));
        QVERIFY(!editor->property("text").toString().contains(QStringLiteral("First document")));
    }

    void escapeLeavesARemovedFileRemoved() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("removed.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("on disk");
        seed.close();

        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> created(createMainWindow(engine, component, backend));
        QVERIFY2(created, qPrintable(component.errorString()));
        QQuickWindow *window = qobject_cast<QQuickWindow *>(created.data());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("my version"));

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);
        QVERIFY(QFile::remove(path));
        QTRY_COMPARE(conflict.count(), 1);

        QObject *prompt = window->findChild<QObject *>(
            QStringLiteral("externalChangeDialog"));
        QVERIFY(prompt);
        QTRY_VERIFY(prompt->property("visible").toBool());

        // Escape is how a popup is dismissed everywhere else in Omarchy, and
        // here it answers nothing: the removal stands and the writing carries
        // on. Autosave still has nothing it may write to that path, so the
        // file the writer deleted does not come back behind them.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!prompt->property("visible").toBool());
        QCOMPARE(backend.status(),
                 QStringLiteral("Left removed.md deleted; Ctrl+S writes it again"));
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(backend.modified());

        // Ctrl+S is the writer asking for it back, which is an answer, so the
        // file is written where it was.
        backend.save();
        QVERIFY(QFileInfo::exists(path));
        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(written.readAll()), QStringLiteral("my version"));
        written.close();
        QVERIFY(!backend.modified());
    }

    void escapeOnAChangedFileAsksAgainAtTheNextSave() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("contested.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("original");
        seed.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("my version"));

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);
        QFile outside(path);
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("their version");
        outside.close();
        QTRY_COMPARE(conflict.count(), 1);

        // With a copy of the work on disk, dismissing cannot mean take mine:
        // it leaves both where they are.
        backend.dismissExternalChange();
        QCOMPARE(backend.status(), QStringLiteral("Left contested.md alone; Ctrl+S asks again"));
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));
        QFile theirs(path);
        QVERIFY(theirs.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(theirs.readAll()), QStringLiteral("their version"));
        theirs.close();

        // And the save that would overwrite it puts the question back rather
        // than answering it by writing.
        backend.save();
        QCOMPARE(conflict.count(), 2);
        QFile untouched(path);
        QVERIFY(untouched.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(untouched.readAll()), QStringLiteral("their version"));
    }

    void keepsTheGuardWhenTheNextDocumentWillNotOpen() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("contested.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("original");
        seed.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("my version"));

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);
        QFile outside(path);
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("their version");
        outside.close();
        QTRY_COMPARE(conflict.count(), 1);

        // An open that fails replaces nothing, so the document still on screen
        // is the contested one and its guard has to stand.
        backend.open(QUrl::fromLocalFile(folder.filePath(QStringLiteral("missing.md"))));
        backend.save();
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(after.readAll()), QStringLiteral("their version"));
    }

    void asksAgainWhenTheReloadItselfFails() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("vanishing.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("original");
        seed.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        // Local changes are what makes an outside edit a conflict at all.
        editor->setProperty("text", QStringLiteral("mine, unsaved"));
        QVERIFY(backend.modified());

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);
        QSaveFile outside(path);
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("theirs");
        QVERIFY(outside.commit());
        QTRY_COMPARE(conflict.count(), 1);

        // Taking their version cannot be done if there is no longer a their
        // version to take. That has answered nothing, and the prompt has
        // already closed itself, so it is raised again rather than leaving the
        // guard standing with nothing able to clear it.
        QVERIFY(QFile::remove(path));
        backend.reloadFromDisk();
        QCOMPARE(conflict.count(), 2);
        QCOMPARE(conflict.last().at(0).toBool(), true);  // reported as deleted
    }

    void asksAgainWhenTheFileIsReplacedTwice() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("contested.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("original");
        seed.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        // Local changes are what makes an outside edit a conflict at all. With
        // none, the newer text is simply taken.
        editor->setProperty("text", QStringLiteral("mine, unsaved"));
        QVERIFY(backend.modified());

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);

        // An atomic save from another editor replaces the file rather than
        // rewriting it, which takes the watched inode away with it.
        QSaveFile first(path);
        QVERIFY(first.open(QIODevice::WriteOnly));
        first.write("theirs");
        QVERIFY(first.commit());
        QTRY_COMPARE(conflict.count(), 1);

        // Nothing has answered the prompt, and nothing has re-opened the file.
        // A second replacement still has to reach the writer, or the one
        // chance to ask went with the first inode.
        QSaveFile again(path);
        QVERIFY(again.open(QIODevice::WriteOnly));
        again.write("theirs, again");
        QVERIFY(again.commit());
        QTRY_COMPARE(conflict.count(), 2);
    }

    void reloadingAlsoEndsTheConflict() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("shared.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("original");
        seed.close();

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("mine"));

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);
        QFile outside(path);
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("theirs");
        outside.close();
        QTRY_COMPARE(conflict.count(), 1);

        // Taking their version is the other answer, and saving resumes on it.
        backend.reloadFromDisk();
        QCOMPARE(editor->property("text").toString(), QStringLiteral("theirs"));
        editor->setProperty("text", QStringLiteral("theirs, then mine"));
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(after.readAll()),
                 QStringLiteral("theirs, then mine"));
    }

    void namesAnUntitledDocumentFromItsFirstLine() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        // A document that was never named takes one from its first line when
        // it is left, rather than stopping the writer to ask for it.
        editor->setProperty("text", QStringLiteral("Field notes\n\nbody"));
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveBeforeLeaving"));
        QCOMPARE(backend.fileName(), QStringLiteral("Field notes.md"));
        QFile named(folder.filePath(QStringLiteral("Field notes.md")));
        QVERIFY(named.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(named.readAll()),
                 QStringLiteral("Field notes\n\nbody"));
        named.close();

        // The name gives way rather than the writing: a second note opening
        // on the same line lands beside the first.
        // Its own tab session, or it restores the tab the first one just named
        // and is no longer an untitled document at all.
        QTemporaryDir secondState;
        QVERIFY(secondState.isValid());
        Backend second(secondState.path());
        second.setFolder(QUrl::fromLocalFile(folder.path()));
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &second);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QScopedPointer<QObject> secondWindow(component.create());
        QVERIFY2(secondWindow, qPrintable(component.errorString()));
        QObject *secondEditor =
            secondWindow->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(secondEditor);
        secondEditor->setProperty("text", QStringLiteral("Field notes\n\nagain"));
        QVERIFY(QMetaObject::invokeMethod(&second, "saveBeforeLeaving"));
        QCOMPARE(second.fileName(), QStringLiteral("Field notes 2.md"));
    }

    void doesNotSettleForAnOlderDraftWhenClosing() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QTemporaryDir state;
        QVERIFY(state.isValid());
        const QString path = folder.filePath(QStringLiteral("draft.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.close();

        qputenv("XDG_DATA_HOME", state.path().toUtf8());
        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));

        // A draft of an older version, made when the document itself could not
        // be written but the draft still could.
        QVERIFY(QFile::setPermissions(folder.path(), QFileDevice::ReadOwner
                                                     | QFileDevice::ExeOwner));
        editor->setProperty("text", QStringLiteral("old version"));
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveNow"));

        const QString appData =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(appData.startsWith(state.path()));
        QDir drafts(appData);
        // Named for the drafts alone: the tab session writes its own session.json
        // in this directory, and it is not a draft of anything.
        const QStringList written =
            drafts.entryList({QStringLiteral("recovery-*.json")}, QDir::Files);
        QCOMPARE(written.size(), 1);
        const QString draftPath = drafts.filePath(written.first());

        // Newer work, and now nowhere at all to put it.
        editor->setProperty("text", QStringLiteral("new version"));
        QVERIFY(QFile::setPermissions(appData, QFileDevice::ReadOwner
                                               | QFileDevice::ExeOwner));

        // The old draft is still sitting there, but it holds the wrong text, so
        // it is no reason to let the window take the new text with it.
        QVERIFY2(!backend.saveBeforeClosing(),
                 "an older draft does not stand in for this attempt");
        QVERIFY(QMetaObject::invokeMethod(window.data(), "close"));
        QVERIFY(window->property("visible").toBool());

        QFile stale(draftPath);
        QVERIFY(stale.open(QIODevice::ReadOnly));
        QVERIFY2(QString::fromUtf8(stale.readAll()).contains(QStringLiteral("old version")),
                 "the draft on disk is the older one, which is the point");
        stale.close();

        QVERIFY(QFile::setPermissions(appData, QFileDevice::ReadOwner
                                               | QFileDevice::WriteOwner
                                               | QFileDevice::ExeOwner));
        QVERIFY(QFile::setPermissions(folder.path(), QFileDevice::ReadOwner
                                                     | QFileDevice::WriteOwner
                                                     | QFileDevice::ExeOwner));
        qunsetenv("XDG_DATA_HOME");
    }

    void refusesTheFirstCloseWhenTheWorkFitsNowhere() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QTemporaryDir state;
        QVERIFY(state.isValid());
        const QString path = folder.filePath(QStringLiteral("stuck.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.close();

        // A recovery slot of its own, so taking it away takes away the last
        // place the work could go.
        qputenv("XDG_DATA_HOME", state.path().toUtf8());
        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("the only copy"));
        QVERIFY(backend.modified());

        const QString appData =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(appData.startsWith(state.path()));
        QVERIFY(QFile::setPermissions(folder.path(), QFileDevice::ReadOwner
                                                     | QFileDevice::ExeOwner));
        QVERIFY(QFile::setPermissions(appData, QFileDevice::ReadOwner
                                               | QFileDevice::ExeOwner));

        QVERIFY2(!backend.saveBeforeClosing(),
                 "with nowhere to write, the work reached nowhere");

        // The first close is refused rather than dropping the only copy.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "close"));
        QVERIFY(window->property("visible").toBool());
        QVERIFY(window->property("closeAnyway").toBool());

        // That override belongs only to the text from the refused attempt.
        // Continuing to write revokes it, so a later close tries persistence
        // again instead of silently discarding the newer text.
        editor->setProperty("text", QStringLiteral("newer only copy"));
        QVERIFY(!window->property("closeAnyway").toBool());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "close"));
        QVERIFY(window->property("visible").toBool());
        QVERIFY(window->property("closeAnyway").toBool());

        QVERIFY(QFile::setPermissions(appData, QFileDevice::ReadOwner
                                               | QFileDevice::WriteOwner
                                               | QFileDevice::ExeOwner));
        QVERIFY(QFile::setPermissions(folder.path(), QFileDevice::ReadOwner
                                                     | QFileDevice::WriteOwner
                                                     | QFileDevice::ExeOwner));
        qunsetenv("XDG_DATA_HOME");
    }

    void discardsAnEmptyUntitledDocument() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        // Nothing was written, so there is nothing to name and nothing to keep.
        editor->setProperty("text", QStringLiteral("   \n\n  "));
        QVERIFY(QMetaObject::invokeMethod(&backend, "saveBeforeLeaving"));
        QVERIFY(!backend.modified());
        QCOMPARE(QDir(folder.path())
                     .entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 0);
    }

    void keepsTheSidebarSelectionWhenTheFolderIsReRead() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        for (const QString &name : {QStringLiteral("one.md"), QStringLiteral("two.md")}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QVERIFY(sidebar);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));

        QVERIFY(QMetaObject::invokeMethod(sidebar, "selectNext"));
        QCOMPARE(sidebar->property("selectedName").toString(), QStringLiteral("two.md"));

        // The folder is re-read whenever anything in it changes — a save is
        // enough — and the rows can move; the keyboard should stay on the
        // row it was on rather than be thrown back to the top.
        QVERIFY(QMetaObject::invokeMethod(&backend, "createFolder",
                                          Q_ARG(QString, QStringLiteral("archive"))));
        QTRY_COMPARE(sidebar->property("selectedName").toString(),
                     QStringLiteral("two.md"));

        // Reopening the panel starts from the open document instead.
        backend.open(QUrl::fromLocalFile(folder.filePath(QStringLiteral("one.md"))));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, false)));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));
        QCOMPARE(sidebar->property("selectedName").toString(), QStringLiteral("one.md"));
    }

    void createsAndOpensADocumentFromTheSidebar() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        Backend backend;
        backend.setFolder(QUrl::fromLocalFile(folder.path()));

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QObject *nameField = window->findChild<QObject *>(QStringLiteral("newEntryField"));
        QVERIFY(sidebar);
        QVERIFY(nameField);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));

        QVERIFY(QMetaObject::invokeMethod(sidebar, "beginNewDocument"));
        QVERIFY(sidebar->property("creating").toBool());
        nameField->setProperty("text", QStringLiteral("Field notes"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "commitNewEntry"));
        QVERIFY(!sidebar->property("creating").toBool());

        // The new document is created and opened, ready to be written in,
        // with the selection left on it rather than back at the top.
        QVERIFY(QFileInfo::exists(folder.filePath(QStringLiteral("Field notes.md"))));
        QCOMPARE(backend.fileName(), QStringLiteral("Field notes.md"));
        QCOMPARE(sidebar->property("selectedName").toString(),
                 QStringLiteral("Field notes.md"));

        QVERIFY(QMetaObject::invokeMethod(sidebar, "beginNewFolder"));
        nameField->setProperty("text", QStringLiteral("archive"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "commitNewEntry"));
        QVERIFY(QFileInfo(folder.filePath(QStringLiteral("archive"))).isDir());
        QCOMPARE(sidebar->property("selectedName").toString(), QStringLiteral("archive"));

        // An abandoned name creates nothing.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "beginNewDocument"));
        nameField->setProperty("text", QStringLiteral("discarded"));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "cancelNewEntry"));
        QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("discarded.md"))));
    }

    void followsThePointerWhenTheEdgeIsDragged() {
        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QVERIFY(sidebar);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));
        window->setProperty("width", 1280);
        QCOMPARE(sidebar->property("width").toReal(), 240.0);

        // The pointer starts on the edge, where the handle is.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "beginResize", Q_ARG(QVariant, 240.0)));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "resizeTo", Q_ARG(QVariant, 300.0)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(), 300);

        // A pointer that has not moved asks for the width it already has. The
        // handle rides the edge it moves, so a width measured against it used
        // to come back different every time it was asked — which is what the
        // drag twitching was.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "resizeTo", Q_ARG(QVariant, 300.0)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(), 300);

        // One pixel of pointer, one pixel of panel, both ways.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "resizeTo", Q_ARG(QVariant, 360.0)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(), 360);
        QVERIFY(QMetaObject::invokeMethod(sidebar, "resizeTo", Q_ARG(QVariant, 200.0)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(), 200);

        // Widths are kept at text scale 1, and the pointer is not: a drag to a
        // device pixel lands on the logical width under it, once divided.
        backend.setTextScale(1.25);
        QCOMPARE(sidebar->property("width").toReal(), 250.0);
        QVERIFY(QMetaObject::invokeMethod(sidebar, "beginResize", Q_ARG(QVariant, 250.0)));
        QVERIFY(QMetaObject::invokeMethod(sidebar, "resizeTo", Q_ARG(QVariant, 500.0)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(), 400);
    }

    void keepsTheWritingColumnWhenDraggedWider() {
        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("fileSidebar"));
        QVERIFY(sidebar);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setSidebarOpen",
                                          Q_ARG(QVariant, true)));
        window->setProperty("width", 1280);

        QVERIFY(QMetaObject::invokeMethod(sidebar, "requestLogicalWidth",
                                          Q_ARG(QVariant, 380)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(), 380);

        // Dragging past either end is held at the limit, and the wide end
        // always leaves the editor its minimum.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "requestLogicalWidth",
                                          Q_ARG(QVariant, 40)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(),
                 sidebar->property("minimumLogicalWidth").toInt());

        QVERIFY(QMetaObject::invokeMethod(sidebar, "requestLogicalWidth",
                                          Q_ARG(QVariant, 5000)));
        QCOMPARE(window->property("sidebarLogicalWidth").toInt(), 1280 - 420);

        // The drag writes the width once it is let go, not on every frame.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "requestLogicalWidth",
                                          Q_ARG(QVariant, 300)));
        QCOMPARE(backend.sidebarWidth(), 240);
        QVERIFY(QMetaObject::invokeMethod(sidebar, "widthCommitted"));
        QCOMPARE(backend.sidebarWidth(), 300);
    }

    void readsTomlValuesPastInlineComments() {
        // Every colour in every theme is a quoted "#rrggbb": the hash inside the quotes stays.
        QCOMPARE(Backend::tomlValue(QStringLiteral(" \"#1a1b26\" ")), QStringLiteral("#1a1b26"));
        QCOMPARE(Backend::tomlValue(QStringLiteral("\"#EDE6D6\"   # unbleached cloth")),
                 QStringLiteral("#EDE6D6"));
        QCOMPARE(Backend::tomlValue(QStringLiteral("'#445566'# no space before the hash")),
                 QStringLiteral("#445566"));
        QCOMPARE(Backend::tomlValue(QStringLiteral("\"rebecca#purple\"")),
                 QStringLiteral("rebecca#purple"));
        QCOMPARE(Backend::tomlValue(QStringLiteral(" #1a1b26 ")), QStringLiteral("#1a1b26"));
        QCOMPARE(Backend::tomlValue(QStringLiteral("\"#1a1b26")), QStringLiteral("\"#1a1b26"));
        QCOMPARE(Backend::tomlValue(QStringLiteral("\"\"")), QString());
        QCOMPARE(Backend::tomlValue(QString()), QString());
    }

    void keepsDefaultsForColorsItCannotParse() {
        ScopedTheme theme(
            "mode = \"dark\"          # warmed ink\r\n"
            "accent = \"#112233\"     # shell-white pigment\r\n"
            "selection = '#445566'\r\n"
            "background = \"#1a1b17\" # ink, warmed\r\n"
            "foreground = \"unbleached cloth\"\r\n");
        QVERIFY(theme.ok);

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#1a1b17"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#eeeeee"));
        QVERIFY(backend.darkMode());
    }

    void takesItsDefaultsFromTheModeTheThemeAsksFor() {
        ScopedTheme theme(
            "mode = \"light\"\n"
            "background = \"#ffffff\"\n"
            "foreground = \"unbleached cloth\"\n");
        QVERIFY(theme.ok);

        Backend backend;
        QVERIFY(!backend.darkMode());
        QCOMPARE(backend.themeForeground(), QStringLiteral("#222324"));
    }

    void ignoresStrikethroughInsideCodeSpans() {
        // Strikethrough answers to the same rule as the other inline markup.
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("`a ~~b~~ c`")).size(), 0);
    }

    void ignoresInlineMarkdownInsideCodeSpans() {
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("`The_brown_fox` jumps")).size(), 0);
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("`a **b** [c](d)` and `e`")).size(), 0);

        const auto mixed = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("`code_span` then *real* emphasis"));
        QCOMPARE(mixed.size(), 1);
        QCOMPARE(mixed.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(mixed.at(0).content.start, 18);
    }

    void keepsMarkupThatEnclosesACodeSpan() {
        const auto spanning = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("_a `b` c_"));
        QCOMPARE(spanning.size(), 1);
        QCOMPARE(spanning.at(0).kind, MarkdownHighlighter::InlineKind::Italic);
        QCOMPARE(spanning.at(0).content.start, 1);
        QCOMPARE(spanning.at(0).content.length, 7);

        const auto linked = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("[see `code`](url)"));
        QCOMPARE(linked.size(), 1);
        QCOMPARE(linked.at(0).kind, MarkdownHighlighter::InlineKind::Link);
        QCOMPARE(linked.at(0).content.start, 1);
        QCOMPARE(linked.at(0).content.length, 10);

        const auto wrapped = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("*`code`*"));
        QCOMPARE(wrapped.size(), 1);
        QCOMPARE(wrapped.at(0).kind, MarkdownHighlighter::InlineKind::Italic);

        const auto bold = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**a `b` c**"));
        QCOMPARE(bold.size(), 1);
        QCOMPARE(bold.at(0).kind, MarkdownHighlighter::InlineKind::Bold);
        QCOMPARE(bold.at(0).content.start, 2);
        QCOMPARE(bold.at(0).content.length, 7);

        // The closing underscore is inside the code span, so it is not a marker.
        QCOMPARE(MarkdownHighlighter::inlineMarkup(
                     QStringLiteral("_a `b_ c` d_")).size(), 0);
    }

    void stylesAnEnclosedCodeSpanAsCode() {
        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        document.setPlainText(QStringLiteral("_a `b` c_"));
        // The constructor's own rehighlight is queued, so ask for one directly.
        highlighter.rehighlight();

        const QTextBlock block = document.firstBlock();
        QVERIFY(block.isValid());
        const auto formatAt = [&block](int index) {
            QTextCharFormat found;
            for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                if (index >= range.start && index < range.start + range.length)
                    found = range.format;
            }
            return found;
        };

        // "a" is italic and carries no code background.
        QVERIFY(formatAt(1).fontItalic());
        QCOMPARE(formatAt(1).background().style(), Qt::NoBrush);

        // The code span keeps the code background and is not italicised.
        QVERIFY(formatAt(4).background().style() != Qt::NoBrush);
        QVERIFY(!formatAt(4).fontItalic());
    }

    void findsStrikethroughRanges() {
        const auto markup =
            MarkdownHighlighter::inlineMarkup(QStringLiteral("keep ~~drop this~~ keep"));
        QCOMPARE(markup.size(), 1);
        QCOMPARE(markup.at(0).kind, MarkdownHighlighter::InlineKind::Strikethrough);
        QCOMPARE(markup.at(0).content.start, 7);
        QCOMPARE(markup.at(0).content.length, 9);
        QCOMPARE(markup.at(0).markers[0].length, 2);
        QCOMPARE(markup.at(0).markers[1].length, 2);
    }

    void togglesWrappedSelection() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string wrappedText
                property string unwrappedText
                property int unwrappedSelectionStart
                property int unwrappedSelectionEnd
                property string emptyToggleText
                property string italicInsideBold
                property string italicAroundBold

                Component.onCompleted: {
                    text = "strike this";
                    select(0, 6);
                    EditorMutations.toggleWrap(this, "~~", "~~");
                    wrappedText = text;
                    EditorMutations.toggleWrap(this, "~~", "~~");
                    unwrappedText = text;
                    unwrappedSelectionStart = selectionStart;
                    unwrappedSelectionEnd = selectionEnd;

                    text = "";
                    cursorPosition = 0;
                    EditorMutations.toggleWrap(this, "~~", "~~");
                    EditorMutations.toggleWrap(this, "~~", "~~");
                    emptyToggleText = text;

                    text = "**bold**";
                    select(2, 6);
                    EditorMutations.toggleWrap(this, "*", "*");
                    italicInsideBold = text;

                    text = "**bold**";
                    select(0, 8);
                    EditorMutations.toggleWrap(this, "*", "*");
                    italicAroundBold = text;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/ToggleHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("~~strike~~ this"));
        QCOMPARE(editor->property("unwrappedText").toString(),
                 QStringLiteral("strike this"));
        QCOMPARE(editor->property("unwrappedSelectionStart").toInt(), 0);
        QCOMPARE(editor->property("unwrappedSelectionEnd").toInt(), 6);
        QCOMPARE(editor->property("emptyToggleText").toString(), QString());
        QCOMPARE(editor->property("italicInsideBold").toString(),
                 QStringLiteral("***bold***"));
        QCOMPARE(editor->property("italicAroundBold").toString(),
                 QStringLiteral("***bold***"));
    }

    void reservesAnOpaqueFooterBelowTheEditor() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        auto *footer = window->findChild<QQuickItem *>(QStringLiteral("footer"));
        auto *editorViewport = window->findChild<QQuickItem *>(
            QStringLiteral("editorViewport"));
        auto *saveButton = window->findChild<QQuickItem *>(QStringLiteral("saveButton"));
        QVERIFY(footer);
        QVERIFY(editorViewport);
        QVERIFY(saveButton);

        QCOMPARE(footer->opacity(), qreal(1));
        const QColor footerColor = footer->property("color").value<QColor>();
        QCOMPARE(footerColor.alpha(), 255);
        QCOMPARE(footerColor, QColor(backend.themeBackground()));
        QCOMPARE(editorViewport->mapToScene(QPointF(0, editorViewport->height())).y(),
                 footer->mapToScene(QPointF()).y());

        backend.setTextScale(0.5);
        QVERIFY(saveButton->mapToScene(QPointF()).y()
                >= footer->mapToScene(QPointF()).y());
        QCOMPARE(editorViewport->mapToScene(QPointF(0, editorViewport->height())).y(),
                 footer->mapToScene(QPointF()).y());
    }

    void breaksLinesOnReturn() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));

        auto *window = qobject_cast<QQuickWindow *>(object.data());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QMetaObject::invokeMethod(editor, "forceActiveFocus");
        QVERIFY(editor->property("activeFocus").toBool());

        auto text = [&] { return editor->property("text").toString(); };
        auto load = [&](const QString &content, int position) {
            editor->setProperty("text", content);
            editor->setProperty("cursorPosition", position);
        };
        auto returnKey = [&] { QTest::keyClick(window, Qt::Key_Return); };

        // Ending a paragraph leaves the blank line that separates it from the
        // next one.
        load(QStringLiteral("one\n\ntwo"), 8);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\ntwo\n\n"));

        // On a line that is already blank there is nothing to separate from,
        // so Return is worth one line, not two.
        load(QStringLiteral("one\n\ntwo"), 4);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\ntwo"));
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\n\ntwo"));

        // An empty document is a blank line too.
        load(QString(), 0);
        returnKey();
        QCOMPARE(text(), QStringLiteral("\n"));

        // Whitespace left behind on a line still reads as blank.
        load(QStringLiteral("one\n  \ntwo"), 5);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n \n \ntwo"));

        // A selection dragged right to left leaves the caret on the blank line
        // it began on, but the break lands on the line it leaves behind.
        load(QStringLiteral("one\n\ntwo"), 7);
        QMetaObject::invokeMethod(editor, "moveCursorSelection", Q_ARG(int, 4));
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\no"));

        // A list carries its marker down, and an item left empty drops the
        // marker to end the list.
        load(QStringLiteral("- item"), 6);
        returnKey();
        QCOMPARE(text(), QStringLiteral("- item\n- "));
        returnKey();
        QCOMPARE(text(), QStringLiteral("- item\n\n"));

        // Inside a code fence every line is its own, blank ones included.
        load(QStringLiteral("```\ncode\n```"), 8);
        returnKey();
        QCOMPARE(text(), QStringLiteral("```\ncode\n\n```"));
        returnKey();
        QCOMPARE(text(), QStringLiteral("```\ncode\n\n\n```"));
    }

    void closesParagraphBreaksOnBackspace() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));

        auto *window = qobject_cast<QQuickWindow *>(object.data());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QMetaObject::invokeMethod(editor, "forceActiveFocus");
        QVERIFY(editor->property("activeFocus").toBool());

        auto text = [&] { return editor->property("text").toString(); };
        auto caret = [&] { return editor->property("cursorPosition").toInt(); };
        auto load = [&](const QString &content, int position) {
            editor->setProperty("text", content);
            editor->setProperty("cursorPosition", position);
        };
        auto returnKey = [&] { QTest::keyClick(window, Qt::Key_Return); };
        auto backspaceKey = [&] { QTest::keyClick(window, Qt::Key_Backspace); };

        // Backspace at the head of a paragraph closes the whole break above
        // it, so the two paragraphs join in one press rather than two, and
        // the caret lands where they meet.
        load(QStringLiteral("one\n\ntwo"), 5);
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("onetwo"));
        QCOMPARE(caret(), 3);

        // Ending a paragraph writes both breaks at once, and one Backspace
        // takes both back. A Return on the blank line of "one\n\n\ntwo" can
        // leave this very document with the caret in this very place, so it
        // is not provenance that decides here: it is that the gap is left
        // standing either way, and this is much the commoner press.
        load(QStringLiteral("one\n\ntwo"), 3);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\n\ntwo"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n\ntwo"));
        QCOMPARE(caret(), 3);

        // Which is what a Return inside a wide gap costs: the same rule reads
        // this as the paragraph-ending press it cannot be told apart from, so
        // the writer gets back one blank line fewer than they had. The gap
        // still separates the paragraphs, and one more Return returns it.
        load(QStringLiteral("one\n\n\n\ntwo"), 4);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\n\n\ntwo"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\ntwo"));
        QCOMPARE(caret(), 3);

        // Return on a blank line writes a single break, so Backspace must
        // take back a single break too. Closing the pair behind the caret
        // would swallow the separator that was there beforehand and leave
        // the two paragraphs with nothing between them.
        load(QStringLiteral("one\n\ntwo"), 4);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\ntwo"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n\ntwo"));

        // The same holds however far the gap has grown: each Return is worth
        // one Backspace.
        load(QStringLiteral("one\n\ntwo"), 4);
        returnKey();
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n\n\ntwo"));
        backspaceKey();
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n\ntwo"));

        // A line with nothing but spaces on it is a blank line, and Return
        // above one writes a single break there too.
        load(QStringLiteral("one\n  \ntwo"), 4);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n  \ntwo"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n  \ntwo"));

        // On the blank line a document ends with, the two Returns leave the
        // same text behind and no reading of it says which was pressed.
        // Backspace takes one break, because taking two would carry off the
        // line the document already ended with.
        load(QStringLiteral("one\n"), 4);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\n"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n"));

        // Which costs the paragraph that ends a document a second press,
        // and that is the whole of what it costs: nothing is lost on the
        // way through.
        load(QStringLiteral("one\n\ntwo"), 8);
        returnKey();
        QCOMPARE(text(), QStringLiteral("one\n\ntwo\n\n"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n\ntwo\n"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n\ntwo"));

        // An empty document is a blank line, so it takes two Returns to open
        // a gap and two Backspaces to close it again.
        load(QString(), 0);
        returnKey();
        returnKey();
        QCOMPARE(text(), QStringLiteral("\n\n"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("\n"));
        backspaceKey();
        QCOMPARE(text(), QString());

        // A document holding nothing but spaces is a blank line as well, and
        // the spaces are text: they must still be there at the end of it.
        load(QStringLiteral("   "), 3);
        returnKey();
        returnKey();
        QCOMPARE(text(), QStringLiteral("   \n\n"));
        backspaceKey();
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("   "));

        // Return at the very head of a document writes both breaks, since
        // the paragraph below it is not blank, and one Backspace takes them.
        load(QStringLiteral("two"), 0);
        returnKey();
        QCOMPARE(text(), QStringLiteral("\n\ntwo"));
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("two"));
        QCOMPARE(caret(), 0);

        // Spaces left on the line above a break do not make the break any
        // less of one: Return between them and the paragraph below wrote
        // both of these, so Backspace still takes both.
        load(QStringLiteral("one\n  \n\ntwo"), 8);
        backspaceKey();
        QCOMPARE(text(), QStringLiteral("one\n  two"));
        QCOMPARE(caret(), 6);
    }

    void writesAPastedImageBesideTheDocument() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("notes.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
        seed.write("# Notes\n");
        seed.close();

        QImage pasted(4, 3, QImage::Format_RGB32);
        pasted.fill(Qt::red);
        clipboard->setImage(pasted);

        Backend backend;
        backend.open(QUrl::fromLocalFile(path));

        const QString reference = backend.saveClipboardImage();
        QCOMPARE(reference, QStringLiteral("images/") + QFileInfo(reference).fileName());

        // The reference is relative to the document, so it resolves beside it
        // and stays inside the folder the preview is allowed to read.
        const QString written = folder.filePath(reference);
        QVERIFY(QFileInfo::exists(written));
        QCOMPARE(QImage(written).size(), pasted.size());

        // A second paste in the same second takes a name of its own rather than
        // writing over the first.
        const QString second = backend.saveClipboardImage();
        QVERIFY(!second.isEmpty());
        QVERIFY(second != reference);
        QVERIFY(QFileInfo::exists(folder.filePath(second)));

        clipboard->clear();
    }

    void refusesToPasteAnImageIntoAnUntitledDocument() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);

        QImage pasted(2, 2, QImage::Format_RGB32);
        pasted.fill(Qt::blue);
        clipboard->setImage(pasted);

        // Nothing is written, because an untitled document has no folder for the
        // image to sit in that a later Save As would keep it beside.
        Backend backend;
        QVERIFY(backend.saveClipboardImage().isEmpty());
        QVERIFY(backend.status().contains(QStringLiteral("Save the document first")));

        clipboard->clear();
    }

    void rendersAPastedImageInThePreview() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("notes.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
        seed.write("# Notes\n");
        seed.close();

        QImage source(6, 4, QImage::Format_RGB32);
        source.fill(Qt::green);
        clipboard->setImage(source);

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        backend.open(QUrl::fromLocalFile(path));
        const QString reference = backend.saveClipboardImage();
        QVERIFY(!reference.isEmpty());

        QObject *preview = window->findChild<QObject *>(QStringLiteral("renderedPreview"));
        QVERIFY(preview);
        backend.attachPreviewDocument(
            preview->property("textDocument").value<QQuickTextDocument *>());
        backend.setPreviewMarkdown(QStringLiteral("![a note](") + reference
                                   + QStringLiteral(")"));

        auto *previewDocument = preview->property("textDocument")
                                    .value<QQuickTextDocument *>()->textDocument();
        QVERIFY(previewDocument);

        // The paste writes where the preview is allowed to read, so the
        // reference it inserted resolves to a picture rather than to nothing.
        QString imageName;
        for (QTextBlock block = previewDocument->begin();
             block.isValid() && imageName.isEmpty(); block = block.next()) {
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat format = it.fragment().charFormat();
                if (format.isImageFormat()) {
                    imageName = format.toImageFormat().name();
                    break;
                }
            }
        }
        QVERIFY2(!imageName.isEmpty(), "the preview holds no image at all");

        const QVariant resource = previewDocument->resource(QTextDocument::ImageResource,
                                                            QUrl(imageName));
        QCOMPARE(qvariant_cast<QImage>(resource).size(), source.size());

        clipboard->clear();
    }

    void refusesAnImageTheAllowListRefuses() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString outside = root.filePath(QStringLiteral("outside.png"));
        const QString inside = root.filePath(QStringLiteral("doc/images/inside.png"));
        QVERIFY(writeImage(outside));
        QVERIFY(writeImage(inside));
        const QString path = root.filePath(QStringLiteral("doc/README.md"));
        QVERIFY(writeFile(path, "# Readme\n"));

        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> window(createMainWindow(engine, component, backend));
        QVERIFY2(window, qPrintable(component.errorString()));
        backend.open(QUrl::fromLocalFile(path));
        QTextDocument *rendered = attachPreview(window.data(), backend);
        QVERIFY(rendered);

        // Every file here exists and is a picture, so what refuses one is the
        // rule and not a missing file. The last is inside the document's
        // folder, which leaves the allow-list as the only thing refusing it.
        struct Case { QString target; bool loads; };
        const QList<Case> cases = {
            {QStringLiteral("images/inside.png"), true},
            {QStringLiteral("https://example.com/logo.png"), false},
            {QStringLiteral("../outside.png"), false},
            {outside, false},
            {QUrl::fromLocalFile(inside).toString(), false},
        };
        for (const Case &test : cases) {
            const QString source = QStringLiteral("![x](") + test.target + QStringLiteral(")\n");
            backend.setPreviewMarkdown(source);
            const QHash<QString, bool> images = imagesIn(rendered);
            QVERIFY2(images.contains(test.target), qPrintable(source));
            QVERIFY2(images.value(test.target) == test.loads, qPrintable(source));
        }
    }

    void saysWhenThePreviewShowsHtmlAsText() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("README.md"));
        QVERIFY(writeFile(path, "# Readme\n"));

        Backend backend;
        QQmlEngine engine;
        QQmlComponent component(&engine);
        QScopedPointer<QObject> window(createMainWindow(engine, component, backend));
        QVERIFY2(window, qPrintable(component.errorString()));
        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(attachPreview(window.data(), backend));

        // Tags written as code, and a less-than in prose, show as text on
        // purpose, so they say nothing.
        backend.setPreviewMarkdown(QStringLiteral(
            "# Readme\n\nIf a < b, write `<br>` for a break.\n\n```html\n<div>shown</div>\n```\n"));
        QVERIFY2(!backend.status().contains(QStringLiteral("HTML")), qPrintable(backend.status()));

        backend.setPreviewMarkdown(QStringLiteral(
            "<h1 align=\"center\">Omawrite</h1>\n\nA Markdown writing app.\n"));
        QVERIFY2(backend.status().contains(QStringLiteral("HTML")), qPrintable(backend.status()));

        // Said once for the document, not again over whatever the status says
        // next on every render while it is being written.
        QVERIFY(!backend.createFolder(QStringLiteral("notes")).isEmpty());
        const QString next = backend.status();
        QVERIFY2(!next.contains(QStringLiteral("HTML")), qPrintable(next));
        backend.setPreviewMarkdown(QStringLiteral(
            "<h1 align=\"center\">Omawrite</h1>\n\nA Markdown writing app, still.\n"));
        QCOMPARE(backend.status(), next);
    }

    void restoresOrderedBuffersAndActiveCaret() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        BufferSession session(stateDirectory.path());
        const QString first = session.createBuffer();
        session.updateBuffer(first, QString(), QStringLiteral("first"), 2, 1, 2, true);
        const QString second = session.createBuffer();
        session.updateBuffer(second, QString(), QStringLiteral("second"), 4, 4, 4, true);
        session.selectBuffer(first);
        QVERIFY(session.saveNow());

        BufferSession restored(stateDirectory.path());
        QVERIFY(restored.restore());
        QCOMPARE(restored.activeBufferId(), first);
        QCOMPARE(restored.buffers().size(), 2);
        QCOMPARE(restored.buffers().at(0).toMap().value(QStringLiteral("text")),
                 QStringLiteral("first"));
        QCOMPARE(restored.buffers().at(0).toMap().value(QStringLiteral("cursorPosition")), 2);
        QCOMPARE(restored.buffers().at(0).toMap().value(QStringLiteral("selectionStart")), 1);
        QCOMPARE(restored.buffers().at(0).toMap().value(QStringLiteral("selectionEnd")), 2);
        QCOMPARE(restored.buffers().at(1).toMap().value(QStringLiteral("text")),
                 QStringLiteral("second"));
    }

    void restoresWorkspaceWindowsAndActiveCarets() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        WorkspaceSession session(stateDirectory.path());
        const QString firstWindow = session.createWindow(10, 20, 900, 700, false);
        const QString firstTab = session.createTab(firstWindow, QUrl(), QStringLiteral("first"),
                                                   2, 1, 2, true);
        const QString secondTab = session.createTab(firstWindow, QUrl(), QStringLiteral("second"),
                                                    4, 4, 4, true);
        QVERIFY(session.setActiveTab(firstWindow, firstTab));

        const QString secondWindow = session.createWindow(30, 40, 800, 600, true);
        const QString thirdTab = session.createTab(secondWindow, QUrl(), QStringLiteral("third"),
                                                   3, 0, 3, false);
        QVERIFY(session.setActiveTab(secondWindow, thirdTab));
        QVERIFY(session.saveNow());

        WorkspaceSession restored(stateDirectory.path());
        QVERIFY(restored.restore());
        const QVariantList windows = restored.windows();
        QCOMPARE(windows.size(), 2);

        const QVariantMap first = windows.at(0).toMap();
        QCOMPARE(first.value(QStringLiteral("x")).toInt(), 10);
        QCOMPARE(first.value(QStringLiteral("activeTabId")).toString(), firstTab);
        const QVariantList firstTabs = first.value(QStringLiteral("tabs")).toList();
        QCOMPARE(firstTabs.size(), 2);
        QCOMPARE(firstTabs.at(0).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("first"));
        QCOMPARE(firstTabs.at(0).toMap().value(QStringLiteral("cursorPosition")).toInt(), 2);
        QCOMPARE(firstTabs.at(1).toMap().value(QStringLiteral("id")).toString(), secondTab);

        const QVariantMap second = windows.at(1).toMap();
        QCOMPARE(second.value(QStringLiteral("maximized")).toBool(), true);
        QCOMPARE(second.value(QStringLiteral("activeTabId")).toString(), thirdTab);
    }

    void keepsLocalFilesUniqueAndMovesTheActiveTab() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        WorkspaceSession session(stateDirectory.path());
        const QString firstWindow = session.createWindow(0, 0, 900, 700, false);
        const QString firstTab = session.createTab(firstWindow,
                                                   QUrl::fromLocalFile(QStringLiteral("/tmp/one.md")),
                                                   QStringLiteral("one"), 0, 0, 0, false);
        const QString secondTab = session.createTab(firstWindow, QUrl(), QStringLiteral("two"),
                                                    0, 0, 0, false);
        const QString secondWindow = session.createWindow(0, 0, 800, 600, false);

        QCOMPARE(session.findOpenLocalFile(QUrl::fromLocalFile(QStringLiteral("/tmp/one.md"))),
                 firstTab);
        QVERIFY(session.createTab(secondWindow,
                                  QUrl::fromLocalFile(QStringLiteral("/tmp/one.md")),
                                  QStringLiteral("other copy"), 0, 0, 0, false).isEmpty());
        QVERIFY(!session.updateTab(secondWindow, secondTab,
                                   QUrl::fromLocalFile(QStringLiteral("/tmp/one.md")),
                                   QStringLiteral("other copy"), 0, 0, 0, false));
        QCOMPARE(session.windowIdForTab(firstTab), firstWindow);

        QVERIFY(session.moveActiveTab(firstWindow, -1));
        const QVariantList tabs = session.windows().constFirst().toMap()
            .value(QStringLiteral("tabs")).toList();
        QCOMPARE(tabs.at(0).toMap().value(QStringLiteral("id")).toString(), secondTab);
        QCOMPARE(session.windows().constFirst().toMap()
                     .value(QStringLiteral("activeTabId")).toString(), secondTab);
    }

    void rejectsWorkspaceSnapshotsWithDuplicateLocalFiles() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        const QJsonObject tab{{QStringLiteral("id"), QStringLiteral("first-tab")},
                              {QStringLiteral("fileUrl"),
                               QUrl::fromLocalFile(QStringLiteral("/tmp/note.md")).toString()},
                              {QStringLiteral("text"), QStringLiteral("text")},
                              {QStringLiteral("cursorPosition"), 0},
                              {QStringLiteral("selectionStart"), 0},
                              {QStringLiteral("selectionEnd"), 0},
                              {QStringLiteral("modified"), false},
                              {QStringLiteral("externalChanged"), false}};
        const QJsonObject duplicateTab{{QStringLiteral("id"), QStringLiteral("second-tab")},
                                       {QStringLiteral("fileUrl"),
                                        QUrl::fromLocalFile(QStringLiteral("/tmp/note.md")).toString()},
                                       {QStringLiteral("text"), QStringLiteral("text")},
                                       {QStringLiteral("cursorPosition"), 0},
                                       {QStringLiteral("selectionStart"), 0},
                                       {QStringLiteral("selectionEnd"), 0},
                                       {QStringLiteral("modified"), false},
                                       {QStringLiteral("externalChanged"), false}};
        const QJsonObject window{{QStringLiteral("id"), QStringLiteral("window")},
                                 {QStringLiteral("x"), 0},
                                 {QStringLiteral("y"), 0},
                                 {QStringLiteral("width"), 900},
                                 {QStringLiteral("height"), 700},
                                 {QStringLiteral("maximized"), false},
                                 {QStringLiteral("activeTabId"), QStringLiteral("first-tab")},
                                 {QStringLiteral("tabs"), QJsonArray{tab, duplicateTab}}};
        QFile file(stateDirectory.filePath(QStringLiteral("session.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 2},
                                              {QStringLiteral("windows"), QJsonArray{window}}})
                       .toJson(QJsonDocument::Compact));
        file.close();

        WorkspaceSession session(stateDirectory.path());
        QVERIFY(!session.restore());
    }

    void removesTheLastTabWithoutCreatingAReplacement() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        WorkspaceSession session(stateDirectory.path());
        const QString window = session.createWindow(0, 0, 900, 700, false);
        const QString tab = session.createTab(window, QUrl(), QStringLiteral("draft"),
                                              0, 0, 0, true);

        QVERIFY(session.removeTab(window, tab));
        const QVariantMap restoredWindow = session.windows().constFirst().toMap();
        QVERIFY(restoredWindow.value(QStringLiteral("tabs")).toList().isEmpty());
        QVERIFY(restoredWindow.value(QStringLiteral("activeTabId")).toString().isEmpty());
    }

    void savesWorkspaceWindowGeometry() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        WorkspaceSession session(stateDirectory.path());
        const QString windowId = session.createWindow(10, 20, 900, 700, false);
        session.createTab(windowId, QUrl(), QString(), 0, 0, 0, false);

        QVERIFY(session.updateWindowGeometry(windowId, 30, 40, 1200, 800, true));
        const QVariantMap window = session.window(windowId);
        QCOMPARE(window.value(QStringLiteral("x")).toInt(), 30);
        QCOMPARE(window.value(QStringLiteral("y")).toInt(), 40);
        QCOMPARE(window.value(QStringLiteral("width")).toInt(), 1200);
        QCOMPARE(window.value(QStringLiteral("height")).toInt(), 800);
        QVERIFY(window.value(QStringLiteral("maximized")).toBool());
    }

    void backendReadsTabsFromItsWorkspaceWindow() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        WorkspaceSession session(stateDirectory.path());
        const QString firstWindow = session.createWindow(0, 0, 900, 700, false);
        const QString firstTab = session.createTab(firstWindow, QUrl(), QStringLiteral("first"),
                                                   2, 1, 2, true);
        const QString secondWindow = session.createWindow(0, 0, 800, 600, false);
        session.createTab(secondWindow, QUrl(), QStringLiteral("second"), 0, 0, 0, false);

        Backend backend(&session, firstWindow);
        QCOMPARE(backend.buffers().size(), 1);
        QCOMPARE(backend.activeBufferId(), firstTab);
        QCOMPARE(backend.activeBufferText(), QStringLiteral("first"));
    }

    void reportsAndSteersTabsForTheCommandLine() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString first = stateDirectory.filePath(QStringLiteral("first.md"));
        const QString second = stateDirectory.filePath(QStringLiteral("second.md"));
        QFile a(first);
        QVERIFY(a.open(QIODevice::WriteOnly | QIODevice::Text));
        a.write("# First\n\nalpha beta");
        a.close();
        QFile b(second);
        QVERIFY(b.open(QIODevice::WriteOnly | QIODevice::Text));
        b.write("# Second\n\ngamma");
        b.close();

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());
        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        Backend *backend = manager.createWindow();
        QVERIFY(backend);

        backend->open(QUrl::fromLocalFile(first));
        backend->newBuffer();
        backend->open(QUrl::fromLocalFile(second));
        QCOMPARE(backend->buffers().size(), 2);

        // The bus face reads the windows themselves, so this is the state a
        // caller of `omawrite --tabs` is given, without a bus in the test.
        Remote remote(&manager);
        const QJsonObject state = QJsonDocument::fromJson(remote.State().toUtf8()).object();
        const QJsonArray windows = state.value(QStringLiteral("windows")).toArray();
        QCOMPARE(windows.size(), 1);
        const QJsonArray tabs = windows.first().toObject().value(QStringLiteral("tabs")).toArray();
        QCOMPARE(tabs.size(), 2);
        QCOMPARE(tabs.at(0).toObject().value(QStringLiteral("index")).toInt(), 1);
        QCOMPARE(tabs.at(0).toObject().value(QStringLiteral("path")).toString(), first);
        QVERIFY(!tabs.at(0).toObject().value(QStringLiteral("active")).toBool());
        QCOMPARE(tabs.at(1).toObject().value(QStringLiteral("path")).toString(), second);
        QVERIFY(tabs.at(1).toObject().value(QStringLiteral("active")).toBool());
        QCOMPARE(tabs.at(1).toObject().value(QStringLiteral("firstLine")).toString(),
                 QStringLiteral("# Second"));

        // A tab is named by the number that was printed, by its path, or by
        // its file name; nothing names the one that is showing.
        QCOMPARE(remote.ReadText(QString()), QStringLiteral("# Second\n\ngamma"));
        QCOMPARE(remote.ReadText(QStringLiteral("1")), QStringLiteral("# First\n\nalpha beta"));
        QCOMPARE(remote.ReadText(first), QStringLiteral("# First\n\nalpha beta"));
        QCOMPARE(remote.ReadText(QStringLiteral("first.md")),
                 QStringLiteral("# First\n\nalpha beta"));
        QCOMPARE(remote.ReadText(QStringLiteral("nothing.md")), QString());

        // And selecting one moves the editor onto it, which is the whole point
        // of being able to name it.
        QVERIFY(remote.SelectTab(QStringLiteral("first.md")));
        QTRY_COMPARE(backend->currentDocumentText(), QStringLiteral("# First\n\nalpha beta"));
        QCOMPARE(remote.ReadText(QString()), QStringLiteral("# First\n\nalpha beta"));
        QVERIFY(!remote.SelectTab(QStringLiteral("nothing.md")));
    }

    void windowManagerCreatesIndependentWritingWindows() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));

        Backend *first = manager.createWindow();
        QVERIFY(first);
        first->newWindow();

        QTRY_COMPARE(manager.windowCount(), 2);
        QCOMPARE(session.windows().size(), 2);
    }

    // A start that turns out to be the second one hands its launch to the
    // Omawrite already running and goes. Its windows go with it, records and
    // all: a record left behind is a window nothing is showing, which is what
    // swallowed an open.
    void windowManagerAbandonsTheWindowsItBuilt() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY(manager.createWindow());
        QVERIFY(manager.createWindow());
        QCOMPARE(manager.windowCount(), 2);
        QCOMPARE(session.windows().size(), 2);

        manager.abandonWindows();

        QCOMPARE(manager.windowCount(), 0);
        QVERIFY(session.windows().isEmpty());

        // And the session file says so too, so the next start reads none.
        WorkspaceSession reread(stateDirectory.path());
        reread.restore();
        QVERIFY(reread.windows().isEmpty());
    }

    void windowManagerClosesTheLastTabWithItsWindow() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        Backend *backend = manager.createWindow();
        QVERIFY(backend);

        QVERIFY(backend->discardActiveBuffer());
        QTRY_COMPARE(manager.windowCount(), 0);
        QVERIFY(session.windows().isEmpty());
    }

    void windowManagerActivatesAnExistingFileTab() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString filePath = stateDirectory.filePath(QStringLiteral("note.md"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("note");
        file.close();

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        Backend *first = manager.createWindow();
        Backend *second = manager.createWindow();
        QVERIFY(first);
        QVERIFY(second);

        first->open(QUrl::fromLocalFile(filePath));
        second->open(QUrl::fromLocalFile(filePath));

        // Opening takes over the tab that is showing rather than adding one, so
        // the first window still holds the one tab it started with. The second
        // window finds the file already open and brings that tab forward
        // instead of taking a second copy of the same document.
        QVERIFY(!session.findOpenLocalFile(QUrl::fromLocalFile(filePath)).isEmpty());
        QCOMPARE(session.windows().at(0).toMap().value(QStringLiteral("tabs")).toList().size(), 1);
        QCOMPARE(session.windows().at(1).toMap().value(QStringLiteral("tabs")).toList().size(), 1);
    }

    void windowManagerMarksBackgroundExternalChangesWithoutPrompting() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString filePath = stateDirectory.filePath(QStringLiteral("note.md"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("first");
        file.close();

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        Backend *backend = manager.createWindow();
        QVERIFY(backend);
        backend->open(QUrl::fromLocalFile(filePath));
        const QString fileTab = backend->activeBufferId();
        backend->newBuffer();
        QSignalSpy externalChangeSpy(backend, &Backend::externalChangeDetected);

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("second");
        file.close();

        // Marked, not announced: a change to a tab you are not looking at waits
        // until you get there.
        QTRY_VERIFY(session.tab(fileTab).value(QStringLiteral("externalChanged")).toBool());
        QCOMPARE(externalChangeSpy.count(), 0);

        // And when you do get there, with nothing of yours at stake, it is
        // taken rather than asked about, and the mark comes off.
        QVERIFY(backend->selectBuffer(fileTab));
        QTRY_VERIFY(!session.tab(fileTab).value(QStringLiteral("externalChanged")).toBool());
        QCOMPARE(session.tab(fileTab).value(QStringLiteral("text")).toString(),
                 QStringLiteral("second"));
        QCOMPARE(externalChangeSpy.count(), 0);
    }

    void windowManagerRestoresWritingWindows() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());
        const QString firstWindow = session.createWindow(10, 20, 900, 700, false);
        session.createTab(firstWindow, QUrl(), QStringLiteral("first"), 0, 0, 0, true);
        const QString secondWindow = session.createWindow(30, 40, 1200, 800, true);
        session.createTab(secondWindow, QUrl(), QStringLiteral("second"), 0, 0, 0, true);

        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));

        QCOMPARE(manager.restoreWindows(), 2);
        QCOMPARE(manager.windowCount(), 2);
    }

    void windowManagerRecoversLegacySnapshotsAsUnsavedTabs() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        const QString recoveryPath = stateDirectory.filePath(QStringLiteral("recovery-0.json"));
        QFile recovery(recoveryPath);
        QVERIFY(recovery.open(QIODevice::WriteOnly));
        recovery.write(QJsonDocument(QJsonObject{{QStringLiteral("fileUrl"), QString()},
                                                  {QStringLiteral("text"), QStringLiteral("draft")}})
                           .toJson(QJsonDocument::Compact));
        recovery.close();

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());
        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY(manager.createWindow());

        QCOMPARE(manager.recoverLegacySnapshots(), 1);
        QCOMPARE(session.windows().constFirst().toMap().value(QStringLiteral("tabs")).toList().size(), 2);
        QVERIFY(!QFile::exists(recoveryPath));
    }

    void preservesBufferTextWhenUpdatingCaret() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        BufferSession session(stateDirectory.path());
        const QString id = session.createBuffer();
        QVERIFY(session.updateBuffer(id, QString(), QStringLiteral("first"), 0, 0, 0, true));
        QVERIFY(session.updateBufferCursor(id, 3, 1, 3));
        QVERIFY(session.saveNow());

        BufferSession restored(stateDirectory.path());
        QVERIFY(restored.restore());
        const QVariantMap buffer = restored.buffers().constFirst().toMap();
        QCOMPARE(buffer.value(QStringLiteral("text")), QStringLiteral("first"));
        QCOMPARE(buffer.value(QStringLiteral("cursorPosition")), 3);
        QCOMPARE(buffer.value(QStringLiteral("selectionStart")), 1);
        QCOMPARE(buffer.value(QStringLiteral("selectionEnd")), 3);
    }

    void activatesExistingBufferForSameFile() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        BufferSession session(stateDirectory.path());
        const QUrl fileUrl = QUrl::fromLocalFile(QStringLiteral("/tmp/note.md"));
        const QString first = session.openBuffer(fileUrl, QStringLiteral("first"));
        const QString second = session.openBuffer(fileUrl, QStringLiteral("second"));

        QCOMPARE(second, first);
        QCOMPARE(session.buffers().size(), 1);
        QCOMPARE(session.buffers().constFirst().toMap().value(QStringLiteral("text")),
                 QStringLiteral("first"));
    }

    void sessionSnapshotsDoNotModifyUserFiles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("note.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("original"), qint64(8));
        file.close();

        BufferSession session(directory.filePath(QStringLiteral("state")));
        const QString id = session.openBuffer(QUrl::fromLocalFile(path), QStringLiteral("edited"));
        session.updateBuffer(id, QUrl::fromLocalFile(path).toString(), QStringLiteral("edited"),
                             6, 6, 6, true);
        QVERIFY(session.saveNow());

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("original"));
    }

    void backendCreatesAndSelectsBuffers() {
        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());

        QCOMPARE(backend.buffers().size(), 1);
        const QString first = backend.activeBufferId();
        const QString second = backend.newBuffer();
        QCOMPARE(backend.buffers().size(), 2);
        QCOMPARE(backend.activeBufferId(), second);
        QVERIFY(backend.selectBuffer(first));
        QCOMPARE(backend.activeBufferId(), first);
    }

    void derivesBufferTitles() {
        Backend backend;

        QCOMPARE(backend.bufferTitle({{QStringLiteral("fileUrl"), QString()},
                                     {QStringLiteral("text"), QString()}}, 0),
                 QStringLiteral("Untitled 1"));
        QCOMPARE(backend.bufferTitle({{QStringLiteral("fileUrl"), QString()},
                                     {QStringLiteral("text"), QStringLiteral("  Draft title\nBody")}}, 1),
                 QStringLiteral("Draft title"));
        QCOMPARE(backend.bufferTitle({{QStringLiteral("fileUrl"), QString()},
                                     {QStringLiteral("text"), QString(40, QChar('a'))}}, 2),
                 QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaa…"));
        QCOMPARE(backend.bufferTitle({{QStringLiteral("fileUrl"),
                                      QStringLiteral("file:///tmp/notes.md")},
                                     {QStringLiteral("text"), QStringLiteral("Ignored")}}, 3),
                 QStringLiteral("notes.md"));
    }

    void scrollsOverflowingTabs() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        for (int index = 0; index < 12; ++index)
            backend.newBuffer();

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        window->setProperty("width", 280);

        QObject *tabFlick = window->findChild<QObject *>(QStringLiteral("tabFlick"));
        QVERIFY(tabFlick);
        QTRY_VERIFY(tabFlick->property("contentWidth").toReal()
                     > tabFlick->property("width").toReal());
        QVERIFY(QMetaObject::invokeMethod(window.get(), "scrollTabs", Q_ARG(QVariant, 1)));
        QTRY_VERIFY(tabFlick->property("contentX").toReal() > 0);
    }

    void hidesTabBarForSingleBuffer() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *tabBar = window->findChild<QObject *>(QStringLiteral("tabBar"));
        QVERIFY(tabBar);
        QVERIFY(!tabBar->property("visible").toBool());
    }

    void cyclesTabsFromWindowShortcuts() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        const QString first = backend.activeBufferId();
        const QString second = backend.newBuffer();
        const QString third = backend.newBuffer();

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(QMetaObject::invokeMethod(window.get(), "selectAdjacentTab", Q_ARG(QVariant, 1)));
        QCOMPARE(backend.activeBufferId(), first);
        QVERIFY(QMetaObject::invokeMethod(window.get(), "selectAdjacentTab", Q_ARG(QVariant, -1)));
        QCOMPARE(backend.activeBufferId(), third);
        QVERIFY(backend.selectBuffer(second));
        QVERIFY(QMetaObject::invokeMethod(window.get(), "selectAdjacentTab", Q_ARG(QVariant, 1)));
        QCOMPARE(backend.activeBufferId(), third);
    }

    void cyclesToHiddenTabsAndScrollsThemIntoView() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        for (int index = 0; index < 12; ++index)
            backend.newBuffer();

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        window->setProperty("width", 280);

        QObject *tabFlick = window->findChild<QObject *>(QStringLiteral("tabFlick"));
        QVERIFY(tabFlick);
        QTRY_VERIFY(tabFlick->property("contentWidth").toReal()
                     > tabFlick->property("width").toReal());
        QCOMPARE(tabFlick->property("contentX").toReal(), 0.0);

        QVERIFY(QMetaObject::invokeMethod(window.get(), "selectAdjacentTab", Q_ARG(QVariant, -1)));
        QTRY_VERIFY(tabFlick->property("contentX").toReal() > 0);
    }

    void scopesDocumentShortcutsToTheirWindow() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        for (const QString &name : {QStringLiteral("newTabShortcut"),
                                    QStringLiteral("closeTabShortcut"),
                                    QStringLiteral("nextTabShortcut"),
                                    QStringLiteral("previousTabShortcut"),
                                    QStringLiteral("moveTabLeftShortcut"),
                                    QStringLiteral("moveTabRightShortcut"),
                                    QStringLiteral("newWindowShortcut")}) {
            QObject *shortcut = window->findChild<QObject *>(name);
            QVERIFY(shortcut);
            QCOMPARE(shortcut->property("context").toInt(), static_cast<int>(Qt::WindowShortcut));
        }
    }

    void restoresActiveTextAndCaretThroughQmlLifecycle() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());

        {
            Backend writer(stateDirectory.path());
            QQmlEngine engine;
            engine.rootContext()->setContextProperty(QStringLiteral("backend"), &writer);
            engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                      new AgentSession(&engine));
            QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
            QVERIFY2(component.isReady(), qPrintable(component.errorString()));
            QScopedPointer<QObject> window(component.create());
            QVERIFY2(window, qPrintable(component.errorString()));

            QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
            QVERIFY(editor);
            QTest::qWait(120);
            QTRY_VERIFY(!writer.buffers().isEmpty());
            editor->setProperty("text", QStringLiteral("First tab"));
            editor->setProperty("cursorPosition", 5);
            QTRY_COMPARE(writer.buffers().constFirst().toMap().value(QStringLiteral("text")),
                         QStringLiteral("First tab"));
            QTRY_COMPARE(writer.buffers().constFirst().toMap()
                             .value(QStringLiteral("cursorPosition")).toInt(),
                         5);
            writer.prepareForApplicationClose();
        }

        BufferSession persisted(stateDirectory.path());
        QVERIFY(persisted.restore());
        QCOMPARE(persisted.buffers().constFirst().toMap()
                     .value(QStringLiteral("cursorPosition")).toInt(),
                 5);

        Backend reader(stateDirectory.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &reader);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QTRY_COMPARE(editor->property("text").toString(), QStringLiteral("First tab"));
        QTRY_COMPARE(reader.activeCursorPosition(), 5);
        QTRY_COMPARE(editor->property("cursorPosition").toInt(), 5);
    }

    void dispatchesEditorFontSizeShortcuts() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));

        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        QCOMPARE(backend.editorFontSize(), 12);
        QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 14);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 16);
        QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 14);
        QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(backend.editorFontSize(), 12);
        QCOMPARE(QSettings().value(QStringLiteral("editor/fontSize")).toInt(), 12);
    }

    void persistsEditorFontSizeAcrossBackendInstances() {
        {
            Backend backend;
            QCOMPARE(backend.editorFontSize(), 12);

            QSignalSpy changedSpy(&backend, &Backend::editorFontSizeChanged);
            backend.setEditorFontSize(28);
            QCOMPARE(changedSpy.count(), 1);
            QCOMPARE(QSettings().value(QStringLiteral("editor/fontSize")).toInt(), 28);
        }

        Backend restoredBackend;
        QCOMPARE(restoredBackend.editorFontSize(), 28);

        restoredBackend.setEditorFontSize(100);
        QCOMPARE(restoredBackend.editorFontSize(), 48);
        restoredBackend.setEditorFontSize(0);
        QCOMPARE(restoredBackend.editorFontSize(), 10);
    }

    void togglesFocusMode() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("focus-test.md"));

        Backend backend;
        QSignalSpy focusSpy(&backend, &Backend::focusModeChanged);
        QVERIFY(!backend.focusMode());

        backend.saveAs(QUrl::fromLocalFile(path));
        QCOMPARE(backend.status(), QStringLiteral("Saved focus-test.md"));

        backend.toggleFocusMode();
        QVERIFY(backend.focusMode());
        QCOMPARE(focusSpy.count(), 1);
        QCOMPARE(backend.status(), QStringLiteral("Saved focus-test.md"));

        backend.toggleFocusMode();
        QVERIFY(!backend.focusMode());
        QCOMPARE(focusSpy.count(), 2);
        QCOMPARE(backend.status(), QStringLiteral("Saved focus-test.md"));
    }

    void focusModeDimsInactiveBlocks() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("First paragraph\n\nSecond paragraph"));
        MarkdownHighlighter highlighter(&doc);
        highlighter.setColors(QStringLiteral("#101010"), QStringLiteral("#eeeeee"),
                              QStringLiteral("#5584aa"));

        // Enable focus mode with cursor in the first block
        highlighter.setFocusCursorPosition(0);
        highlighter.setFocusMode(true);

        // First block (active) should keep its original undimmed foreground
        QTextBlock firstBlock = doc.findBlockByNumber(0);
        bool firstIsDimmed = !firstBlock.layout()->formats().isEmpty()
            && firstBlock.layout()->formats().first().format.foreground().color() != QColor(QStringLiteral("#eeeeee"));
        QVERIFY(!firstIsDimmed);

        // Third block (inactive, "Second paragraph") should be dimmed
        QTextBlock thirdBlock = doc.findBlockByNumber(2);
        QVERIFY(!thirdBlock.layout()->formats().isEmpty());
        QColor dimmedColor = thirdBlock.layout()->formats().first().format.foreground().color();
        QVERIFY(dimmedColor.isValid());
        QVERIFY(dimmedColor != QColor(QStringLiteral("#eeeeee")));
        QVERIFY(dimmedColor != QColor(QStringLiteral("#101010")));

        // Move cursor to third block — first should dim, third should un-dim
        highlighter.setFocusCursorPosition(thirdBlock.position());
        QTextBlock updatedFirst = doc.findBlockByNumber(0);
        QVERIFY(!updatedFirst.layout()->formats().isEmpty());
        QColor nowDimmed = updatedFirst.layout()->formats().first().format.foreground().color();
        QCOMPARE(nowDimmed, dimmedColor);

        QTextBlock updatedThird = doc.findBlockByNumber(2);
        bool thirdIsDimmed = !updatedThird.layout()->formats().isEmpty()
            && updatedThird.layout()->formats().first().format.foreground().color() == dimmedColor;
        QVERIFY(!thirdIsDimmed);
    }

    void focusModeKeepsInlineMarkersHidden() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("active line\n\nsome **bold** here"));
        MarkdownHighlighter highlighter(&doc);
        highlighter.setColors(QStringLiteral("#101010"), QStringLiteral("#eeeeee"),
                              QStringLiteral("#5584aa"));

        highlighter.setFocusCursorPosition(0);
        highlighter.setFocusMode(true);

        const QTextBlock dimmed = doc.findBlockByNumber(2);
        QVERIFY(!dimmed.layout()->formats().isEmpty());
        QColor markerColor;
        QColor textColor;
        for (const QTextLayout::FormatRange &range : dimmed.layout()->formats()) {
            const QString run = dimmed.text().mid(range.start, range.length);
            if (run == QStringLiteral("**"))
                markerColor = range.format.foreground().color();
            else if (run == QStringLiteral("bold"))
                textColor = range.format.foreground().color();
        }

        QCOMPARE(markerColor, QColor(QStringLiteral("#101010")));
        QVERIFY(textColor.isValid());
        QVERIFY(textColor != markerColor);
    }

    void recentresOnlyWhenFocusModeMovesTheEditor() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QObject *viewport = editor->parent();
        while (viewport && !viewport->property("contentY").isValid())
            viewport = viewport->parent();
        QVERIFY(viewport);

        QString text;
        for (int line = 0; line < 600; ++line)
            text += QStringLiteral("line %1 of the document\n").arg(line);
        editor->setProperty("text", text);
        const int caretPosition = text.indexOf(QStringLiteral("line 150 "));
        QVERIFY(caretPosition > 0);
        editor->setProperty("cursorPosition", caretPosition);
        QTest::qWait(50);

        // Reading elsewhere, with the caret scrolled out of sight: resizing
        // the window has to leave the page where the reader put it.
        // Somewhere below the caret, and inside what the document can
        // actually scroll: a Flickable clamps a contentY past its end, and a
        // resize would then re-clamp it and look like the page moved.
        const qreal furthest = viewport->property("contentHeight").toReal()
            - viewport->property("height").toReal() - 100;
        QVERIFY2(furthest > 0, "the document is too short to scroll");
        const qreal readingAt = qMin(editor->property("y").toReal()
                                         + editor->property("cursorRectangle").toRectF().y()
                                         + viewport->property("height").toReal() + 200,
                                     furthest);
        viewport->setProperty("contentY", readingAt);
        QCOMPARE(viewport->property("contentY").toReal(), readingAt);
        const qreal viewportBefore = viewport->property("height").toReal();
        window->setProperty("height", window->property("height").toReal() + 80);
        QTest::qWait(50);
        // A resize the layout never receives leaves the page in place for the
        // wrong reason, and everything below it would then pass on anything.
        QVERIFY2(qAbs(viewport->property("height").toReal() - (viewportBefore + 80)) <= 2,
                 qPrintable(QStringLiteral("the resize never reached the editor: viewport %1, "
                                           "expected %2. Run this through bin/test, which "
                                           "forces QT_QPA_PLATFORM=offscreen.")
                                .arg(viewport->property("height").toReal())
                                .arg(viewportBefore + 80)));
        const qreal stillReadingAt = viewport->property("contentY").toReal();
        QVERIFY2(qAbs(stillReadingAt - readingAt) <= 2,
                 qPrintable(QStringLiteral("the page moved from %1 to %2")
                                .arg(readingAt)
                                .arg(stillReadingAt)));

        // Entering focus mode does have to move it: the caret line goes to
        // the middle of the viewport straight away, not at the next keystroke.
        backend.updateCursorPosition(caretPosition);
        backend.toggleFocusMode();
        QTest::qWait(50);
        const QRectF caret = editor->property("cursorRectangle").toRectF();
        const qreal viewportHeight = viewport->property("height").toReal();
        const qreal caretCentre = editor->property("y").toReal() + caret.y()
            + caret.height() / 2 - viewport->property("contentY").toReal();
        QVERIFY2(qAbs(caretCentre - viewportHeight / 2) <= 2,
                 qPrintable(QStringLiteral("caret centre %1 in a viewport of %2")
                                .arg(caretCentre)
                                .arg(viewportHeight)));
    }

    void answersHelpBeforeOpeningAWindow() {
        const QString usage = Cli::usage();
        QVERIFY(usage.contains(QStringLiteral("omawrite [FILE]")));
        QVERIFY(usage.contains(QStringLiteral("-h, --help")));

        QCOMPARE(Cli::handleArguments({QStringLiteral("omawrite"), QStringLiteral("--help")}),
                 std::optional<int>(0));
        QCOMPARE(Cli::handleArguments({QStringLiteral("omawrite"), QStringLiteral("-h")}),
                 std::optional<int>(0));
        QCOMPARE(Cli::handleArguments({QStringLiteral("omawrite"), QStringLiteral("--nope")}),
                 std::optional<int>(1));
        QCOMPARE(Cli::handleArguments({QStringLiteral("omawrite")}), std::nullopt);
        QCOMPARE(Cli::handleArguments({QStringLiteral("omawrite"), QStringLiteral("draft.md")}),
                 std::nullopt);
    }

    void startsANewFileFromAPathThatIsNotThereYet() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString newPath = directory.filePath(QStringLiteral("new_document.md"));
        const QString existingPath = directory.filePath(QStringLiteral("already-there.md"));
        QFile existing(existingPath);
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Text));
        existing.write("on disk already");
        existing.close();

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        // A name from the command line that is not on disk yet is still this
        // document's name, blank as the document is.
        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        backend.open(QUrl::fromLocalFile(newPath));
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(newPath));
        QCOMPARE(backend.fileName(), QStringLiteral("new_document.md"));
        QCOMPARE(backend.status(), QStringLiteral("New file new_document.md"));
        QCOMPARE(editor->property("text").toString(), QString());
        QVERIFY(!backend.modified());

        // Opening it wrote nothing: the file appears when the writer saves.
        QVERIFY(!QFileInfo::exists(newPath));

        editor->setProperty("text", QStringLiteral("first words"));
        QVERIFY(backend.modified());
        backend.save();
        QCOMPARE(saveDialogSpy.count(), 0);
        QVERIFY(!backend.modified());

        QFile written(newPath);
        QVERIFY(written.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(written.readAll(), QByteArray("first words"));
        written.close();

        // A file that is there still opens and reads.
        backend.open(QUrl::fromLocalFile(existingPath));
        QCOMPARE(backend.fileName(), QStringLiteral("already-there.md"));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("on disk already"));

        // A path that is there but cannot be read is still an error, and
        // leaves the document it could not replace alone.
        backend.open(QUrl::fromLocalFile(directory.path()));
        QCOMPARE(backend.status(),
                 QStringLiteral("Could not open %1.")
                     .arg(QFileInfo(directory.path()).fileName()));
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(existingPath));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("on disk already"));

        // A name under a directory that is not there is not a file anyone can
        // start, so it stays an error rather than a document that cannot save.
        backend.open(QUrl::fromLocalFile(
            directory.filePath(QStringLiteral("not-there/child.md"))));
        QCOMPARE(backend.status(), QStringLiteral("Could not open child.md."));
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(existingPath));
        QCOMPARE(editor->property("text").toString(), QStringLiteral("on disk already"));
    }

    void asksBeforeAFirstSaveReplacesAFileThatAppeared() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("arriving.md"));

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        QCOMPARE(backend.status(), QStringLiteral("New file arriving.md"));
        editor->setProperty("text", QStringLiteral("my draft"));
        QVERIFY(backend.modified());

        // A file that is not there cannot be watched, so nothing tells us when
        // a `git pull` or a sync client puts one on that path. The first save
        // is the first look, and it must not replace a file it has never read.
        QFile arrived(path);
        QVERIFY(arrived.open(QIODevice::WriteOnly | QIODevice::Text));
        arrived.write("arrived from elsewhere");
        arrived.close();

        QSignalSpy appearedSpy(&backend, &Backend::externalFileAppeared);
        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        backend.save();
        QCOMPARE(appearedSpy.count(), 1);
        QCOMPARE(appearedSpy.takeFirst().constFirst().toBool(), true);

        // Asked, not answered: the file on disk is whole and the draft is
        // still unsaved. The name is not in question, so no Save As dialog.
        QCOMPARE(saveDialogSpy.count(), 0);
        QVERIFY(backend.modified());
        QFile untouched(path);
        QVERIFY(untouched.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(untouched.readAll(), QByteArray("arrived from elsewhere"));
        untouched.close();

        // Keeping your version is what the dialog offers, and the save that
        // follows it goes through: the guard asks once, it does not lock the
        // writer out of the name they gave.
        backend.keepExternalVersion();
        backend.save();
        QVERIFY(!backend.modified());
        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(written.readAll(), QByteArray("my draft"));
        written.close();
    }

    void asksOnlyOnceWhenWhatAppearedCannotBeRead() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("blocked.md"));

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("my draft"));
        QVERIFY(backend.modified());

        // What turns up on the path need not be a readable file. A directory
        // is the plainest case: keepExternalVersion() cannot read it, so it
        // has no contents to remember afterwards.
        QVERIFY(QDir().mkpath(path));

        QSignalSpy appearedSpy(&backend, &Backend::externalFileAppeared);
        backend.save();
        QCOMPARE(appearedSpy.count(), 1);

        // Keeping your version answers the question, and an answer that could
        // not be read is still an answer. Asking again would put the writer in
        // a dialog with no way out of it, every Ctrl+S for the rest of the
        // session. The second save goes to the filesystem and reports what the
        // filesystem says, which is the only thing that can end this.
        backend.keepExternalVersion();
        QCOMPARE(backend.status(), QStringLiteral("Kept your version"));
        backend.save();
        QCOMPARE(appearedSpy.count(), 1);
        QCOMPARE(backend.status(), QStringLiteral("Could not save blocked.md."));
    }

    // Dropped from omacom/omawrite#13: the unsaved-changes dialog it drives was removed with #22, and autosave
    // answers the question it used to ask.


    void putsKeepMineForwardWhenAFileAppeared() {
        const QString dialogPath = QFINDTESTDATA("../src/ExternalChangeDialog.qml");
        QVERIFY(!dialogPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(dialogPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> dialog(component.create());
        QVERIFY2(dialog, qPrintable(component.errorString()));

        QObject *keep = dialog->findChild<QObject *>(QStringLiteral("keepMineButton"));
        QObject *reload = dialog->findChild<QObject *>(QStringLiteral("reloadButton"));
        QObject *message = dialog->findChild<QObject *>(QStringLiteral("externalChangeMessage"));
        QObject *heading = dialog->findChild<QObject *>(QStringLiteral("externalChangeHeading"));
        QVERIFY(keep);
        QVERIFY(reload);
        QVERIFY(message);
        QVERIFY(heading);

        // For an ordinary outside edit the file on disk is a second copy of
        // the work, so Reload is the safe answer and leads, as it always has.
        QVERIFY(!dialog->property("keepIsSafer").toBool());
        QVERIFY(reload->property("primary").toBool());
        QVERIFY(!keep->property("primary").toBool());

        // For a file that appeared there is no second copy: every word the
        // writer has is in the editor, and reloading throws all of it away,
        // recovery snapshot included. The button that does that must not be
        // the one Enter presses, and the text must say what is at stake.
        dialog->setProperty("appeared", true);
        QVERIFY(dialog->property("keepIsSafer").toBool());
        QVERIFY(keep->property("primary").toBool());
        QVERIFY(!reload->property("primary").toBool());
        QCOMPARE(heading->property("text").toString(), QStringLiteral("File appeared"));
        const QString message_ = message->property("text").toString();
        QVERIFY2(message_.contains(QStringLiteral("created this file")), qPrintable(message_));
        QVERIFY2(message_.contains(QStringLiteral("discard everything")), qPrintable(message_));
    }

    // Dropped from omacom/omawrite#13: autosave writes the file back within the second, so the window this race
    // needed -- a deleted path staying deleted until the next manual save -- is
    // one autosave closes.


    void writesTheNeverReadPathIntoTheSnapshot() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());
        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("fresh.md"));

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));

        // Autosave writes the file itself, so a name that can be written never
        // reaches a draft at all. Take the folder away once the name is taken,
        // and the autosave falls back to the draft this test is about.
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner
                                                        | QFileDevice::ExeOwner));
        editor->setProperty("text", QStringLiteral("words only I have"));
        QVERIFY(backend.modified());

        // The snapshot the next run reads is the one this run wrote, so the
        // flag has to survive the write as well as the read. Hand-writing the
        // JSON proves only half of that, and it is the half that cannot lose
        // a file.
        const QString snapshotPath =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("recovery-0.json"));
        QTRY_VERIFY(QFile::exists(snapshotPath));
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner
                                                        | QFileDevice::WriteOwner
                                                        | QFileDevice::ExeOwner));

        QFile snapshot(snapshotPath);
        QVERIFY(snapshot.open(QIODevice::ReadOnly));
        const QJsonObject recovery = QJsonDocument::fromJson(snapshot.readAll()).object();
        snapshot.close();
        QVERIFY(recovery.contains(QStringLiteral("pathNeverRead")));
        QVERIFY(recovery.value(QStringLiteral("pathNeverRead")).toBool());
    }

    void remembersANeverReadPathAcrossRecovery() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());
        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("fresh.md"));

        // The snapshot a crash leaves behind, for a new file whose first save
        // never happened.
        const QString stateDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QVERIFY(QDir().mkpath(stateDirectory));
        QFile snapshot(QDir(stateDirectory).filePath(QStringLiteral("recovery-0.json")));
        QVERIFY(snapshot.open(QIODevice::WriteOnly));
        const QJsonObject recovery{
            {QStringLiteral("fileUrl"), QUrl::fromLocalFile(path).toString()},
            {QStringLiteral("pathNeverRead"), true},
            {QStringLiteral("text"), QStringLiteral("words only I have")}};
        snapshot.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
        snapshot.close();

        // A file turns up on the path while Omawrite is not running to see it.
        QFile arrived(path);
        QVERIFY(arrived.open(QIODevice::WriteOnly | QIODevice::Text));
        arrived.write("arrived while we were down");
        arrived.close();

        // Reading it back on restore says what is on the path now, which is
        // not the same as this document having read it. Without the flag the
        // first save takes the guard's silence for permission.
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QCOMPARE(backend.status(), QStringLiteral("Recovered unsaved changes"));
        QSignalSpy appearedSpy(&backend, &Backend::externalFileAppeared);
        backend.save();
        QCOMPARE(appearedSpy.count(), 1);
        QFile untouched(path);
        QVERIFY(untouched.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(untouched.readAll(), QByteArray("arrived while we were down"));
        untouched.close();
    }

    // What the Omawrite already running is asked to do with a launch. Keying
    // this on the kind rather than on the file dropped the file: `omawrite
    // FILE` is Run with a path, not Open, and that is what a desktop entry's
    // `Exec=omawrite %f` sends -- so a click in a file manager raised the
    // window and left the document alone.
    void handsOverAFileHoweverItWasNamed() {
        const auto handover = [](const QStringList &words) {
            return Cli::handoverFor(Cli::parse(words));
        };
        const QStringList omawrite{QStringLiteral("omawrite")};

        // What a file manager sends.
        QCOMPARE(handover(omawrite + QStringList{QStringLiteral("draft.md")}),
                 Cli::Handover::Open);
        // And what --open sends, which took this path all along.
        QCOMPARE(handover(omawrite + QStringList{QStringLiteral("--open"),
                                                 QStringLiteral("draft.md")}),
                 Cli::Handover::Open);
        QCOMPARE(handover(omawrite + QStringList{QStringLiteral("--open"),
                                                 QStringLiteral("draft.md:12")}),
                 Cli::Handover::Open);

        // A launcher entry with nothing named asks for Omawrite, and the one
        // already running is Omawrite.
        QCOMPARE(handover(omawrite), Cli::Handover::Present);

        // The reading calls are answered before a window is built.
        QCOMPARE(handover(omawrite + QStringList{QStringLiteral("--tabs")}),
                 Cli::Handover::None);
        QCOMPARE(handover(omawrite + QStringList{QStringLiteral("--help")}),
                 Cli::Handover::None);
    }

    void parsesTheCommandLine() {
        using Request = Cli::Request;
        const auto parse = [](const QStringList &words) { return Cli::parse(words); };

        QCOMPARE(parse({QStringLiteral("omawrite")}).kind, Request::Run);
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("draft.md")}).path,
                 QStringLiteral("draft.md"));
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--list-tabs")}).kind,
                 Request::ListTabs);

        const Request opened = parse({QStringLiteral("omawrite"), QStringLiteral("--open"),
                                      QStringLiteral("notes.md:12")});
        QCOMPARE(opened.kind, Request::Open);
        QCOMPARE(opened.path, QStringLiteral("notes.md"));
        QCOMPARE(opened.line, 12);

        // A colon is legal in a filename, so only a trailing run of digits is a
        // line number: guessing wrong would open a file nobody named.
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--open"),
                        QStringLiteral("10:30 standup.md")}).path,
                 QStringLiteral("10:30 standup.md"));
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--open"),
                        QStringLiteral("notes.md:")}).path,
                 QStringLiteral("notes.md:"));
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--open"),
                        QStringLiteral("notes.md:0")}).line, 0);

        // The reading and steering calls. --read takes an optional tab and
        // --select a required one, and neither may swallow the flag after it.
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--tabs")}).kind,
                 Request::Tabs);
        QVERIFY(!parse({QStringLiteral("omawrite"), QStringLiteral("--tabs")}).json);
        QVERIFY(parse({QStringLiteral("omawrite"), QStringLiteral("--tabs"),
                       QStringLiteral("--json")}).json);

        const Request read = parse({QStringLiteral("omawrite"), QStringLiteral("--read"),
                                    QStringLiteral("notes.md")});
        QCOMPARE(read.kind, Request::Read);
        QCOMPARE(read.path, QStringLiteral("notes.md"));
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--read")}).path, QString());
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--read"),
                        QStringLiteral("--json")}).path, QString());

        const Request selected = parse({QStringLiteral("omawrite"), QStringLiteral("--select"),
                                        QStringLiteral("2")});
        QCOMPARE(selected.kind, Request::Select);
        QCOMPARE(selected.path, QStringLiteral("2"));

        // --select with nothing to select is an error, not a silent no-op.
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--select")}).kind,
                 Request::Error);

        // --append takes the name whole: its text is on stdin, so a line number
        // would have nothing to mean.
        const Request appended = parse({QStringLiteral("omawrite"), QStringLiteral("--append"),
                                        QStringLiteral("log.md:9")});
        QCOMPARE(appended.kind, Request::Append);
        QCOMPARE(appended.path, QStringLiteral("log.md:9"));

        // --tab is a modifier, not a mode, so it reads the same on either side
        // of the file it applies to.
        QVERIFY(parse({QStringLiteral("omawrite"), QStringLiteral("--open"),
                       QStringLiteral("notes.md"), QStringLiteral("--tab")}).newTab);
        QVERIFY(parse({QStringLiteral("omawrite"), QStringLiteral("--tab"),
                       QStringLiteral("--open"), QStringLiteral("notes.md")}).newTab);
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--tab"),
                        QStringLiteral("--open"), QStringLiteral("notes.md:4")}).line, 4);
        QVERIFY(!parse({QStringLiteral("omawrite"), QStringLiteral("--open"),
                        QStringLiteral("notes.md")}).newTab);
        QVERIFY(parse({QStringLiteral("omawrite"), QStringLiteral("draft.md"),
                       QStringLiteral("--tab")}).newTab);

        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--open")}).kind,
                 Request::Error);
        QCOMPARE(parse({QStringLiteral("omawrite"), QStringLiteral("--nope")}).exitCode, 1);
    }

    void appendsToAFileWithoutRunningTheWindow() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("log.md"));

        // A file with no closing newline would otherwise have the note run on
        // from its last line, which is never what appending means.
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
        seed.write("first line");
        seed.close();

        const auto append = [&path](const QByteArray &text) {
            QTemporaryFile input;
            QVERIFY(input.open());
            input.write(text);
            input.flush();
            input.seek(0);
            FILE *replaced = freopen(input.fileName().toLocal8Bit().constData(), "r", stdin);
            QVERIFY(replaced);
            const int code = Cli::appendStdin(path);
            // Put stdin back rather than closing it: a process running on with
            // descriptor 0 free hands it to the next pipe anything opens, and
            // a QProcess further down the run could not write to its child.
            QVERIFY(freopen("/dev/null", "r", stdin));
            QCOMPARE(code, 0);
        };

        append("appended\n");

        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(written.readAll(), QByteArray("first line\nappended\n"));
    }

    void listsNothingWhenThereIsNoSession() {
        // A first run has no session file, and a caller looping over the output
        // should see an empty list rather than an error.
        const QString sessionPath =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("session.json"));
        QFile::remove(sessionPath);
        QCOMPARE(Cli::listTabs(), 0);
    }

    void keepsTheReadingPlaceAcrossThePreviewToggle() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        auto *viewport = window->findChild<QQuickItem *>(QStringLiteral("editorViewport"));
        QVERIFY(editor);
        QVERIFY(viewport);

        // Long enough that the viewport cannot hold it, or there is no scrolling
        // to preserve and the test would pass without proving anything.
        QString document;
        for (int paragraph = 0; paragraph < 200; ++paragraph) {
            document += QStringLiteral("## Heading %1\n\nSome words in a paragraph.\n\n")
                            .arg(paragraph);
        }
        editor->setProperty("text", document);

        QTRY_VERIFY(viewport->property("contentHeight").toReal()
                    > viewport->property("height").toReal());

        const auto span = [&viewport] {
            return viewport->property("contentHeight").toReal()
                   - viewport->property("height").toReal();
        };
        const auto fraction = [&viewport, &span] {
            return span() > 0 ? viewport->property("contentY").toReal() / span() : 0.0;
        };

        QVERIFY(QMetaObject::invokeMethod(viewport, "scrollTo",
                                          Q_ARG(QVariant, span() * 0.5)));
        const qreal before = fraction();
        QVERIFY(before > 0.4);

        // The preview is a different height, so the place in the writing is the
        // only thing the two surfaces can agree on.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));
        QVERIFY(window->property("previewVisible").toBool());
        QTRY_VERIFY(qAbs(fraction() - before) < 0.05);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "togglePreview"));
        QVERIFY(!window->property("previewVisible").toBool());
        QTRY_VERIFY(qAbs(fraction() - before) < 0.05);
    }

    void namesTheLinkUnderThePointer() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        auto *label = window->findChild<QQuickItem *>(QStringLiteral("linkLabel"));
        QVERIFY(label);
        // Nothing under the pointer, nothing in the way of the writing.
        QVERIFY(!label->property("visible").toBool());

        const auto describe = [&window](const QVariantMap &link) {
            QVariant answer;
            QMetaObject::invokeMethod(window.data(), "linkLabel",
                                      Q_RETURN_ARG(QVariant, answer),
                                      Q_ARG(QVariant, QVariant::fromValue(link)));
            return answer.toString();
        };

        QCOMPARE(describe({{QStringLiteral("kind"), QStringLiteral("none")}}), QString());

        QCOMPARE(describe({{QStringLiteral("kind"), QStringLiteral("url")},
                           {QStringLiteral("url"), QUrl(QStringLiteral("https://example.com/a"))}}),
                 QStringLiteral("https://example.com/a"));

        // A local file reads as a path, not as a file:// URL nobody types.
        QCOMPARE(describe({{QStringLiteral("kind"), QStringLiteral("url")},
                           {QStringLiteral("url"),
                            QUrl::fromLocalFile(QStringLiteral("/tmp/notes/a.md"))}}),
                 QStringLiteral("/tmp/notes/a.md"));

        // A wikilink reads as it was written; one with no note behind it says so
        // rather than showing a path that is not there.
        QCOMPARE(describe({{QStringLiteral("kind"), QStringLiteral("file")},
                           {QStringLiteral("url"),
                            QUrl::fromLocalFile(QStringLiteral("/tmp/notes/Standup.md"))},
                           {QStringLiteral("target"), QStringLiteral("Standup")}}),
                 QStringLiteral("[[Standup]]"));
        QCOMPARE(describe({{QStringLiteral("kind"), QStringLiteral("missing")},
                           {QStringLiteral("target"), QStringLiteral("Retro")}}),
                 QStringLiteral("[[Retro]] — no note yet"));
    }

    void takesAnOutsideEditWhenNothingLocalIsAtStake() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("shared-draft.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
        seed.write("line one\nline two\nline three\n");
        seed.close();

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(!backend.modified());
        editor->setProperty("cursorPosition", 9);

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);

        // Someone else writes the file while it is open and untouched here.
        QFile outside(path);
        QVERIFY(outside.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        outside.write("line one\nline two edited\nline three\n");
        outside.close();

        // Taken rather than asked about, because there is nothing of theirs to
        // weigh it against.
        QTRY_COMPARE(editor->property("text").toString(),
                     QStringLiteral("line one\nline two edited\nline three\n"));
        QCOMPARE(conflict.count(), 0);
        QVERIFY(!backend.modified());

        // The caret keeps its place rather than snapping to the top of text the
        // reader did not ask for.
        QTRY_COMPARE(backend.activeCursorPosition(), 9);
    }

    void stillAsksWhenTheOutsideEditMeetsLocalChanges() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("contested-draft.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
        seed.write("original\n");
        seed.close();

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        backend.open(QUrl::fromLocalFile(path));
        editor->setProperty("text", QStringLiteral("mine, unsaved"));
        QVERIFY(backend.modified());

        QSignalSpy conflict(&backend, &Backend::externalChangeDetected);
        QFile outside(path);
        QVERIFY(outside.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        outside.write("theirs\n");
        outside.close();

        // Two versions of the document now exist and only the writer can say
        // which one survives, so this one still asks.
        QTRY_COMPARE(conflict.count(), 1);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("mine, unsaved"));
    }

    void closesATabThatIsNotTheOneShowing() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        const QString first = backend.activeBufferId();
        const QString second = backend.newBuffer();
        QCOMPARE(backend.buffers().size(), 2);
        QCOMPARE(backend.activeBufferId(), second);

        // The strip has laid a tab out for each buffer, which is what the close
        // cross hangs off.
        auto *tabFlick = window->findChild<QQuickItem *>(QStringLiteral("tabFlick"));
        QVERIFY(tabFlick);
        QTRY_VERIFY(tabFlick->property("contentWidth").toReal() > 0);

        // Closing only ever closes the active tab, so a cross on any other one
        // has to bring its tab forward first. That is the path this takes.
        QVERIFY(backend.selectBuffer(first));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "requestCloseTab"));
        QCOMPARE(backend.buffers().size(), 1);
        QCOMPARE(backend.activeBufferId(), second);
    }

    void keepsTheDocumentClearOfTheTabStrip() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        auto *bar = window->findChild<QQuickItem *>(QStringLiteral("tabBar"));
        auto *viewport = window->findChild<QQuickItem *>(QStringLiteral("editorViewport"));
        QVERIFY(bar);
        QVERIFY(viewport);

        // One buffer, no strip, and the document has the window to itself.
        QCOMPARE(backend.buffers().size(), 1);
        QVERIFY(!bar->property("visible").toBool());
        QTRY_COMPARE(viewport->mapToScene(QPointF()).y(), qreal(0));

        // A second tab puts the strip up, and the document starts below it
        // rather than scrolling underneath.
        backend.newBuffer();
        QTRY_VERIFY(bar->property("visible").toBool());
        QTRY_COMPARE(viewport->mapToScene(QPointF()).y(),
                     bar->mapToScene(QPointF(0, bar->height())).y());

        // The strip paints, so what scrolls behind it cannot show through the
        // gaps between the tabs.
        const QColor stripColour = bar->property("color").value<QColor>();
        QCOMPARE(stripColour.alpha(), 255);
        QCOMPARE(stripColour, QColor(window->property("pageColor").toString()));
    }

    void opensTheShortcutListFromEitherKey() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                  new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        // Ctrl+? is Ctrl+Shift+/ on most layouts, so the list also answers to
        // the key you can reach with one hand.
        QObject *shortcut = nullptr;
        for (QObject *candidate : window->findChildren<QObject *>()) {
            if (!candidate->inherits("QQuickShortcut"))
                continue;
            const QVariantList keys = candidate->property("sequences").toList();
            QStringList spelled;
            for (const QVariant &key : keys)
                spelled << key.toString();
            if (spelled.contains(QStringLiteral("Ctrl+/"))) {
                shortcut = candidate;
                QVERIFY2(spelled.contains(QStringLiteral("Ctrl+?")),
                         "the old key still has to work");
                break;
            }
        }
        QVERIFY2(shortcut, "nothing is bound to Ctrl+/");
    }

    void givesTheWritingColumnWhateverTheDocksLeave() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                 new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        // The measure fills the window rather than stopping at a fixed count
        // of characters: most of the width is the document.
        const qreal windowWidth = window->property("width").toReal();
        const int full = window->property("editorWidth").toInt();
        QVERIFY2(full > windowWidth * 0.75, "the writing column is not filling the window");

        // Each dock takes its width out of the column rather than sliding
        // over it, and both together take more than either alone.
        window->setProperty("sidebarOpen", true);
        QTRY_VERIFY(window->property("editorWidth").toInt() < full);
        const int withSidebar = window->property("editorWidth").toInt();

        window->setProperty("agentOpen", true);
        QTRY_VERIFY(window->property("editorWidth").toInt() < withSidebar);
        const int withBoth = window->property("editorWidth").toInt();
        QCOMPARE(withBoth, full - window->property("dockedWidth").toInt());

        window->setProperty("sidebarOpen", false);
        window->setProperty("agentOpen", false);
        QTRY_COMPARE(window->property("editorWidth").toInt(), full);
    }

    void drawsThePreviewAtTheSizesTheEditorUses() {
        QTextDocument rendered;
        QFont editorFont(Backend::appFont());
        editorFont.setPixelSize(12);
        rendered.setDefaultFont(editorFont);
        rendered.setMarkdown(QStringLiteral("# One\n\n## Two\n\nSome prose with `code` in it.\n\n"
                                            "```\nfenced code\n```\n"),
                             QTextDocument::MarkdownDialectGitHub);

        // Qt renders Markdown at its own heading sizes, which is the thing
        // being corrected: the preview has to agree with the source view.
        Backend::applyPreviewTypography(&rendered, editorFont);

        const auto sizeOfLine = [&rendered](const QString &text) {
            for (QTextBlock block = rendered.begin(); block.isValid(); block = block.next()) {
                if (!block.text().contains(text))
                    continue;
                for (auto it = block.begin(); it != block.end(); ++it) {
                    if (it.fragment().isValid())
                        return it.fragment().charFormat().fontPointSize();
                }
            }
            return qreal(-1);
        };
        const auto familyOfLine = [&rendered](const QString &text) {
            for (QTextBlock block = rendered.begin(); block.isValid(); block = block.next()) {
                if (!block.text().contains(text))
                    continue;
                for (auto it = block.begin(); it != block.end(); ++it) {
                    if (it.fragment().isValid())
                        return it.fragment().charFormat().fontFamilies().toStringList().value(0);
                }
            }
            return QString();
        };

        QCOMPARE(sizeOfLine(QStringLiteral("One")),
                 MarkdownHighlighter::headingPointSize(12, 1));
        QCOMPARE(sizeOfLine(QStringLiteral("Two")),
                 MarkdownHighlighter::headingPointSize(12, 2));
        // Body text is the editor's own size, in points rather than pixels.
        QCOMPARE(sizeOfLine(QStringLiteral("Some prose")), qreal(9));

        // Code is the same face as the prose around it, as it is in the
        // source view, rather than Qt's fallback typewriter.
        QCOMPARE(familyOfLine(QStringLiteral("fenced code")), Backend::appFont());
        QCOMPARE(familyOfLine(QStringLiteral("Some prose")), Backend::appFont());
        QCOMPARE(sizeOfLine(QStringLiteral("fenced code")), qreal(9));

        // And the lines sit as far apart as the source view's do: same words,
        // same rhythm, so the toggle changes the rendering and nothing else.
        for (QTextBlock block = rendered.begin(); block.isValid(); block = block.next()) {
            if (block.text().isEmpty())
                continue;
            QCOMPARE(block.blockFormat().lineHeight(), Backend::lineHeightPercent());
            QCOMPARE(block.blockFormat().lineHeightType(),
                     int(QTextBlockFormat::ProportionalHeight));
        }

        // The heading ratios are the editor's, whatever size it is set to.
        QCOMPARE(MarkdownHighlighter::headingPointSize(20, 1) / (20 * 0.75), qreal(2.3));
        QCOMPARE(MarkdownHighlighter::headingPointSize(12, 6) / (12 * 0.75), qreal(1.3));
    }

    void takesTheWholeConversationInOneGo() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        auto *agent = new AgentSession(tabState.path(), &engine);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"), agent);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        auto *panel = window->findChild<QQuickItem *>(QStringLiteral("agentPanel"));
        QVERIFY(panel);

        // Nothing said yet is said rather than quietly copying an empty
        // string over whatever was on the clipboard.
        QGuiApplication::clipboard()->setText(QStringLiteral("untouched"));
        QMetaObject::invokeMethod(window.data(), "copyConversation");
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("untouched"));
        QTRY_COMPARE(panel->property("notice").toString(), QStringLiteral("Nothing to copy yet"));

        agent->readStreamLine(R"({"type":"system","subtype":"init","session_id":"S1"})");
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({
            R"({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Two notes."}}})",
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"Two notes."})",
        });
        agent->ask(QStringLiteral("read it back"), QUrl(), 0, QString(), QUrl());
        QTRY_VERIFY(!agent->running());

        // A drag cannot cross from one answer to the next -- each is its own
        // field -- so the whole conversation goes in one action, labelled so
        // it makes sense pasted into something else.
        QMetaObject::invokeMethod(window.data(), "copyConversation");
        const QString copied = QGuiApplication::clipboard()->text();
        QCOMPARE(copied, QStringLiteral("You: read it back\n\nClaude: Two notes."));
        QTRY_VERIFY(panel->property("notice").toString().contains(QStringLiteral("conversation")));

        // And the key does the same as the word in the header.
        QGuiApplication::clipboard()->setText(QStringLiteral("untouched"));
        QObject *shortcut = nullptr;
        for (QObject *candidate : window->findChildren<QObject *>()) {
            if (candidate->objectName() == QLatin1String("copyConversationShortcut"))
                shortcut = candidate;
        }
        QVERIFY(shortcut);
        window->setProperty("agentOpen", true);
        QMetaObject::invokeMethod(shortcut, "activated");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), copied);
    }

    void copiesAnAnswerTheSameWayTheDocumentDoes() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        auto *agent = new AgentSession(tabState.path(), &engine);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"), agent);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        auto *panel = window->findChild<QQuickItem *>(QStringLiteral("agentPanel"));
        QVERIFY(panel);
        // The panel follows the window's setting rather than one of its own.
        QCOMPARE(panel->property("copyOnSelect").toBool(), true);
        window->setProperty("agentOpen", true);

        QObject *shortcut = nullptr;
        for (QObject *candidate : window->findChildren<QObject *>()) {
            if (candidate->objectName() == QLatin1String("copyOnSelectShortcut"))
                shortcut = candidate;
        }
        QVERIFY(shortcut);
        QMetaObject::invokeMethod(shortcut, "activated");
        QTRY_COMPARE(panel->property("copyOnSelect").toBool(), false);
        QMetaObject::invokeMethod(shortcut, "activated");
        QTRY_COMPARE(panel->property("copyOnSelect").toBool(), true);

        // A copy made in the panel says so in the panel: the footer is at the
        // far corner of the window from the hand that just did it.
        QMetaObject::invokeMethod(panel, "flashNotice",
                                  Q_ARG(QVariant, QStringLiteral("Copied 12 characters")));
        QTRY_COMPARE(panel->property("notice").toString(),
                     QStringLiteral("Copied 12 characters"));
    }

    void keepsTheAgentButtonAtTheTopRight() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                 new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        auto *agentButton = window->findChild<QQuickItem *>(QStringLiteral("agentButton"));
        auto *panel = window->findChild<QQuickItem *>(QStringLiteral("agentPanel"));
        auto *root = qobject_cast<QQuickWindow *>(window.data())->contentItem();
        // The window is never shown here, so its content item has no size of
        // its own; the window's own width is what the anchors resolve against.
        const qreal windowWidth = window->property("width").toReal();
        QVERIFY(agentButton);
        QVERIFY(panel);

        // Omamail keeps its AI control at the window's top right. Here it is
        // the same corner, above the writing.
        const QPointF corner = agentButton->mapToItem(root, QPointF(agentButton->width(), 0));
        QVERIFY2(windowWidth - corner.x() < 24, "the agent button is not against the right");
        QVERIFY2(corner.y() < 24, "the agent button is not at the top");

        // Opening the panel moves it along rather than burying it underneath.
        window->setProperty("agentOpen", true);
        QTRY_VERIFY(panel->width() > 0);
        const qreal moved = agentButton->mapToItem(root, QPointF(agentButton->width(), 0)).x();
        QVERIFY(moved < corner.x());
        QVERIFY2(moved <= windowWidth - panel->width(), "the panel is over the button");
    }

    void drawsTheChromeAtOmamailsDim() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                 new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        const QColor foreground(window->property("textColor").toString());
        const QColor page(window->property("pageColor").toString());
        const QColor dim = window->property("dimColor").value<QColor>();
        // 68% foreground over 32% background, the mix Omamail draws its own
        // chrome at, taken from the live theme rather than a fixed grey.
        QCOMPARE(qRound(dim.redF() * 255),
                 qRound((foreground.redF() * 0.68 + page.redF() * 0.32) * 255));
        QCOMPARE(qRound(dim.greenF() * 255),
                 qRound((foreground.greenF() * 0.68 + page.greenF() * 0.32) * 255));

        auto *saveButton = window->findChild<QQuickItem *>(QStringLiteral("saveButton"));
        QVERIFY(saveButton);
        QCOMPARE(saveButton->property("iconColor").value<QColor>(), dim);
        // The whole strip used to sit behind 0.55 opacity, which is what made
        // the icons read as decoration.
        QCOMPARE(saveButton->parentItem()->opacity(), qreal(1));
    }

    void fallsBackToTheDrawnIconWithoutANerdFont() {
        QQmlEngine engine;
        QQmlComponent component(&engine,
                                QUrl::fromLocalFile(QFINDTESTDATA("../src/FooterIconButton.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> button(component.create());
        QVERIFY(button);

        // Ask for a glyph, and whether it is drawn depends on the machine
        // having a face that holds it: a codepoint nothing can render is a box.
        button->setProperty("glyph", 0xF167A);
        const bool hasFace = !button->property("nerdFamily").toString().isEmpty();
        QCOMPARE(button->property("drawsGlyph").toBool(), hasFace);

        button->setProperty("glyph", 0);
        QVERIFY2(!button->property("drawsGlyph").toBool(),
                 "an icon with no glyph asked for stays the drawn one");
    }

    // --- The panel Claude answers in -------------------------------------

    void runsInTheFolderTheDocumentLivesIn() {
        QTemporaryDir documents;
        QVERIFY(documents.isValid());
        QTemporaryDir browsing;
        QVERIFY(browsing.isValid());
        const QString note = documents.filePath(QStringLiteral("note.md"));

        QCOMPARE(AgentSession::directoryFor(note, browsing.path()), documents.path());
        // An untitled document has no folder of its own, so it borrows the one
        // the sidebar is showing.
        QCOMPARE(AgentSession::directoryFor(QString(), browsing.path()), browsing.path());
        // And with neither, somewhere that exists rather than nowhere.
        QCOMPARE(AgentSession::directoryFor(QString(), QString()), QDir::homePath());
    }

    void tellsClaudeWhatIsOnScreenAndHowToReadIt() {
        QTemporaryDir documents;
        QVERIFY(documents.isValid());
        const QString note = documents.filePath(QStringLiteral("note.md"));

        const QString preamble = AgentSession::preamble(note, 12,
                                                        QStringLiteral("a chosen sentence"),
                                                        QString());
        QVERIFY(preamble.contains(note));
        QVERIFY(preamble.contains(QStringLiteral("line 12")));
        QVERIFY(preamble.contains(QStringLiteral("a chosen sentence")));
        QVERIFY(preamble.contains(documents.path()));
        // The reading calls are the reason a question about an unsaved
        // paragraph can be answered at all.
        QVERIFY(preamble.contains(QStringLiteral("omawrite --read")));

        // A selection is the writer's words, so it arrives fenced rather than
        // loose in the instructions.
        QVERIFY(preamble.contains(QStringLiteral("```\na chosen sentence\n```")));
    }

    void showsTheWorkWithoutShowingItsArguments() {
        QVariantMap input;
        input[QStringLiteral("file_path")] = QStringLiteral("/home/writer/notes/standup.md");
        QCOMPARE(AgentSession::activityFor(QStringLiteral("Read"), input),
                 QStringLiteral("Reading standup.md"));
        QCOMPARE(AgentSession::activityFor(QStringLiteral("Edit"), input),
                 QStringLiteral("Editing standup.md"));

        QVariantMap command;
        command[QStringLiteral("command")] = QStringLiteral("git log --oneline -20");
        const QString shown = AgentSession::activityFor(QStringLiteral("Bash"), command);
        QCOMPARE(shown, QStringLiteral("Running a command"));
        QVERIFY2(!shown.contains(QStringLiteral("git")), "the command itself is not the panel's");
    }

    void tellsTheTurnWhatItMayDo() {
        // A turn that thinks it can run anything offers a patch to paste
        // instead of making the edit, or reports a command as failing when
        // it was never allowed to run.
        const QString limited = AgentSession::permissionBrief(QStringLiteral("acceptEdits"),
                                                              QStringLiteral("Bash(omawrite:*)"));
        QVERIFY(limited.contains(QStringLiteral("without asking")));
        QVERIFY(limited.contains(QStringLiteral("Bash(omawrite:*)")));
        QVERIFY(limited.contains(QStringLiteral("refused")));

        const QString none = AgentSession::permissionBrief(QStringLiteral("acceptEdits"),
                                                           QString());
        QVERIFY(none.contains(QStringLiteral("No shell commands")));

        const QString everything = AgentSession::permissionBrief(
            QStringLiteral("bypassPermissions"), QStringLiteral("Bash(omawrite:*)"));
        QVERIFY(everything.contains(QStringLiteral("every tool allowed")));
        QVERIFY2(!everything.contains(QStringLiteral("refused")),
                 "a turn that can do anything should not be told what it cannot do");

        // The allow-list is a setting, and an empty one means none rather
        // than the default: somebody who cleared it meant it.
        QSettings().setValue(QStringLiteral("agent/allowedTools"), QString());
        QCOMPARE(AgentSession::allowedTools(), QString());
        QSettings().setValue(QStringLiteral("agent/allowedTools"),
                             QStringLiteral("Bash(git status:*)"));
        QCOMPARE(AgentSession::allowedTools(), QStringLiteral("Bash(git status:*)"));
        QVERIFY(AgentSession::arguments(QString(), QStringLiteral("acceptEdits"),
                                        AgentSession::allowedTools())
                    .contains(QStringLiteral("Bash(git status:*)")));
        QSettings().remove(QStringLiteral("agent/allowedTools"));
        QCOMPARE(AgentSession::allowedTools(), QStringLiteral("Bash(omawrite:*)"));
    }

    void asksWithoutATerminalAndWithoutAnArgumentList() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({
            R"({"type":"system","subtype":"init","session_id":"S1"})",
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"ok"})",
        });

        QTemporaryDir documents;
        QVERIFY(documents.isValid());
        const QString note = documents.filePath(QStringLiteral("note.md"));
        QVERIFY(writeFile(note, QByteArrayLiteral("# Note\n")));

        AgentSession session;
        session.ask(QStringLiteral("what is this about"), QUrl::fromLocalFile(note), 1,
                    QString(), QUrl::fromLocalFile(documents.path()));
        QTRY_VERIFY(!session.running());

        const QStringList arguments = claude.recorded(QStringLiteral("args"))
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(arguments.contains(QStringLiteral("-p")));
        QVERIFY(arguments.contains(QStringLiteral("stream-json")));
        QVERIFY(arguments.contains(QStringLiteral("--include-partial-messages")));
        // The mode has to answer for the writer: a child with no terminal has
        // nobody to ask, and a mode that asks would hang the turn. acceptEdits
        // takes an edit to the document and refuses a shell command, which
        // dontAsk refuses as well -- it blocks both.
        QVERIFY(arguments.contains(QStringLiteral("--permission-mode")));
        QVERIFY(arguments.contains(QStringLiteral("acceptEdits")));
        // acceptEdits refuses every shell command, so the editor's own
        // command line is named explicitly or the brief's offer of the live
        // buffer is a promise the turn cannot keep.
        QVERIFY(arguments.contains(QStringLiteral("--allowedTools")));
        QVERIFY(arguments.contains(QStringLiteral("Bash(omawrite:*)")));
        // A first turn has no session to resume.
        QVERIFY(!arguments.contains(QStringLiteral("--resume")));

        // The question goes in on stdin. An argument list is readable by every
        // other process on the machine, and a document's words are not.
        const QString stdinText = claude.recorded(QStringLiteral("stdin"));
        QVERIFY(stdinText.contains(QStringLiteral("what is this about")));
        QVERIFY(stdinText.contains(note));
        QVERIFY2(!claude.recorded(QStringLiteral("args")).contains(QStringLiteral("what is this")),
                 "the question must not be an argument");

        // And it runs where the document lives, which is the whole point:
        // everything beside it is readable without being copied into a prompt.
        QCOMPARE(QFileInfo(claude.recorded(QStringLiteral("cwd")).trimmed()).canonicalFilePath(),
                 QFileInfo(documents.path()).canonicalFilePath());
    }

    void streamsTheAnswerAndLeavesTheThinkingOut() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({
            R"({"type":"system","subtype":"init","session_id":"S1"})",
            R"({"type":"stream_event","session_id":"S1","event":{"type":"content_block_delta","delta":{"type":"thinking_delta","thinking":"weighing it up"}}})",
            R"({"type":"stream_event","session_id":"S1","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Two "}}})",
            R"({"type":"stream_event","session_id":"S1","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"notes."}}})",
            QStringLiteral("this line is not JSON at all"),
            R"({"type":"assistant","session_id":"S1","message":{"content":[{"type":"text","text":"Two notes."}]}})",
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"Two notes."})",
        });

        AgentSession session;
        session.ask(QStringLiteral("read it back to me"), QUrl(), 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());

        const QVariantList messages = session.messages();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toMap().value(QStringLiteral("role")).toString(),
                 QStringLiteral("you"));
        QCOMPARE(messages.at(1).toMap().value(QStringLiteral("role")).toString(),
                 QStringLiteral("claude"));
        // The deltas are the answer. The assistant message that repeats them
        // whole is not a second answer, and the thinking is not the panel's to
        // show.
        QCOMPARE(messages.at(1).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Two notes."));
        QVERIFY(session.activity().isEmpty());
    }

    void answersEvenWithoutPartialMessages() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({
            R"({"type":"assistant","session_id":"S1","message":{"content":[{"type":"text","text":"Whole."}]}})",
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"Whole."})",
        });

        AgentSession session;
        session.ask(QStringLiteral("anything"), QUrl(), 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());
        QCOMPARE(session.messages().size(), 2);
        QCOMPARE(session.messages().at(1).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Whole."));
    }

    void carriesTheConversationIntoTheNextTurn() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({
            R"({"type":"system","subtype":"init","session_id":"S1"})",
            R"({"type":"stream_event","session_id":"S1","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"First."}}})",
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"First."})",
        });

        AgentSession session;
        session.ask(QStringLiteral("first"), QUrl(), 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());

        session.ask(QStringLiteral("and now the second"), QUrl(), 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());

        const QStringList arguments = claude.recorded(QStringLiteral("args"))
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(arguments.contains(QStringLiteral("--resume")));
        QVERIFY(arguments.contains(QStringLiteral("S1")));
        // Forked, so two chats carrying on from one turn cannot answer each
        // other's questions.
        QVERIFY(arguments.contains(QStringLiteral("--fork-session")));

        // A follow-up is the question, not the whole standing brief again.
        const QString stdinText = claude.recorded(QStringLiteral("stdin"));
        QCOMPARE(stdinText.trimmed(), QStringLiteral("and now the second"));

        // Starting again drops the session with the history.
        session.newChat();
        QVERIFY(session.messages().isEmpty());
        session.ask(QStringLiteral("fresh"), QUrl(), 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());
        QVERIFY(!claude.recorded(QStringLiteral("args")).contains(QStringLiteral("--resume")));
    }

    void saysSoWhenTheTurnFails() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({});
        claude.setExitCode(1);

        AgentSession session;
        session.ask(QStringLiteral("anything"), QUrl(), 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());

        // The empty answer is taken away rather than left as a blank bubble,
        // and what happened is said in its place.
        const QVariantList messages = session.messages();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(1).toMap().value(QStringLiteral("role")).toString(),
                 QStringLiteral("trouble"));
        QVERIFY(messages.at(1).toMap().value(QStringLiteral("text")).toString()
                    .contains(QStringLiteral("login or permissions")));
    }

    void stoppingTheTurnTakesItsToolsWithIt() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        // A turn that has started something of its own, so stopping it has to
        // reach further than the process the panel itself started.
        claude.setBody(QStringLiteral("sleep 5\ntouch \"$dir/finished\"\n"));

        AgentSession session;
        session.ask(QStringLiteral("take your time"), QUrl(), 0, QString(), QUrl());
        QVERIFY(session.running());
        session.interrupt();
        QVERIFY(!session.running());

        QTest::qWait(900);
        QVERIFY2(!QFileInfo::exists(claude.path(QStringLiteral("finished"))),
                 "the work the turn started outlived the turn");
    }

    void refusesASecondTurnWhileOneIsRunning() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setBody(QStringLiteral("sleep 5\n"));

        AgentSession session;
        session.ask(QStringLiteral("first"), QUrl(), 0, QString(), QUrl());
        QVERIFY(session.running());
        const int asked = session.messages().size();
        session.ask(QStringLiteral("second"), QUrl(), 0, QString(), QUrl());
        QCOMPARE(session.messages().size(), asked);
        session.interrupt();
    }

    void bringsTheConversationBackWithTheDocument() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({
            R"({"type":"system","subtype":"init","session_id":"S1"})",
            R"({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Eighty columns, then."}}})",
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"Eighty columns, then."})",
        });

        QTemporaryDir state;
        QVERIFY(state.isValid());
        QTemporaryDir documents;
        QVERIFY(documents.isValid());
        const QString note = documents.filePath(QStringLiteral("status.md"));
        QVERIFY(writeFile(note, QByteArrayLiteral("# Status\n")));
        const QUrl noteUrl = QUrl::fromLocalFile(note);

        {
            AgentSession session(state.path());
            session.showDocument(noteUrl);
            session.ask(QStringLiteral("should we wrap at 80"), noteUrl, 1, QString(), QUrl());
            QTRY_VERIFY(!session.running());
            QCOMPARE(session.messages().size(), 2);
        }

        // A new window, a new process, the same document: the conversation is
        // where it was left, and the next turn carries on the same session
        // rather than introducing itself again.
        AgentSession restored(state.path());
        restored.showDocument(noteUrl);
        QCOMPARE(restored.messages().size(), 2);
        QCOMPARE(restored.messages().at(1).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Eighty columns, then."));

        restored.ask(QStringLiteral("and the tables?"), noteUrl, 1, QString(), QUrl());
        QTRY_VERIFY(!restored.running());
        const QStringList arguments = claude.recorded(QStringLiteral("args"))
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(arguments.contains(QStringLiteral("--resume")));
        QVERIFY(arguments.contains(QStringLiteral("S1")));
    }

    void keepsOneConversationPerDocument() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setStream({
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"noted"})",
        });

        QTemporaryDir state;
        QVERIFY(state.isValid());
        QTemporaryDir documents;
        QVERIFY(documents.isValid());
        const QUrl first = QUrl::fromLocalFile(documents.filePath(QStringLiteral("one.md")));
        const QUrl second = QUrl::fromLocalFile(documents.filePath(QStringLiteral("two.md")));

        AgentSession session(state.path());
        session.showDocument(first);
        session.ask(QStringLiteral("about the first"), first, 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());
        QCOMPARE(session.messages().size(), 2);

        // Moving to another tab shows that document's chat, which is empty.
        session.showDocument(second);
        QVERIFY(session.messages().isEmpty());

        // And moving back brings the first one's conversation with it.
        session.showDocument(first);
        QCOMPARE(session.messages().size(), 2);
        QCOMPARE(session.messages().at(0).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("about the first"));

        // Clearing is the one thing that means "do not bring this back".
        session.newChat();
        QVERIFY(session.messages().isEmpty());
        AgentSession reopened(state.path());
        reopened.showDocument(first);
        QVERIFY(reopened.messages().isEmpty());
    }

    void startsOverWhenAKeptConversationHasGone() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        // The CLI refuses the session it is told to resume, as it does once
        // that conversation has been cleared or was left on another machine.
        claude.setBody(QStringLiteral("case \"$*\" in *--resume*) exit 1;; esac\n"
                                      "cat \"$dir/stream\" 2>/dev/null\n"));
        claude.setStream({
            R"({"type":"result","subtype":"success","is_error":false,"session_id":"S1","result":"fine"})",
        });

        QTemporaryDir state;
        QVERIFY(state.isValid());
        QTemporaryDir documents;
        QVERIFY(documents.isValid());
        const QUrl note = QUrl::fromLocalFile(documents.filePath(QStringLiteral("note.md")));

        AgentSession session(state.path());
        session.showDocument(note);
        session.ask(QStringLiteral("first"), note, 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());

        session.ask(QStringLiteral("second"), note, 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());
        const QVariantList messages = session.messages();
        QCOMPARE(messages.last().toMap().value(QStringLiteral("role")).toString(),
                 QStringLiteral("trouble"));
        QVERIFY(messages.last().toMap().value(QStringLiteral("text")).toString()
                    .contains(QStringLiteral("could not be picked up")));

        // The dead session is dropped rather than failing the same way for
        // ever: the next turn introduces itself instead of resuming.
        session.ask(QStringLiteral("third"), note, 0, QString(), QUrl());
        QTRY_VERIFY(!session.running());
        QVERIFY(!claude.recorded(QStringLiteral("args")).contains(QStringLiteral("--resume")));
        QCOMPARE(session.messages().last().toMap().value(QStringLiteral("role")).toString(),
                 QStringLiteral("claude"));
    }

    void waitsForTheAnswerBeforeChangingDocument() {
        FakeClaude claude;
        QVERIFY(claude.ok);
        claude.setBody(QStringLiteral("sleep 5\n"));

        QTemporaryDir state;
        QVERIFY(state.isValid());
        QTemporaryDir documents;
        QVERIFY(documents.isValid());
        const QUrl first = QUrl::fromLocalFile(documents.filePath(QStringLiteral("one.md")));
        const QUrl second = QUrl::fromLocalFile(documents.filePath(QStringLiteral("two.md")));

        AgentSession session(state.path());
        session.showDocument(first);
        session.ask(QStringLiteral("about the first"), first, 0, QString(), QUrl());
        QVERIFY(session.running());

        // The answer belongs to the document that asked for it, so the swap
        // waits rather than filing it under whatever tab is in front now.
        session.showDocument(second);
        QVERIFY(session.running());
        QCOMPARE(session.messages().size(), 2);
        session.interrupt();
        QTRY_VERIFY(session.messages().isEmpty());
    }

    void opensThePanelWithoutSqueezingTheWriting() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                 new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        auto *panel = window->findChild<QQuickItem *>(QStringLiteral("agentPanel"));
        QVERIFY(panel);
        QCOMPARE(panel->width(), qreal(0));

        const int writingWidth = window->property("editorWidth").toInt();
        window->setProperty("agentOpen", true);
        QTRY_VERIFY(panel->width() > 0);
        // The dock takes its width from the window, not from the document: the
        // writing column narrows rather than sliding under the panel.
        QVERIFY(window->property("editorWidth").toInt() <= writingWidth);
        QCOMPARE(window->property("dockedWidth").toInt(), int(panel->width()));
    }

    // The input box stops growing at its cap. Past that the writing has to move
    // under the caret, or a long question runs on below the border and you lose
    // sight of what you are typing.
    void keepsTheCaretInSightInALongQuestion() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir tabState;
        QVERIFY(tabState.isValid());
        Backend backend(tabState.path());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("agent"),
                                                 new AgentSession(&engine));
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        window->setProperty("agentOpen", true);
        auto *panel = window->findChild<QQuickItem *>(QStringLiteral("agentPanel"));
        QVERIFY(panel);
        QTRY_VERIFY(panel->width() > 0);

        auto *input = window->findChild<QQuickItem *>(QStringLiteral("agentInput"));
        QVERIFY(input);
        QVERIFY(QMetaObject::invokeMethod(input, "forceActiveFocus"));

        // Enough lines to take the text well past the box's cap.
        QString question;
        for (int line = 0; line < 40; ++line)
            question += QStringLiteral("line %1 of a long question\n").arg(line);
        input->setProperty("text", question);
        input->setProperty("cursorPosition", question.size());

        // Something has to scroll the writing under the caret; a plain TextArea
        // filling the box has nothing that can.
        auto *scroll = window->findChild<QQuickItem *>(QStringLiteral("agentInputScroll"));
        QVERIFY2(scroll, "the input has no flickable behind it");

        // The box is capped, so the question is taller than the band it shows.
        QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());

        // And the caret is inside that band rather than below it.
        const qreal top = scroll->property("contentY").toReal();
        const QRectF caret = input->property("cursorRectangle").toRectF();
        QVERIFY2(caret.top() >= top - 1.0 && caret.bottom() <= top + scroll->height() + 1.0,
                 qPrintable(QStringLiteral("caret %1..%2, band %3..%4")
                                .arg(caret.top()).arg(caret.bottom())
                                .arg(top).arg(top + scroll->height())));
    }

    // The sidebar's ordinary switch: open one file, then pick the next.
    void theSidebarSwitchesTheDocument() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        auto put = [&](const QString &name, const QByteArray &body) {
            QFile f(folder.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(body);
            f.close();
        };
        put(QStringLiteral("CLAUDE.md"), "CLAUDE BODY");
        put(QStringLiteral("README.md"), "README BODY");

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());
        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        Backend *backend = manager.createWindow();
        QVERIFY(backend);

        backend->setFolder(QUrl::fromLocalFile(folder.path()));

        backend->open(QUrl::fromLocalFile(folder.filePath(QStringLiteral("CLAUDE.md"))));
        QCOMPARE(backend->activeBufferText(), QStringLiteral("CLAUDE BODY"));

        backend->open(QUrl::fromLocalFile(folder.filePath(QStringLiteral("README.md"))));
        QCOMPARE(backend->activeBufferText(), QStringLiteral("README BODY"));
        QCOMPARE(backend->buffers().size(), 1);
    }

    // Same click, but the session already holds a window that is not on screen
    // -- what a crash, a kill or a second process leaves behind. The file the
    // writer asks for is "already open" in that window, so the open used to be
    // handed to it and dropped: the editor reloaded the document it already
    // had, which reads as a flash with nothing changed, and the status line
    // said the file had opened. The open lands here instead, and the record
    // nothing is showing goes.
    void opensAFileAWindowNobodyIsShowingClaims() {
        QTemporaryDir stateDirectory;
        QVERIFY(stateDirectory.isValid());
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        auto put = [&](const QString &name, const QByteArray &body) {
            QFile f(folder.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(body);
            f.close();
        };
        put(QStringLiteral("CLAUDE.md"), "CLAUDE BODY");
        put(QStringLiteral("README.md"), "README BODY");

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        WorkspaceSession session(stateDirectory.path());

        // The window nobody can see, holding README.md.
        const QString ghost = session.createWindow(-1, -1, 1280, 820, false);
        session.createTab(ghost, QUrl::fromLocalFile(folder.filePath(QStringLiteral("README.md"))),
                          QStringLiteral("README BODY"), 0, 0, 0, false);
        session.saveNow();

        QQmlEngine engine;
        WindowManager manager(&session, &engine, QUrl::fromLocalFile(mainQmlPath));
        Backend *backend = manager.createWindow();
        QVERIFY(backend);
        backend->setFolder(QUrl::fromLocalFile(folder.path()));

        backend->open(QUrl::fromLocalFile(folder.filePath(QStringLiteral("CLAUDE.md"))));
        QCOMPARE(backend->activeBufferText(), QStringLiteral("CLAUDE BODY"));

        backend->open(QUrl::fromLocalFile(folder.filePath(QStringLiteral("README.md"))));
        QCOMPARE(backend->activeBufferText(), QStringLiteral("README BODY"));
        QCOMPARE(backend->fileUrl(),
                 QUrl::fromLocalFile(folder.filePath(QStringLiteral("README.md"))));
        QCOMPARE(backend->status(), QStringLiteral("Opened README.md"));

        // The window nothing was showing is gone rather than left to refuse
        // every later open of the file it claimed.
        for (const QVariant &value : session.windows())
            QVERIFY(value.toMap().value(QStringLiteral("id")).toString() != ghost);
    }

private:
    // A stand-in for the Claude command line: it records the arguments, the
    // working directory and the stdin it was handed, then prints whatever
    // stream the test put in front of it. The panel is tested against a real
    // child process this way, without a model behind it.
    struct FakeClaude {
        FakeClaude() {
            ok = directory.isValid() && setBody(QStringLiteral("cat \"$dir/stream\" 2>/dev/null\n"
                                                               "exit $(cat \"$dir/exit\" "
                                                               "2>/dev/null || echo 0)\n"));
            if (ok)
                qputenv("OMAWRITE_CLAUDE", program().toUtf8());
        }
        ~FakeClaude() { qunsetenv("OMAWRITE_CLAUDE"); }

        QString program() const { return directory.filePath(QStringLiteral("claude")); }
        QString path(const QString &name) const { return directory.filePath(name); }

        bool setBody(const QString &body) {
            // Built by hand rather than through QString::arg, which would
            // leave printf's %% alone and record nothing.
            const QString script = QStringLiteral("#!/bin/sh\ndir=\"")
                + directory.path()
                + QStringLiteral("\"\n"
                                 "printf '%s\\n' \"$@\" > \"$dir/args\"\n"
                                 "pwd > \"$dir/cwd\"\n"
                                 "cat > \"$dir/stdin\"\n")
                + body;
            QFile file(program());
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            if (file.write(script.toUtf8()) != qint64(script.toUtf8().size()))
                return false;
            file.close();
            return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
        }

        bool setStream(const QStringList &lines) {
            QFile file(path(QStringLiteral("stream")));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            for (const QString &line : lines)
                file.write(line.toUtf8() + '\n');
            return true;
        }

        bool setExitCode(int code) {
            QFile file(path(QStringLiteral("exit")));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            return file.write(QByteArray::number(code)) > 0;
        }

        QString recorded(const QString &name) const {
            QFile file(path(name));
            if (!file.open(QIODevice::ReadOnly))
                return QString();
            return QString::fromUtf8(file.readAll());
        }

        QTemporaryDir directory;
        bool ok = false;
    };

    static bool writeFile(const QString &path, const QByteArray &contents) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(contents) == qint64(contents.size());
    }

    static bool writeImage(const QString &path) {
        QImage image(6, 4, QImage::Format_RGB32);
        image.fill(Qt::green);
        return QDir().mkpath(QFileInfo(path).absolutePath()) && image.save(path);
    }

    // The document the window's preview draws, attached the way the
    // pasted-image test does it.
    static QTextDocument *attachPreview(QObject *window, Backend &backend) {
        QObject *preview = window->findChild<QObject *>(QStringLiteral("renderedPreview"));
        if (!preview)
            return nullptr;
        auto *quickDocument = preview->property("textDocument").value<QQuickTextDocument *>();
        if (!quickDocument)
            return nullptr;
        backend.attachPreviewDocument(quickDocument);
        return quickDocument->textDocument();
    }

    // Every image the preview holds, by the name it was written with, and
    // whether the green picture writeImage left on disk is what gets drawn.
    // It is drawn through the layout's own image handler rather than asked of
    // the document, because the handler reads a file itself when the
    // document has no answer for it.
    static QHash<QString, bool> imagesIn(QTextDocument *document) {
        QHash<QString, bool> images;
        QTextObjectInterface *handler =
            document->documentLayout()->handlerForObject(QTextFormat::ImageObject);
        if (!handler)
            return images;
        for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextCharFormat format = it.fragment().charFormat();
                if (!format.isImageFormat())
                    continue;
                QImage canvas(6, 4, QImage::Format_ARGB32_Premultiplied);
                canvas.fill(Qt::transparent);
                QPainter painter(&canvas);
                handler->drawObject(&painter, QRectF(0, 0, 6, 4), document,
                                    it.fragment().position(), format);
                painter.end();
                images.insert(format.toImageFormat().name(),
                              canvas.pixelColor(3, 2) == QColor(Qt::green));
            }
        }
        return images;
    }

    // Points HOME at a scratch tree holding one colors.toml, and puts it back on the way out.
    struct ScopedTheme {
        explicit ScopedTheme(const QByteArray &palette) {
            const QString themeDirectory = home.path()
                + QStringLiteral("/.local/state/omarchy/current/theme");
            QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
            ok = home.isValid() && QDir().mkpath(themeDirectory)
                && qputenv("HOME", home.path().toUtf8())
                && colorsFile.open(QIODevice::WriteOnly | QIODevice::Text)
                && colorsFile.write(palette) == qint64(palette.size());
        }
        ~ScopedTheme() { qputenv("HOME", originalHome); }

        QTemporaryDir home;
        QByteArray originalHome = qgetenv("HOME");
        bool ok = false;
    };

    // Load EditorMutations.js into a throwaway TextEdit, so the library can be
    // driven the way Main.qml drives it.
    QObject *createJsHarness(QQmlComponent &component, const QByteArray &body,
                             const QString &name) {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        if (mutationsPath.isEmpty())
            return nullptr;

        const QByteArray harness = "import QtQuick\n"
                                   "import \"EditorMutations.js\" as EditorMutations\n"
                                   "TextEdit {\n" + body + "\n}\n";
        component.setData(harness, QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + "/" + name + QStringLiteral(".qml")));
        if (!component.isReady())
            return nullptr;
        return component.create();
    }

    // A TextEdit that applies plans the way Main.qml's applyPlan() does.
    QObject *createListEditor(QQmlComponent &component) {
        return createJsHarness(component, R"QML(
                property string resultText
                property int resultCursor

                function apply(plan) {
                    EditorMutations.replaceRange(this, plan.start, plan.end, plan.replacement,
                                                 plan.selectionStartOffset,
                                                 plan.selectionEndOffset);
                    resultText = text;
                    resultCursor = cursorPosition;
                }

                function edit(plan, source, from) {
                    resultText = source;
                    resultCursor = from;
                    if (!plan)
                        return false;
                    apply(plan);
                    return true;
                }

                function indent(source, from, to, direction) {
                    text = source;
                    select(from, to);
                    return edit(EditorMutations.listIndentPlan(source, from, to, direction),
                                source, from);
                }

                function pressReturn(source, cursor) {
                    text = source;
                    cursorPosition = cursor;
                    return edit(EditorMutations.returnPlan(source, cursor, cursor),
                                source, cursor);
                }

                function unwrap(source, from, to) {
                    text = source;
                    if (from === to)
                        cursorPosition = from;
                    else
                        select(from, to);
                    return edit(EditorMutations.unwrapPlan(source, from, to), source, from);
                }

                function wrap(source, from, to, columns) {
                    text = source;
                    if (from === to)
                        cursorPosition = from;
                    else
                        select(from, to);
                    return edit(EditorMutations.wrapPlan(source, from, to, columns),
                                source, from);
                }
        )QML", QStringLiteral("ListHarness"));
    }

    QObject *createMainWindow(QQmlEngine &engine, QQmlComponent &component, Backend &backend) {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        if (mainQmlPath.isEmpty())
            return nullptr;

        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

        engine.rootContext()->setContextProperty(QStringLiteral("agent"),

                                                  new AgentSession(&engine));
        component.loadUrl(QUrl::fromLocalFile(mainQmlPath));
        if (!component.isReady())
            return nullptr;
        return component.create();
    }

    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
