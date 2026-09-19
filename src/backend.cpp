#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QScreen>
#include <QFontDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>
#include <functional>

#include "markdownhighlighter.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");
const QString editorFontSizeSetting = QStringLiteral("editor/fontSize");
constexpr int defaultEditorFontSize = 12;
constexpr int minimumEditorFontSize = 10;
constexpr int maximumEditorFontSize = 48;
const QString browseDirectorySetting = QStringLiteral("file/browseDirectory");
const QString sidebarWidthSetting = QStringLiteral("window/sidebarWidth");
const QString agentPanelWidthSetting = QStringLiteral("window/agentPanelWidth");

class PreviewDocument final : public QTextDocument {
public:
    explicit PreviewDocument(std::function<void(const QString &)> imageLoaded, QObject *parent)
        : QTextDocument(parent), m_imageLoaded(std::move(imageLoaded)) {}

    void setImageRoot(const QString &root) {
        m_imageRoot = QFileInfo(root).canonicalFilePath();
    }

    bool setImageWidth(int width) {
        const int imageWidth = qMax(1, width);
        if (m_imageWidth == imageWidth)
            return false;
        m_imageWidth = imageWidth;
        return true;
    }

    void setAllowedImages(const QString &markdown, const QUrl &baseUrl) {
        m_allowedImages.clear();
        static const QRegularExpression imageRe(
            QStringLiteral("!\\[[^\\]]*\\]\\((?:<([^>]+)>|([^\\s)]+))(?:\\s+[^)]*)?\\)"));
        QRegularExpressionMatchIterator matches = imageRe.globalMatch(markdown);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            allowImage(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1), baseUrl);
        }

        QHash<QString, QString> references;
        static const QRegularExpression referenceRe(
            QStringLiteral("^\\s{0,3}\\[([^\\]]+)\\]:\\s*(?:<([^>]+)>|([^\\s]+))"),
            QRegularExpression::MultilineOption);
        QRegularExpressionMatchIterator definitions = referenceRe.globalMatch(markdown);
        while (definitions.hasNext()) {
            const QRegularExpressionMatch match = definitions.next();
            references.insert(referenceKey(match.captured(1)),
                              match.captured(2).isEmpty() ? match.captured(3) : match.captured(2));
        }

        static const QRegularExpression referenceImageRe(
            QStringLiteral("!\\[([^\\]]*)\\]\\[([^\\]]*)\\]"));
        matches = referenceImageRe.globalMatch(markdown);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const QString destination = references.value(referenceKey(
                match.captured(2).isEmpty() ? match.captured(1) : match.captured(2)));
            if (!destination.isEmpty())
                allowImage(destination, baseUrl);
        }
    }

protected:
    QVariant loadResource(int type, const QUrl &url) override {
        if (type != QTextDocument::ImageResource)
            return QTextDocument::loadResource(type, url);

        const QString path = QFileInfo(url.toLocalFile()).canonicalFilePath();
        if (!url.isLocalFile() || path.isEmpty() || m_imageRoot.isEmpty()
                || !path.startsWith(m_imageRoot + QLatin1Char('/'))
                || !m_allowedImages.contains(url.toLocalFile())
                || !QFileInfo(path).isFile()) {
            return {};
        }

        QImageReader::setAllocationLimit(32);
        if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
            QFile svg(path);
            if (!svg.open(QIODevice::ReadOnly | QIODevice::Text)
                    || QString::fromUtf8(svg.readAll()).contains(QRegularExpression(
                        QStringLiteral("(?:href|xlink:href)\\s*=\\s*[\\\"'](?!#)|"
                                       "url\\(\\s*[\\\"']?(?!#)|@import|<image\\b|xml-stylesheet")))) {
                return {};
            }
        }
        QImageReader reader(path);
        const QSize size = reader.size();
        if (!size.isValid())
            return {};
        if (size.width() > m_imageWidth)
            reader.setScaledSize(size.scaled(m_imageWidth, size.height(), Qt::KeepAspectRatio));

        const QImage image = reader.read();
        if (image.isNull())
            return {};

        m_imageLoaded(path);
        return image;
    }

private:
    void allowImage(const QString &destination, const QUrl &baseUrl) {
        const QUrl source(destination);
        if (source.isRelative() && source.scheme().isEmpty())
            m_allowedImages.insert(baseUrl.resolved(source).toLocalFile());
    }

    static QString referenceKey(const QString &label) {
        return label.simplified().toCaseFolded();
    }

    std::function<void(const QString &)> m_imageLoaded;
    QString m_imageRoot;
    QSet<QString> m_allowedImages;
    int m_imageWidth = 800;
};

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent)
    : Backend(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), parent) {}

Backend::Backend(const QString &stateDirectory, QObject *parent)
    : QObject(parent), m_bufferSession(stateDirectory) {
    m_editorFontSize = qBound(minimumEditorFontSize,
                              QSettings().value(editorFontSizeSetting,
                                                defaultEditorFontSize).toInt(),
                              maximumEditorFontSize);
    QDir().mkpath(stateDirectory);
    if (!m_bufferSession.restore())
        m_bufferSession.createBuffer();
    for (const QVariant &value : m_bufferSession.buffers()) {
        const QVariantMap buffer = value.toMap();
        if (buffer.value(QStringLiteral("id")).toString() != m_bufferSession.activeBufferId())
            continue;
        m_activeBufferText = buffer.value(QStringLiteral("text")).toString();
        m_cursorPosition = buffer.value(QStringLiteral("cursorPosition")).toInt();
        m_selectionStart = buffer.value(QStringLiteral("selectionStart")).toInt();
        m_selectionEnd = buffer.value(QStringLiteral("selectionEnd")).toInt();
        break;
    }
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    initializeRuntime();
}

Backend::Backend(WorkspaceSession *workspaceSession, const QString &windowId, QObject *parent)
    : QObject(parent), m_bufferSession(QString()), m_workspaceSession(workspaceSession),
      m_workspaceWindowId(windowId) {
    m_editorFontSize = qBound(minimumEditorFontSize,
                              QSettings().value(editorFontSizeSetting,
                                                defaultEditorFontSize).toInt(),
                              maximumEditorFontSize);
    for (const QVariant &value : buffers()) {
        const QVariantMap buffer = value.toMap();
        if (buffer.value(QStringLiteral("id")).toString() != activeBufferId())
            continue;
        m_activeBufferText = buffer.value(QStringLiteral("text")).toString();
        m_cursorPosition = buffer.value(QStringLiteral("cursorPosition")).toInt();
        m_selectionStart = buffer.value(QStringLiteral("selectionStart")).toInt();
        m_selectionEnd = buffer.value(QStringLiteral("selectionEnd")).toInt();
        m_fileUrl = QUrl(buffer.value(QStringLiteral("fileUrl")).toString());
        m_modified = buffer.value(QStringLiteral("modified")).toBool();
        break;
    }
    initializeRuntime();
}

