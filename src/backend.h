#pragma once

#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QDir>
#include <QFileSystemWatcher>
#include <QString>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "buffersession.h"
#include "workspacesession.h"

class MarkdownHighlighter;
class PreviewDocument;
class QFont;
class QTextDocument;
class QWindow;
class QLockFile;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    Q_PROPERTY(QVariantList buffers READ buffers NOTIFY buffersChanged)
    Q_PROPERTY(QString activeBufferId READ activeBufferId NOTIFY activeBufferChanged)
    Q_PROPERTY(int activeCursorPosition READ activeCursorPosition NOTIFY activeBufferChanged)
    Q_PROPERTY(int activeSelectionStart READ activeSelectionStart NOTIFY activeBufferChanged)
    Q_PROPERTY(int activeSelectionEnd READ activeSelectionEnd NOTIFY activeBufferChanged)
    Q_PROPERTY(QString activeBufferText READ activeBufferText NOTIFY activeBufferChanged)
    Q_PROPERTY(bool restoringActiveBuffer READ restoringActiveBuffer NOTIFY activeBufferChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY wordCountChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize
               NOTIFY editorFontSizeChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)
    Q_PROPERTY(QUrl folderUrl READ folderUrl NOTIFY folderChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY folderChanged)
    Q_PROPERTY(bool folderHasParent READ folderHasParent NOTIFY folderChanged)
    Q_PROPERTY(QVariantList folderEntries READ folderEntries NOTIFY folderChanged)
    Q_PROPERTY(bool focusMode READ focusMode NOTIFY focusModeChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    Backend(const QString &stateDirectory, QObject *parent = nullptr);
    Backend(WorkspaceSession *workspaceSession, const QString &windowId,
            QObject *parent = nullptr);
    ~Backend() override;

    void setParentWindow(QWindow *window);
    QWindow *parentWindow() const;

    // Open a file and put the caret on a 1-based line, or leave it where the
    // tab left it when the line is 0. With newTab the file opens beside what is
    // showing rather than over it. This is what `omawrite --open` reaches.
    Q_INVOKABLE void openAtLine(const QUrl &url, int line, bool newTab = false);

    QUrl fileUrl() const { return m_fileUrl; }
    QVariantList buffers() const { return m_workspaceSession
            ? m_workspaceSession->tabs(m_workspaceWindowId) : m_bufferSession.buffers(); }
    QString activeBufferId() const { return m_workspaceSession
            ? m_workspaceSession->activeTabId(m_workspaceWindowId) : m_bufferSession.activeBufferId(); }
    int activeCursorPosition() const { return m_cursorPosition; }
    int activeSelectionStart() const { return m_selectionStart; }
    int activeSelectionEnd() const { return m_selectionEnd; }
    QString activeBufferText() const { return m_activeBufferText; }
    bool restoringActiveBuffer() const { return m_restoringActiveBuffer; }
    QString fileName() const;
    Q_INVOKABLE QString bufferTitle(const QVariantMap &buffer, int index) const;

    bool modified() const { return m_modified; }
    QString status() const { return m_status; }
    int wordCount() const { return m_wordCount; }
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    int editorFontSize() const { return m_editorFontSize; }
    void setEditorFontSize(int editorFontSize);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }
    QUrl folderUrl() const { return m_folderUrl; }
    QString folderName() const;
    bool folderHasParent() const;
    QVariantList folderEntries() const;
    bool focusMode() const { return m_focusMode; }
    static int countWords(const QString &text);
    static QString normalizedLinkUrl(const QString &clipboardText);
    static QString suggestedFileName(const QString &text);
    static QString sanitizedEntryName(const QString &text);
    static QString tomlValue(const QString &text);
    static QFont printFont(const QFont &editorFont, qreal screenDpi);

    Q_INVOKABLE void attachDocument(QObject *textDocument);
    Q_INVOKABLE void attachPreviewDocument(QObject *textDocument);
    Q_INVOKABLE void setPreviewMarkdown(const QString &markdown);
    Q_INVOKABLE void setPreviewWidth(int width);
    Q_INVOKABLE QString newBuffer();
    Q_INVOKABLE bool selectBuffer(const QString &id);
    Q_INVOKABLE bool moveActiveBuffer(int direction);
    Q_INVOKABLE bool closeActiveBuffer();
    Q_INVOKABLE bool discardActiveBuffer();
    Q_INVOKABLE void prepareForApplicationClose();
    Q_INVOKABLE void finishActiveBufferRestore();
    Q_INVOKABLE void updateActiveEditorState(int cursorPosition, int selectionStart, int selectionEnd);
    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void setFolder(const QUrl &url);
    Q_INVOKABLE void openParentFolder();
    Q_INVOKABLE QUrl createDocument(const QString &name);
    Q_INVOKABLE QUrl createFolder(const QString &name);
    Q_INVOKABLE int sidebarWidth() const;
    Q_INVOKABLE void saveSidebarWidth(int width);
    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveNow();
    Q_INVOKABLE bool saveBeforeLeaving();
    Q_INVOKABLE bool saveBeforeClosing();
    Q_INVOKABLE void saveForClose();
    Q_INVOKABLE void saveAsDialog();
    Q_INVOKABLE void saveAs(const QUrl &url);
    Q_INVOKABLE void fileDialogCanceled();
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE void reloadFromDisk();
    Q_INVOKABLE void keepExternalVersion();
    Q_INVOKABLE void dismissExternalChange();
    Q_INVOKABLE void resetEditorFontSize();
    Q_INVOKABLE void printDocument();
    Q_INVOKABLE void newWindow();
    Q_INVOKABLE QString saveClipboardImage();
    Q_INVOKABLE QString clipboardUrl() const;
    Q_INVOKABLE QString clipboardText() const;
    // One edit for undo, however many the editor makes inside it. An unwrap
    // rewrites a whole document as a remove and an insert, and a Ctrl+Z that
    // landed between the two would show an empty page.
    Q_INVOKABLE void beginUndoBlock();
    Q_INVOKABLE void endUndoBlock();
    Q_INVOKABLE bool editorTextChanged();
    Q_INVOKABLE QVariantList hiddenRangesAt(int position) const;
    Q_INVOKABLE void setSearchHighlight(const QString &query, int currentMatchStart);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    Q_INVOKABLE QVariantMap linkAt(int position) const;
    Q_INVOKABLE void notifyMissingNote(const QString &target);
    static QString resolveWikilinkPath(const QString &currentFile, const QString &target);
    void reportExternalChange(bool deleted);
    void refreshBuffers();
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    Q_INVOKABLE void toggleFocusMode();
    Q_INVOKABLE void updateCursorPosition(int position);