void Backend::initializeRuntime() {
    adoptFillingMeasure();
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(750);
    connect(&m_persistTimer, &QTimer::timeout, this, &Backend::persistDocument);
    connect(&m_previewImageWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (QFile::exists(path))
                    m_previewImageWatcher.addPath(path);
                if (m_previewDocument) {
                    setPreviewMarkdown(m_previewMarkdown);
                    emit previewChanged();
                }
            });
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        watchCurrentFile();
                        return;
                    }
                }

                // Nothing of theirs is at stake when the document has no local
                // changes, so an outside edit is simply the newer text. Taking
                // it without asking is what makes a second writer workable: an
                // agent, a sync client, another window. A deletion still asks,
                // because losing the file is not the same as being handed a
                // newer version of it.
                if (!deleted && !m_modified) {
                    reloadSilently();
                    watchCurrentFile();
                    return;
                }

                setExternalChangePending(true);
                emit externalChangeDetected(deleted, m_modified);
                // A replacement leaves the old inode behind, and the path with
                // it, so re-arm or a second change would never be noticed.
                watchCurrentFile();
            });

    connect(&m_folderWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this]() { emit folderChanged(); });
    const QString remembered = QSettings().value(browseDirectorySetting).toString();
    applyFolder(QDir(remembered).exists() ? remembered
                                          : defaultDirectory().absolutePath(),
                false);

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QWindow *Backend::parentWindow() const {
    return m_parentWindow;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged(m_textScale);
}

void Backend::setEditorFontSize(int editorFontSize) {
    const int boundedSize = qBound(minimumEditorFontSize, editorFontSize,
                                   maximumEditorFontSize);
    if (m_editorFontSize == boundedSize)
        return;

    m_editorFontSize = boundedSize;
    QSettings().setValue(editorFontSizeSetting, m_editorFontSize);
    emit editorFontSizeChanged();
}

void Backend::resetEditorFontSize() {
    setEditorFontSize(defaultEditorFontSize);
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    // Attaching is a restore from here to loadActiveBuffer(): the editor reports
    // an empty document and a caret at nothing while the highlighter and the
    // typography pass run, and writing that back would erase the tab before it
    // has been read.
    m_restoringActiveBuffer = true;
    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    m_highlighter->setTextScale(m_textScale);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
            });

    applyDocumentTypography();
    // The editor is its own document again from here: the guard only covered
    // the highlighter and typography passes above, which report an empty
    // document and a caret at nothing before anything has been read.
    m_restoringActiveBuffer = false;
    // The tab first, then the crash draft over the top of it: a draft is only
    // written when a save could not land, so it is the newer of the two and the
    // one the writer has not seen reach disk.
    loadActiveBuffer();
    restoreRecovery();

    connect(this, &Backend::textScaleChanged, m_highlighter,
            &MarkdownHighlighter::setTextScale);
}

void Backend::attachPreviewDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument) {
        setStatus(QStringLiteral("Could not attach the Markdown preview."));
        return;
    }

    if (!m_previewDocument) {
        m_previewDocument = new PreviewDocument(
            [this](const QString &path) { watchPreviewImage(path); }, this);
    }
    quickDocument->setTextDocument(m_previewDocument);
    setPreviewMarkdown(m_previewMarkdown);
}

void Backend::setPreviewMarkdown(const QString &markdown) {
    m_previewMarkdown = markdown;
    if (!m_previewDocument)
        return;

    const QUrl baseUrl = m_fileUrl.isLocalFile()
        ? QUrl::fromLocalFile(QFileInfo(m_fileUrl.toLocalFile()).absolutePath() + QLatin1Char('/'))
        : QUrl();
    m_previewDocument->setBaseUrl(baseUrl);
    m_previewDocument->setImageRoot(baseUrl.toLocalFile());
    m_previewDocument->setAllowedImages(markdown, baseUrl);
    const QStringList watchedImages = m_previewImageWatcher.files();
    if (!watchedImages.isEmpty())
        m_previewImageWatcher.removePaths(watchedImages);
    m_previewDocument->clear();
    m_previewDocument->setMarkdown(
        markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
                      | QTextDocument::MarkdownNoHTML);
    QFont editorFont = m_previewDocument->defaultFont();
    editorFont.setPixelSize(qRound(m_editorFontSize * m_textScale));
    applyPreviewTypography(m_previewDocument, editorFont);
}

void Backend::setPreviewWidth(int width) {
    if (m_previewDocument && m_previewDocument->setImageWidth(width))
        setPreviewMarkdown(m_previewMarkdown);
}

void Backend::openDialog() {
    emit openDialogRequested();
}

void Backend::setFolder(const QUrl &url) {
    if (!url.isLocalFile())
        return;

    applyFolder(url.toLocalFile(), true);
}

void Backend::openParentFolder() {
    QDir directory(m_folderUrl.toLocalFile());
    if (!directory.cdUp())
        return;

    applyFolder(directory.absolutePath(), true);
}

QUrl Backend::createDocument(const QString &name) {
    const QDir directory(m_folderUrl.toLocalFile());
    const QString fileName = suggestedFileName(name);
    const QString path = directory.filePath(fileName);
    if (QFileInfo::exists(path)) {
        setStatus(QStringLiteral("%1 already exists.").arg(fileName));
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("Could not create %1.").arg(fileName));
        return {};
    }
    file.close();

    setStatus(QStringLiteral("Created %1").arg(fileName));
    // The folder watcher reports this too, but not before the new row is
    // wanted on screen.
    emit folderChanged();
    return QUrl::fromLocalFile(path);
}

QUrl Backend::createFolder(const QString &name) {
    QDir directory(m_folderUrl.toLocalFile());
    const QString folderName = sanitizedEntryName(name);
    if (directory.exists(folderName)) {
        setStatus(QStringLiteral("%1 already exists.").arg(folderName));
        return {};
    }

    if (!directory.mkdir(folderName)) {
        setStatus(QStringLiteral("Could not create %1.").arg(folderName));
        return {};
    }

    setStatus(QStringLiteral("Created %1").arg(folderName));
    emit folderChanged();
    return QUrl::fromLocalFile(directory.filePath(folderName));
}

int Backend::sidebarWidth() const {
    return QSettings().value(sidebarWidthSetting, 240).toInt();
}

void Backend::saveSidebarWidth(int width) {
    QSettings().setValue(sidebarWidthSetting, width);
}

int Backend::agentPanelWidth() const {
    return QSettings().value(agentPanelWidthSetting, 380).toInt();
}

void Backend::saveAgentPanelWidth(int width) {
    QSettings().setValue(agentPanelWidthSetting, width);
}

void Backend::open(const QUrl &url) {
    openPath(url, true);
}

void Backend::openAtLine(const QUrl &url, int line, bool newTab) {
    // A fresh tab first, so the open below takes over that one rather than the
    // document the writer is looking at.
    if (newTab)
        newBuffer();

    openPath(url, true);
    if (line <= 0 || !m_document)
        return;

    // Lines are 1-based where they come from: an editor, a compiler, a grep.
    // Past the end of the document the last line is as close as we can get,
    // which beats refusing to move at all.
    const QTextBlock block = m_document->findBlockByNumber(
        qBound(0, line - 1, qMax(0, m_document->blockCount() - 1)));
    if (!block.isValid())
        return;

    m_cursorPosition = block.position();
    m_selectionStart = m_cursorPosition;
    m_selectionEnd = m_cursorPosition;
    persistActiveBuffer();
    emit activeBufferChanged();
}