signals:
    void fileUrlChanged();
    void buffersChanged();
    void activeBufferChanged();
    void modifiedChanged();
    void statusChanged();
    void wordCountChanged();
    void darkModeChanged();
    void textScaleChanged(qreal textScale);
    void editorFontSizeChanged();
    void themeColorsChanged();
    void closeAfterSave();
    void openDialogRequested();
    void saveDialogRequested(const QUrl &suggestedUrl);
    void saveSucceeded();
    void externalChangeDetected(bool deleted, bool locallyModified);
    void folderChanged();
    void documentLoaded();
    void previewChanged();
    void newWindowRequested();
    void openTabRequested(const QString &tabId);
    void windowEmptied();
    void focusModeChanged();

    void externalFileAppeared(bool locallyModified);

private:
    void initializeRuntime();
    void openPath(const QUrl &url, bool mayStartNewFile);
    void loadDocumentText(const QString &text);
    // Take the newer text without asking, for a document holding no local
    // changes. The caret keeps its place.
    void reloadSilently();
    void loadActiveBuffer();
    void persistActiveBuffer();
    void setFileUrl(const QUrl &url);
    void setModified(bool modified);
    void setStatus(const QString &status);
    void setExternalChangePending(bool pending);
    bool saveTo(const QUrl &url);
    QUrl suggestedSaveUrl() const;
    QDir defaultDirectory() const;
    void applyFolder(const QString &path, bool remember);
    void watchCurrentFolder();
    QString currentDocumentText() const;
    void setWordCount(int words);
    void refreshWordCount();
    void scheduleWordCount();
    void applyDocumentTypography();
    void reapplyTypographyToChange();
    void schedulePersist();
    void persistDocument();
    bool saveToItsOwnFile();
    bool writeRecovery();
    QUrl unusedDocumentUrl(const QString &fileName) const;
    void restoreRecovery();
    void clearRecovery();
    QString recoveryPath() const;
    void watchCurrentFile();
    void watchPreviewImage(const QString &path);
    void loadOmarchyTheme();
    void watchOmarchyTheme();

    QUrl m_fileUrl;
    bool m_modified = false;
    QString m_status;
    int m_wordCount = 0;
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    int m_editorFontSize = 20;
    bool m_loading = false;
    bool m_closeAfterSave = false;
    bool m_formattingTypography = false;
    int m_formattedBlockCount = 0;
    int m_lastChangePos = 0;
    int m_lastChangeAdded = 0;
    int m_cursorPosition = 0;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    QString m_activeBufferText;
    QTimer m_wordCountTimer;
    QTimer m_persistTimer;
    QFileSystemWatcher m_fileWatcher;
    QUrl m_folderUrl;
    QFileSystemWatcher m_folderWatcher;
    BufferSession m_bufferSession;
    WorkspaceSession *m_workspaceSession = nullptr;
    QString m_workspaceWindowId;
    QPointer<QTextDocument> m_document;
    QTextCursor m_undoBlock;
    QPointer<QWindow> m_parentWindow;
    QPointer<MarkdownHighlighter> m_highlighter;
    QPointer<PreviewDocument> m_previewDocument;
    QFileSystemWatcher m_previewImageWatcher;
    QString m_previewMarkdown;
    QString m_lastDocumentText;
    QByteArray m_lastKnownFileContents;
    bool m_hasKnownFileContents = false;
    bool m_externalChangePending = false;
    // The prompt was dismissed rather than answered, so the question is still
    // open and the file is still not ours to write.
    bool m_externalChangeDismissed = false;
    bool m_applicationClosing = false;
    bool m_restoringActiveBuffer = false;
    bool m_ignoringInitialCursorReset = false;
    // Set where this document takes a name without having read what is on it,
    // and cleared the moment anything settles the question -- a read, a write,
    // or the writer answering the dialog. It is not the same question as
    // m_hasKnownFileContents, which asks whether we hold a copy to compare
    // against; a path we have never looked at is one nothing can watch.
    bool m_pathNeverRead = false;
    QString m_recoveryPath;
    std::unique_ptr<QLockFile> m_recoveryLock;
    bool m_focusMode = false;

    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QFileSystemWatcher m_themeWatcher;
};