void Backend::openPath(const QUrl &url, bool mayStartNewFile) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    // A file some tab already holds is brought forward rather than opened twice.
    if (m_workspaceSession) {
        const QString openTabId = m_workspaceSession->findOpenLocalFile(url);
        if (!openTabId.isEmpty()) {
            emit openTabRequested(openTabId);
            return;
        }
    } else {
        for (const QVariant &value : m_bufferSession.buffers()) {
            const QVariantMap buffer = value.toMap();
            if (buffer.value(QStringLiteral("fileUrl")).toString() != url.toString())
                continue;
            m_bufferSession.selectBuffer(buffer.value(QStringLiteral("id")).toString());
            loadActiveBuffer();
            emit buffersChanged();
            emit activeBufferChanged();
            setStatus(QStringLiteral("Opened %1").arg(fileName()));
            return;
        }
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    // A path that is not there yet is a file the writer means to start, so
    // take the name for a blank document. The first save then lands where
    // they said it should, instead of asking them again.
    if (mayStartNewFile && !file.exists()) {
        // Only where it could be written: a name under a directory that is not
        // there leaves the first save with nowhere to land and no dialog.
        const QFileInfo parentDirectory(QFileInfo(url.toLocalFile()).absolutePath());
        if (!parentDirectory.isDir() || !parentDirectory.isWritable()) {
            setStatus(QStringLiteral("Could not open %1.").arg(targetName));
            return;
        }

        persistActiveBuffer();
        if (m_workspaceSession) {
            const QString reuse = m_workspaceSession->activeTabId(m_workspaceWindowId);
            if (reuse.isEmpty())
                m_workspaceSession->createTab(m_workspaceWindowId, url, QString(), 0, 0, 0, false);
            else
                m_workspaceSession->updateTab(m_workspaceWindowId, reuse, url, QString(),
                                              0, 0, 0, false);
            m_workspaceSession->saveNow();
        } else {
            QString reuse = m_bufferSession.activeBufferId();
            if (reuse.isEmpty())
                reuse = m_bufferSession.createBuffer();
            m_bufferSession.updateBuffer(reuse, url.toString(), QString(), 0, 0, 0, false);
            m_bufferSession.saveNow();
        }
        loadActiveBuffer();
        clearRecovery();
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
        m_pathNeverRead = true;
        emit buffersChanged();
        emit activeBufferChanged();
        setStatus(QStringLiteral("New file %1").arg(fileName()));
        return;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // A reload with nothing left to read leaves this document holding a
        // name and no file, and the watcher let the path go when it went.
        // That is the state a new file starts in, so say so: if the file
        // comes back, the next save asks rather than replacing it unseen.
        if (!mayStartNewFile && !file.exists())
            m_pathNeverRead = true;

        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    // Only now, with the new text in hand: whatever was contested belonged to
    // the document being replaced, and an open that failed replaces nothing.
    setExternalChangePending(false);
    persistActiveBuffer();

    // Opening takes over the tab that is showing rather than adding one. The
    // sidebar switches documents freely and autosave has already written
    // whatever the tab held, so a tab per file opened would turn a morning's
    // browsing into thirty tabs waiting at the next start. Ctrl+T is what adds
    // a tab, and that is the only thing that does.
    const QString text = QString::fromUtf8(contents);
    if (m_workspaceSession) {
        const QString reuse = m_workspaceSession->activeTabId(m_workspaceWindowId);
        if (reuse.isEmpty())
            m_workspaceSession->createTab(m_workspaceWindowId, url, text, 0, 0, 0, false);
        else
            m_workspaceSession->updateTab(m_workspaceWindowId, reuse, url, text, 0, 0, 0, false);
    } else {
        QString reuse = m_bufferSession.activeBufferId();
        if (reuse.isEmpty())
            reuse = m_bufferSession.createBuffer();
        m_bufferSession.updateBuffer(reuse, url.toString(), text, 0, 0, 0, false);
    }
    loadActiveBuffer();
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    if (m_workspaceSession)
        m_workspaceSession->saveNow();
    else
        m_bufferSession.saveNow();
    emit buffersChanged();
    emit activeBufferChanged();
    m_pathNeverRead = false;
    watchCurrentFile();
    setStatus(QStringLiteral("Opened %1").arg(fileName()));
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    // Nothing can watch a file that is not there, so a name taken for a file
    // that has yet to be written is unguarded until this save: a `git pull` or
    // a sync client can put something on that path in the meantime and
    // QSaveFile::commit() would replace it without a word. Ask once, and only
    // once -- the flag is cleared by every answer the dialog can give, so a
    // file that turns out to be unreadable cannot leave the writer trapped in
    // a question they have already answered.
    if (m_pathNeverRead && m_fileUrl.isLocalFile()
            && QFileInfo::exists(m_fileUrl.toLocalFile())) {
        m_closeAfterSave = false;
        emit externalFileAppeared(m_modified);
        return;
    }

    // A dismissal left the question open, and an explicit save is the writer
    // answering it. With the file gone there is no other version to weigh
    // against, so the save writes it back; with one on disk the prompt comes
    // again rather than overwriting it unasked. Autosave never reaches here,
    // so neither happens behind the writer's back.
    if (m_externalChangeDismissed) {
        if (m_fileUrl.isLocalFile() && !QFileInfo::exists(m_fileUrl.toLocalFile())) {
            setExternalChangePending(false);
            if (m_workspaceSession)
                m_workspaceSession->setExternalChange(activeBufferId(), false);
        } else {
            emit externalChangeDetected(false, m_modified);
            return;
        }
    }

    saveTo(m_fileUrl);
}

void Backend::saveNow() {
    m_persistTimer.stop();
    persistDocument();
}

// The file the document belongs in: its own if it has one, one named from its
// first line if it does not. False when the write did not land there.
bool Backend::saveToItsOwnFile() {
    if (!m_modified)
        return true;

    if (m_fileUrl.isLocalFile())
        return saveTo(m_fileUrl);

    const QString text = currentDocumentText();
    if (text.trimmed().isEmpty()) {
        clearRecovery();
        setModified(false);
        return true;
    }

    return saveTo(unusedDocumentUrl(suggestedFileName(text)));
}

// Switching documents asks the strict question: the work has to have reached
// the file it belongs in, because the writer is about to lose sight of it.
bool Backend::saveBeforeLeaving() {
    m_persistTimer.stop();

    if (saveToItsOwnFile())
        return true;

    // Worth a draft even though the switch is declined.
    writeRecovery();
    return false;
}

// Closing asks the weaker one: anywhere at all will do. It has to be the draft
// written for this attempt, though — an older one on disk proves only that
// something was saved once, not that it holds what is on screen now.
bool Backend::saveBeforeClosing() {
    m_persistTimer.stop();

    if (saveToItsOwnFile() || writeRecovery())
        return true;

    setStatus(QStringLiteral("Could not save %1 anywhere; close again to discard.")
                  .arg(fileName()));
    return false;
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::reloadSilently() {
    QFile file(m_fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QByteArray contents = file.readAll();
    const QString text = QString::fromUtf8(contents);

    // The caret is the reader's place in the document, so it survives the
    // reload rather than snapping to the top of text they did not ask for.
    // Clamped, because the newer version may be shorter than where they were.
    const int caret = qBound(0, m_cursorPosition, int(text.size()));

    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setExternalChangePending(false);

    if (m_workspaceSession) {
        m_workspaceSession->updateTab(m_workspaceWindowId, activeBufferId(), m_fileUrl,
                                      text, caret, caret, caret, false);
        m_workspaceSession->setExternalChange(activeBufferId(), false);
        m_workspaceSession->saveNow();
    } else {
        m_bufferSession.updateBuffer(m_bufferSession.activeBufferId(), m_fileUrl.toString(),
                                     text, caret, caret, caret, false);
        m_bufferSession.saveNow();
    }

    loadActiveBuffer();
    clearRecovery();
    emit buffersChanged();
    emit activeBufferChanged();
    setStatus(QStringLiteral("Updated %1 from disk").arg(fileName()));
}

void Backend::reloadFromDisk() {
    if (m_workspaceSession) {
        QFile file(m_fileUrl.toLocalFile());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            setStatus(QStringLiteral("Could not reload %1.").arg(fileName()));
            return;
        }
        const QByteArray contents = file.readAll();
        m_workspaceSession->updateTab(m_workspaceWindowId, activeBufferId(), m_fileUrl,
                                      QString::fromUtf8(contents), 0, 0, 0, false);
        m_workspaceSession->setExternalChange(activeBufferId(), false);
        m_workspaceSession->saveNow();
        m_lastKnownFileContents = contents;
        m_hasKnownFileContents = true;
        loadActiveBuffer();
        emit buffersChanged();
        emit activeBufferChanged();
        setStatus(QStringLiteral("Reloaded %1").arg(fileName()));
        return;
    }

    // Not open(): that brings forward the tab already holding this path, and
    // this tab is that tab. Reloading means taking what is on disk now.
    if (m_fileUrl.isLocalFile()) {
        QFile file(m_fileUrl.toLocalFile());
        // A reload that could not read leaves the conflict standing, so it falls
        // through to the question below rather than returning on the spot.
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray contents = file.readAll();
            setExternalChangePending(false);
            m_bufferSession.updateBuffer(m_bufferSession.activeBufferId(), m_fileUrl.toString(),
                                         QString::fromUtf8(contents), 0, 0, 0, false);
            m_bufferSession.saveNow();
            m_lastKnownFileContents = contents;
            m_hasKnownFileContents = true;
            loadActiveBuffer();
            clearRecovery();
            watchCurrentFile();
            emit buffersChanged();
            emit activeBufferChanged();
            setStatus(QStringLiteral("Reloaded %1").arg(fileName()));
        } else {
            // A reload with nothing left to read leaves this document holding a
            // name and no file, and the watcher let the path go when it went.
            // That is the state a new file starts in, so say so: if the file
            // comes back, the next save asks rather than replacing it unseen.
            if (!file.exists())
                m_pathNeverRead = true;
            setStatus(QStringLiteral("Could not open %1.").arg(fileName()));
        }
    }

    // A reload that did not happen has answered nothing, and the prompt that
    // asked has already closed itself. Ask again rather than leave the guard
    // standing with nothing able to clear it.
    if (m_externalChangePending)
        emit externalChangeDetected(!QFileInfo::exists(m_fileUrl.toLocalFile()),
                                    m_modified);
    m_pathNeverRead = false;
    watchCurrentFile();
}

void Backend::keepExternalVersion() {
    setExternalChangePending(false);
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    // Answered, whether or not the file could be read. Failing to read it is
    // not a reason to ask again: the writer said to keep their version, and
    // the next save must be allowed to try, so the filesystem gets to give
    // the answer instead of the dialog asking the same question forever.
    m_pathNeverRead = false;
    setModified(true);
    if (m_workspaceSession)
        m_workspaceSession->setExternalChange(activeBufferId(), false);
    schedulePersist();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

// Escape, or a click outside: the way a popup is dismissed everywhere else in
// Omarchy. It answers nothing, so the guard stays up — the file is not written
// over, the text keeps going to the recovery draft, and the tab keeps the dot
// that says the question is open. For a file that was removed that is the
// whole answer: the removal stands and the writing carries on.
void Backend::dismissExternalChange() {
    if (!m_externalChangePending)
        return;

    m_externalChangeDismissed = true;
    const bool deleted = m_fileUrl.isLocalFile()
        && !QFileInfo::exists(m_fileUrl.toLocalFile());
    setStatus(deleted
                  ? QStringLiteral("Left %1 deleted; Ctrl+S writes it again")
                        .arg(fileName())
                  : QStringLiteral("Left %1 alone; Ctrl+S asks again").arg(fileName()));
}

QFont Backend::printFont(const QFont &editorFont, qreal screenDpi) {
    // The editor sizes its font in pixels, which a printer takes as device dots.
    // Convert to points at the screen's DPI so the page matches the editor.
    QFont font = editorFont;
    if (font.pixelSize() <= 0)
        return font;

    const qreal dpi = screenDpi > 0.0 ? screenDpi : 96.0;
    font.setPointSizeF(font.pixelSize() * 72.0 / dpi);
    return font;
}

void Backend::reportExternalChange(bool deleted) {
    emit buffersChanged();

    // The same rule the document's own watcher follows: with no local changes
    // there is nothing to weigh the newer text against, so it is taken rather
    // than asked about. Two paths reach here -- the window manager watching
    // every tab's file, and the tab strip noticing a background change as you
    // switch to it -- and both have to answer the same way, or the rule holds
    // only for whichever watcher happened to fire first.
    if (!deleted && !m_modified && m_fileUrl.isLocalFile()
            && QFileInfo::exists(m_fileUrl.toLocalFile())) {
        reloadSilently();
        return;
    }

    emit externalChangeDetected(deleted, m_modified);
}

void Backend::refreshBuffers() {
    emit buffersChanged();
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        const QScreen *screen = m_parentWindow ? m_parentWindow->screen()
                                               : QGuiApplication::primaryScreen();
        rendered.setDefaultFont(printFont(m_document->defaultFont(),
                                          screen ? screen->logicalDotsPerInchY() : 0.0));
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}

void Backend::newWindow() {
    if (m_workspaceSession) {
        emit newWindowRequested();
        return;
    }

    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 QStringList());
    if (!started)
        setStatus(QStringLiteral("Could not open a new window."));
}

// The folder a pasted image lands in, beside the document that refers to it.
// A relative path under the document's own folder is also the only shape the
// preview will load, so writing anywhere else would insert a broken image.
const QString pastedImageFolder = QStringLiteral("images");

// An image the clipboard carries as a file rather than as pixels, which is what
// a file manager or a browser's "Copy Image" puts there. Returns its path.
static QString clipboardImageFile(const QMimeData *mimeData) {
    if (!mimeData->hasUrls())
        return {};

    const QByteArrayList readable = QImageReader::supportedImageFormats();
    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile())
            continue;
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile())
            continue;
        if (readable.contains(info.suffix().toLower().toUtf8()))
            return info.absoluteFilePath();
    }
    return {};
}

// A name nothing in the folder answers to yet. The clock names the image so
// pastes stay in the order they were made; a counter settles the collisions
// that a clock at one second's resolution still leaves.
static QString unusedImageName(const QDir &folder, const QString &suffix) {
    const QString stamp = QStringLiteral("pasted-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString name = stamp + QLatin1Char('.') + suffix;
    for (int attempt = 2; folder.exists(name); ++attempt)
        name = stamp + QLatin1Char('-') + QString::number(attempt) + QLatin1Char('.') + suffix;
    return name;
}

QString Backend::saveClipboardImage() {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mimeData = clipboard ? clipboard->mimeData() : nullptr;
    if (!mimeData)
        return {};

    const QString sourceFile = clipboardImageFile(mimeData);
    if (!mimeData->hasImage() && sourceFile.isEmpty())
        return {};

    // An untitled document has no folder of its own, so an image pasted into it
    // would have nowhere to live that the document could still point at once it
    // is saved somewhere else.
    if (!m_fileUrl.isLocalFile()) {
        setStatus(QStringLiteral("Save the document first, then paste the image."));
        return {};
    }

    QDir folder(QFileInfo(m_fileUrl.toLocalFile()).absolutePath());
    if (!folder.exists(pastedImageFolder) && !folder.mkpath(pastedImageFolder)) {
        setStatus(QStringLiteral("Could not make an %1 folder.").arg(pastedImageFolder));
        return {};
    }
    const QDir imageFolder(folder.filePath(pastedImageFolder));

    QString name;
    bool written = false;
    if (!sourceFile.isEmpty()) {
        // Copying keeps the original encoding, which re-encoding to PNG would
        // throw away along with any compression the file already had.
        name = unusedImageName(imageFolder, QFileInfo(sourceFile).suffix().toLower());
        written = QFile::copy(sourceFile, imageFolder.filePath(name));
    } else {
        const QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (image.isNull()) {
            setStatus(QStringLiteral("The clipboard image could not be read."));
            return {};
        }
        name = unusedImageName(imageFolder, QStringLiteral("png"));
        written = image.save(imageFolder.filePath(name), "PNG");
    }

    if (!written) {
        setStatus(QStringLiteral("Could not write %1.").arg(name));
        return {};
    }

    const QString relativePath = pastedImageFolder + QLatin1Char('/') + name;
    setStatus(QStringLiteral("Pasted %1").arg(relativePath));
    return relativePath;
}

// The block lives on a cursor but belongs to the document: everything the
// editor's own cursors do while it is open lands in one undo command.
void Backend::beginUndoBlock() {
    if (!m_document || !m_undoBlock.isNull())
        return;
    m_undoBlock = QTextCursor(m_document);
    m_undoBlock.beginEditBlock();
}

void Backend::endUndoBlock() {
    if (m_undoBlock.isNull())
        return;
    m_undoBlock.endEditBlock();
    m_undoBlock = QTextCursor();
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged() {
    if (!m_document || m_applicationClosing || m_restoringActiveBuffer || m_loading
            || m_formattingTypography)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;
    m_lastDocumentText = text;

    if (m_document) {
        const int blockCount = m_document->blockCount();
        if (blockCount > m_formattedBlockCount)
            reapplyTypographyToChange();
        m_formattedBlockCount = blockCount;
    }

    scheduleWordCount();
    setModified(true);
    setStatus(QStringLiteral("Unsaved"));
    schedulePersist();
    return true;
}

QVariantList Backend::hiddenRangesAt(int position) const {
    QVariantList ranges;
    if (!m_document)
        return ranges;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return ranges;

    const int lineStart = block.position();
    QList<QPair<int, int>> spans;
    static const QRegularExpression checkboxRe(
        QStringLiteral("^(\\s*[-+*]\\s+)(\\[[ xX]\\])(?=\\s|$)"));
    const QRegularExpressionMatch checkbox = checkboxRe.match(block.text());
    if (checkbox.hasMatch()) {
        spans.append({lineStart + checkbox.capturedStart(2),
                      lineStart + checkbox.capturedStart(2) + checkbox.capturedLength(2)});
    }
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(block.text());
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        for (const MarkdownHighlighter::Span &marker : item.markers) {
            spans.append({lineStart + marker.start,
                          lineStart + marker.start + marker.length});
        }
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.first},
                                  {QStringLiteral("end"), span.second}});
    }
    return ranges;
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto") || scheme == QStringLiteral("file"))
        QDesktopServices::openUrl(url);
}

static QString vaultRootFor(const QString &filePath) {
    if (!filePath.isEmpty()) {
        QDir dir = QFileInfo(filePath).absoluteDir();
        while (true) {
            if (QDir(dir.filePath(QStringLiteral(".obsidian"))).exists())
                return dir.absolutePath();
            const QString current = dir.absolutePath();
            if (!dir.cdUp() || dir.absolutePath() == current)
                break;
        }
        if (!filePath.isEmpty())
            return QFileInfo(filePath).absolutePath();
    }
    const QString notes = QDir::home().filePath(QStringLiteral("Notes"));
    if (QDir(notes).exists())
        return notes;
    return {};
}

QString Backend::resolveWikilinkPath(const QString &currentFile, const QString &rawTarget) {
    QString target = rawTarget.trimmed();
    if (target.isEmpty() || target.contains(QStringLiteral("..")))
        return {};

    if (target.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        target.chop(3);

    const QString withMd = target + QStringLiteral(".md");
    const QString currentDir = currentFile.isEmpty()
        ? vaultRootFor({})
        : QFileInfo(currentFile).absolutePath();
    if (!currentDir.isEmpty()) {
        const QFileInfo beside(QDir(currentDir).filePath(withMd));
        if (beside.exists() && beside.isFile())
            return beside.canonicalFilePath();
    }

    const QString vault = vaultRootFor(currentFile);
    if (vault.isEmpty())
        return {};

    const QFileInfo nested(QDir(vault).filePath(withMd));
    if (nested.exists() && nested.isFile())
        return nested.canonicalFilePath();

    const QString needle = QFileInfo(withMd).fileName();
    QString match;
    QDirIterator it(vault, QStringList{QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileName().compare(needle, Qt::CaseInsensitive) != 0)
            continue;
        if (!match.isEmpty() && match != it.fileInfo().canonicalFilePath())
            return {};
        match = it.fileInfo().canonicalFilePath();
    }
    return match;
}

void Backend::notifyMissingNote(const QString &target) {
    setStatus(QStringLiteral("No note named %1").arg(target));
}

QVariantMap Backend::linkAt(int position) const {
    QVariantMap none{{QStringLiteral("kind"), QStringLiteral("none")}};
    if (!m_document)
        return none;

    const int clamped = qBound(0, position, qMax(0, m_document->characterCount() - 1));
    const QTextBlock block = m_document->findBlock(clamped);
    if (!block.isValid())
        return none;

    const int relative = clamped - block.position();
    const QList<MarkdownHighlighter::Clickable> spans =
        MarkdownHighlighter::clickableSpans(block.text());
    for (const MarkdownHighlighter::Clickable &item : spans) {
        if (relative < item.span.start || relative >= item.span.start + item.span.length)
            continue;

        if (item.kind == MarkdownHighlighter::InlineKind::WikiLink) {
            const QString path = resolveWikilinkPath(m_fileUrl.toLocalFile(), item.target);
            if (path.isEmpty()) {
                return {{QStringLiteral("kind"), QStringLiteral("missing")},
                        {QStringLiteral("target"), item.target}};
            }
            return {{QStringLiteral("kind"), QStringLiteral("file")},
                    {QStringLiteral("url"), QUrl::fromLocalFile(path)},
                    {QStringLiteral("target"), item.target}};
        }

        const QString destination = item.target.trimmed();
        if (destination.isEmpty())
            return none;

        QUrl url(destination);
        if (url.isRelative() || url.scheme().isEmpty()) {
            const QString currentDir = m_fileUrl.isLocalFile()
                ? QFileInfo(m_fileUrl.toLocalFile()).absolutePath()
                : vaultRootFor({});
            const QString localPath = QDir(currentDir).filePath(destination);
            const QFileInfo info(localPath);
            if (info.exists()) {
                const QString suffix = info.suffix().toLower();
                if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")
                        || suffix == QStringLiteral("txt")) {
                    return {{QStringLiteral("kind"), QStringLiteral("file")},
                            {QStringLiteral("url"), QUrl::fromLocalFile(info.canonicalFilePath())}};
                }
                return {{QStringLiteral("kind"), QStringLiteral("url")},
                        {QStringLiteral("url"), QUrl::fromLocalFile(info.canonicalFilePath())}};
            }
            url = QUrl::fromUserInput(destination);
        }

        const QString scheme = url.scheme().toLower();
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
                || scheme == QStringLiteral("mailto")) {
            return {{QStringLiteral("kind"), QStringLiteral("url")},
                    {QStringLiteral("url"), url}};
        }
        if (scheme == QStringLiteral("file") && url.isLocalFile()) {
            const QFileInfo info(url.toLocalFile());
            const QString suffix = info.suffix().toLower();
            if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown")
                    || suffix == QStringLiteral("txt")) {
                return {{QStringLiteral("kind"), QStringLiteral("file")},
                        {QStringLiteral("url"), url}};
            }
            return {{QStringLiteral("kind"), QStringLiteral("url")}, {QStringLiteral("url"), url}};
        }
        return none;
    }
    return none;
}

QVariantMap Backend::windowGeometry() const {
    if (m_workspaceSession)
        return m_workspaceSession->window(m_workspaceWindowId);

    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1)},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1)},
            {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
            {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
            {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    if (m_workspaceSession) {
        m_workspaceSession->updateWindowGeometry(m_workspaceWindowId, x, y, width, height,
                                                  maximized);
        m_workspaceSession->saveNow();
        return;
    }

    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::toggleFocusMode() {
    m_focusMode = !m_focusMode;
    if (m_highlighter)
        m_highlighter->setFocusMode(m_focusMode);
    emit focusModeChanged();
}

void Backend::updateCursorPosition(int position) {
    if (m_highlighter)
        m_highlighter->setFocusCursorPosition(position);
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
    emit documentLoaded();
}

QString Backend::bufferTitle(const QVariantMap &buffer, int index) const {
    const QUrl fileUrl(buffer.value(QStringLiteral("fileUrl")).toString());
    if (fileUrl.isLocalFile()) {
        const QString fileName = QFileInfo(fileUrl.toLocalFile()).fileName();
        if (!fileName.isEmpty())
            return fileName;
    }

    const QString firstLine = buffer.value(QStringLiteral("text")).toString()
        .section(QLatin1Char('\n'), 0, 0).trimmed();
    if (firstLine.isEmpty())
        return QStringLiteral("Untitled %1").arg(index + 1);

    constexpr int maximumTitleLength = 29;
    if (firstLine.size() <= maximumTitleLength)
        return firstLine;
    return firstLine.left(maximumTitleLength - 1) + QChar(0x2026);
}

QString Backend::newBuffer() {
    persistActiveBuffer();
    const QString id = m_workspaceSession
        ? m_workspaceSession->createTab(m_workspaceWindowId, QUrl(), QString(), 0, 0, 0, false)
        : m_bufferSession.createBuffer();
    loadActiveBuffer();
    emit buffersChanged();
    emit activeBufferChanged();
    return id;
}

bool Backend::selectBuffer(const QString &id) {
    persistActiveBuffer();
    if (m_workspaceSession ? !m_workspaceSession->setActiveTab(m_workspaceWindowId, id)
                           : !m_bufferSession.selectBuffer(id))
        return false;
    loadActiveBuffer();
    emit activeBufferChanged();
    return true;
}

bool Backend::moveActiveBuffer(int direction) {
    if (!m_workspaceSession || !m_workspaceSession->moveActiveTab(m_workspaceWindowId, direction))
        return false;

    m_workspaceSession->saveNow();
    emit buffersChanged();
    return true;
}

bool Backend::closeActiveBuffer() {
    persistActiveBuffer();
    if (m_modified) {
        setStatus(QStringLiteral("Save or discard changes before closing this tab"));
        return false;
    }
    return discardActiveBuffer();
}

bool Backend::discardActiveBuffer() {
    if (m_workspaceSession ? !m_workspaceSession->removeTab(m_workspaceWindowId, activeBufferId())
                           : !m_bufferSession.closeBuffer(m_bufferSession.activeBufferId()))
        return false;
    loadActiveBuffer();
    if (m_workspaceSession)
        m_workspaceSession->saveNow();
    else
        m_bufferSession.saveNow();
    emit buffersChanged();
    emit activeBufferChanged();
    if (m_workspaceSession && buffers().isEmpty())
        emit windowEmptied();
    return true;
}

void Backend::prepareForApplicationClose() {
    if (m_applicationClosing)
        return;

    persistActiveBuffer();
    m_applicationClosing = true;
}

void Backend::finishActiveBufferRestore() {
    m_restoringActiveBuffer = false;
    QTimer::singleShot(250, this, [this]() { m_ignoringInitialCursorReset = false; });
}

void Backend::updateActiveEditorState(int cursorPosition, int selectionStart, int selectionEnd) {
    if (!m_document || m_applicationClosing || m_restoringActiveBuffer)
        return;
    if (m_ignoringInitialCursorReset && cursorPosition == 0 && m_cursorPosition > 0)
        return;

    m_cursorPosition = cursorPosition;
    m_selectionStart = selectionStart;
    m_selectionEnd = selectionEnd;
    persistActiveBuffer();
}

void Backend::loadActiveBuffer() {
    for (const QVariant &value : buffers()) {
        const QVariantMap buffer = value.toMap();
        if (buffer.value(QStringLiteral("id")).toString() != activeBufferId())
            continue;
        m_cursorPosition = buffer.value(QStringLiteral("cursorPosition")).toInt();
        m_selectionStart = buffer.value(QStringLiteral("selectionStart")).toInt();
        m_selectionEnd = buffer.value(QStringLiteral("selectionEnd")).toInt();
        m_activeBufferText = buffer.value(QStringLiteral("text")).toString();
        m_restoringActiveBuffer = true;
        m_ignoringInitialCursorReset = true;
        loadDocumentText(m_activeBufferText);
        setFileUrl(QUrl(buffer.value(QStringLiteral("fileUrl")).toString()));
        setModified(buffer.value(QStringLiteral("modified")).toBool());
        // The text is in the document by now, so typing counts again at once.
        // Only the caret is still unrestored, and m_ignoringInitialCursorReset
        // is what holds that; keeping the wider guard up until the restore timer
        // comes back would drop whatever was typed in between.
        m_restoringActiveBuffer = false;
        return;
    }

    m_activeBufferText.clear();
    m_cursorPosition = 0;
    m_selectionStart = 0;
    m_selectionEnd = 0;
    m_restoringActiveBuffer = false;
    setFileUrl(QUrl());
    setModified(false);
}

void Backend::persistActiveBuffer() {
    // prepareForApplicationClose() takes the last snapshot and then raises this
    // flag. What follows is teardown: the editor is being pulled apart and the
    // empty text it reports on the way out is not what the tab held.
    // No document attached yet means there is nothing to snapshot: the editor
    // signals its empty initial caret before the restore has run, and writing
    // that back would erase the very text about to be loaded.
    if (!m_document || m_applicationClosing || activeBufferId().isEmpty())
        return;
    m_activeBufferText = currentDocumentText();
    if (m_workspaceSession) {
        m_workspaceSession->updateTab(m_workspaceWindowId, activeBufferId(), m_fileUrl,
                                      m_activeBufferText, m_cursorPosition, m_selectionStart,
                                      m_selectionEnd, m_modified);
        m_workspaceSession->saveNow();
    } else {
        m_bufferSession.updateBuffer(m_bufferSession.activeBufferId(), m_fileUrl.toString(),
                                     m_activeBufferText, m_cursorPosition, m_selectionStart,
                                     m_selectionEnd, m_modified);
        m_bufferSession.saveNow();
    }
    emit buffersChanged();
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit fileUrlChanged();
    watchCurrentFile();
    if (m_fileUrl.isLocalFile()) {
        // A restored tab can name a folder that has since gone. applyFolder
        // answers that with the home directory, which then becomes where an
        // untitled document saves itself -- so leave the browse folder where it
        // was rather than move it somewhere nobody asked for.
        const QString parent = QFileInfo(m_fileUrl.toLocalFile()).absolutePath();
        if (QDir(parent).exists())
            applyFolder(parent, true);
    }
    if (m_previewDocument)
        setPreviewMarkdown(m_previewMarkdown);
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setExternalChangePending(bool pending) {
    m_externalChangePending = pending;
    m_externalChangeDismissed = false;
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

// NotOpened is kept apart from Failed because only it leaves the target
// definitely untouched, and only then is a destructive retry safe.
enum class AtomicWrite { Written, Failed, NotOpened };

static AtomicWrite writeDocumentAtomically(const QString &path, const QByteArray &contents)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return AtomicWrite::NotOpened;
    if (file.write(contents) != contents.size()) {
        file.cancelWriting();
        return AtomicWrite::Failed;
    }
    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    return file.commit() ? AtomicWrite::Written : AtomicWrite::Failed;
}

static bool writeDocumentDirectly(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size() && file.flush();
}

bool Backend::saveTo(const QUrl &url) {
    // The prompt is on screen asking which version to keep, so the file is not
    // ours to write until it is answered. Saving somewhere else is still fine.
    if (m_externalChangePending && url == m_fileUrl) {
        setStatus(QStringLiteral("%1 changed on disk; answer that first.")
                      .arg(fileName()));
        return false;
    }

    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return false;
    }

    // Two tabs writing to one file would each overwrite the other, so the
    // save is refused and the tab already holding it is brought forward.
    if (m_workspaceSession) {
        const QString openTabId = m_workspaceSession->findOpenLocalFile(url);
        if (!openTabId.isEmpty() && openTabId != activeBufferId()) {
            setStatus(QStringLiteral("This file is already open."));
            emit openTabRequested(openTabId);
            return false;
        }
    }

    const QString path = url.toLocalFile();
    const QString targetName = QFileInfo(path).fileName();
    const QByteArray contents = currentDocumentText().toUtf8();

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // QSaveFile uses Linux O_TMPFILE. CIFS/SMB returns ENOENT for that instead
    // of EOPNOTSUPP, so Qt never falls back to a named temp file and open()
    // fails. Direct write still works on those mounts. Retry only that case:
    // once QSaveFile has opened, the document on disk survives any later error,
    // and truncating it to try again would destroy what the failure spared.
    // A folder we may not write to refuses the temp file for a reason a direct
    // write cannot answer, so that refusal is reported rather than worked
    // around — the atomic write is given up only where it was never possible.
    const AtomicWrite atomic = writeDocumentAtomically(path, contents);
    const bool mayRetryDirectly = atomic == AtomicWrite::NotOpened
        && QFileInfo(QFileInfo(path).absolutePath()).isWritable();
    if (atomic == AtomicWrite::Failed
        || (atomic == AtomicWrite::NotOpened
            && (!mayRetryDirectly || !writeDocumentDirectly(path, contents)))) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return false;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    m_pathNeverRead = false;
    setFileUrl(url);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    persistActiveBuffer();
    emit saveSucceeded();

    if (shouldClose)
        emit closeAfterSave();

    return true;
}

void Backend::schedulePersist() {
    m_persistTimer.start();
}

// A named document is written to its file. Anything that stops that — an
// unwritable file, or an outside change the writer has not answered yet, which
// is not ours to overwrite — falls back to the recovery draft, so quitting
// after a failed save still comes back.
void Backend::persistDocument() {
    if (m_modified && !(m_fileUrl.isLocalFile() && saveTo(m_fileUrl)))
        writeRecovery();

    // Written after the save rather than before it, so the tab records the state
    // the save left behind instead of the one it was about to change. A caret
    // that moved is worth keeping even where there was nothing new to write.
    persistActiveBuffer();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

bool Backend::writeRecovery() {
    if (!m_modified)
        return true;
    const QString path = recoveryPath();
    if (path.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("pathNeverRead"), m_pathNeverRead},
                               {QStringLiteral("text"), currentDocumentText()}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    return file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
        // Reading it now says what is on the path, not that this document ever
        // looked: the file can have arrived while Omawrite was gone. Only the
        // snapshot knows, so a snapshot without the key predates the flag and
        // names a path something was written to.
        m_pathNeverRead = recovery.value(QStringLiteral("pathNeverRead")).toBool();
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
        // A snapshot can name a file that was never written -- the crash came
        // first. That is the same unverified path a new file starts on.
        m_pathNeverRead = true;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_persistTimer.stop();
    QFile::remove(recoveryPath());
}

void Backend::watchCurrentFile() {
    if (m_workspaceSession)
        return;

    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

void Backend::applyFolder(const QString &path, bool remember) {
    const QDir directory = QDir(path).exists() ? QDir(path) : defaultDirectory();
    const QUrl folderUrl = QUrl::fromLocalFile(directory.absolutePath());
    if (m_folderUrl == folderUrl)
        return;

    m_folderUrl = folderUrl;
    watchCurrentFolder();
    if (remember)
        QSettings().setValue(browseDirectorySetting, directory.absolutePath());
    emit folderChanged();
}

void Backend::watchCurrentFolder() {
    const QStringList watched = m_folderWatcher.directories();
    if (!watched.isEmpty())
        m_folderWatcher.removePaths(watched);
    if (m_folderUrl.isLocalFile())
        m_folderWatcher.addPath(m_folderUrl.toLocalFile());
}

QString Backend::folderName() const {
    const QDir directory(m_folderUrl.toLocalFile());
    // The root directory has no name of its own; show its path instead.
    return directory.isRoot() ? directory.absolutePath() : directory.dirName();
}

bool Backend::folderHasParent() const {
    return !QDir(m_folderUrl.toLocalFile()).isRoot();
}

QVariantList Backend::folderEntries() const {
    static const QStringList markdownFilter{QStringLiteral("*.md"),
                                            QStringLiteral("*.markdown")};
    // AllDirs exempts folders from the name filter so an empty one can still
    // be walked into, while files stay narrowed to what Omawrite can open:
    // this is a view of a writing folder, not a file manager.
    QVariantList entries;
    const QFileInfoList infos = QDir(m_folderUrl.toLocalFile())
        .entryInfoList(markdownFilter, QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot,
                       QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    entries.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        entries.append(QVariantMap{
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("url"), QUrl::fromLocalFile(info.absoluteFilePath())},
            {QStringLiteral("isDir"), info.isDir()}});
    }
    return entries;
}

void Backend::watchPreviewImage(const QString &path) {
    if (!m_previewImageWatcher.files().contains(path))
        m_previewImageWatcher.addPath(path);
}

// A quoted TOML value ends at its closing quote; whatever trails it is an inline comment.
QString Backend::tomlValue(const QString &text) {
    const QString value = text.trimmed();
    if (value.isEmpty())
        return value;

    const QChar quote = value.front();
    if (quote != QLatin1Char('"') && quote != QLatin1Char('\''))
        return value;

    const int close = value.indexOf(quote, 1);
    return close < 0 ? value : value.mid(1, close - 1);
}

void Backend::loadOmarchyTheme() {
    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QString background;
    QString foreground;
    QString accent;
    QString selection;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            const QString value = tomlValue(line.mid(equals + 1));

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                background = value;
            else if (key == QStringLiteral("foreground"))
                foreground = value;
            else if (key == QStringLiteral("accent"))
                accent = value;
            else if (key == QStringLiteral("selection"))
                selection = value;
        }
    }

    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
    } else {
        const QColor parsedBackground(background);
        if (parsedBackground.isValid()) {
            const double luminance = 0.299 * parsedBackground.redF()
                + 0.587 * parsedBackground.greenF() + 0.114 * parsedBackground.blueF();
            themeIsDark = luminance < 0.5;
        }
    }
    const bool darkModeFlipped = themeIsDark != m_darkMode;
    m_darkMode = themeIsDark;

    // The defaults follow the mode the theme resolved to, so a colour we cannot
    // read costs that one colour rather than the contrast of the whole editor.
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    // An unparseable colour reaches QML as black, so keep the default instead.
    const auto applyColor = [](QString &target, const QString &value) {
        if (QColor(value).isValid())
            target = value;
    };
    applyColor(m_themeBackground, background);
    applyColor(m_themeForeground, foreground);
    applyColor(m_themeAccent, accent);
    applyColor(m_themeSelection, selection);

    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    }

    if (darkModeFlipped)
        emit darkModeChanged();
    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

// A save cannot report a clash the way creating a document does, so the name
// gives way instead.
QUrl Backend::unusedDocumentUrl(const QString &fileName) const {
    const QDir directory(m_folderUrl.toLocalFile());
    if (!QFileInfo::exists(directory.filePath(fileName)))
        return QUrl::fromLocalFile(directory.filePath(fileName));

    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString()
                                                   : QLatin1Char('.') + info.suffix();
    for (int n = 2; n < 1000; ++n) {
        const QString candidate = QStringLiteral("%1 %2%3").arg(base).arg(n).arg(suffix);
        if (!QFileInfo::exists(directory.filePath(candidate)))
            return QUrl::fromLocalFile(directory.filePath(candidate));
    }
    return QUrl::fromLocalFile(directory.filePath(fileName));
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    return QUrl::fromLocalFile(
        defaultDirectory().filePath(suggestedFileName(currentDocumentText())));
}

QDir Backend::defaultDirectory() const {
    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    return savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

void Backend::adoptFillingMeasure() {
    QSettings settings;
    const QString migrated = QStringLiteral("layout/measureFills");
    if (settings.value(migrated).toBool())
        return;
    settings.setValue(migrated, true);
    // 65 was the old default, so a stored 65 is almost certainly nobody's
    // decision. Anything else is, and stays.
    if (settings.value(QStringLiteral("layout/editorColumns")).toInt() == 65)
        settings.setValue(QStringLiteral("layout/editorColumns"), 0);
}

qreal Backend::lineHeightPercent() {
    return typoraLineHeightPercent;
}

void Backend::applyPreviewTypography(QTextDocument *document, const QFont &editorFont) {
    if (!document)
        return;

    const qreal editorPixelSize = editorFont.pixelSize() > 0
        ? editorFont.pixelSize()
        : document->defaultFont().pixelSize();
    if (editorPixelSize <= 0)
        return;
    const qreal bodyPointSize = editorPixelSize * 0.75;

    // The source view sets its lines 40% apart. A preview at Qt's default
    // single spacing is the same words at a different rhythm, and the toggle
    // reads as a change of document rather than of rendering.
    QTextBlockFormat spacing;
    spacing.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextCursor blockCursor(block);
        blockCursor.mergeBlockFormat(spacing);
        const int heading = block.blockFormat().headingLevel();
        for (auto it = block.begin(); it != block.end(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            QTextCharFormat format = fragment.charFormat();
            // Everything is written in one face here, prose and code alike,
            // so code in the preview reads as the same text it is in the
            // source rather than as Qt's fallback typewriter.
            format.setFontFamilies({appFont()});
            format.setFontFixedPitch(true);
            format.setFontPointSize(heading > 0
                ? MarkdownHighlighter::headingPointSize(editorPixelSize, heading)
                : bodyPointSize);
            if (heading > 0)
                format.setFontWeight(QFont::Bold);
            QTextCursor range(document);
            range.setPosition(fragment.position());
            range.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            range.setCharFormat(format);
        }
    }
    cursor.endEditBlock();
}

QString Backend::appFont() {
    static const QString family = []() {
        const QStringList installed = QFontDatabase::families();
        const QStringList preferred{
            QStringLiteral("JetBrainsMono Nerd Font"),
            QStringLiteral("JetBrainsMono NF"),
            QStringLiteral("JetBrains Mono"),
        };
        for (const QString &candidate : preferred) {
            if (installed.contains(candidate))
                return candidate;
        }
        return QStringLiteral("iA Writer Mono S");
    }();
    return family;
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::sanitizedEntryName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    return name;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = sanitizedEntryName(text);
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
    m_formattingTypography = false;

    m_document->setUndoRedoEnabled(undoEnabled);

    m_formattedBlockCount = m_document->blockCount();
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);

    // Format only the block(s) touched by the last edit instead of the whole
    // document, and fold the change into the preceding edit command so a single
    // undo reverts both the text and its formatting.
    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    cursor.joinPreviousEditBlock();
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(blockFormat);
    cursor.endEditBlock();
    m_formattingTypography = false;
}
