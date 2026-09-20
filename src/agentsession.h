#pragma once

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QJsonObject;

// Claude beside the document, in the panel rather than in the next tile.
//
// One turn is one `claude -p --output-format stream-json` child, started in the
// folder the document lives in so the agent's own tools reach the rest of the
// work without anything being copied into a prompt. The question goes in on
// stdin; answer text and public status events come back out line by line. A
// follow-up resumes the session the last turn ended with.
class AgentSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QString activity READ activity NOTIFY activityChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY workingDirectoryChanged)

public:
    explicit AgentSession(QObject *parent = nullptr);
    // The directory the conversations are kept in. The tests give it a
    // scratch one; the app gives it the same place the tabs are kept.
    AgentSession(const QString &stateDirectory, QObject *parent = nullptr);
    ~AgentSession() override;

    // The CLI this panel drives. `OMAWRITE_CLAUDE` names another program, which
    // is how the tests hand it a synthetic stream instead of a real model.
    static QString program();
    // Whether that program is on PATH at all. The panel says so rather than
    // failing a turn when it is not.
    static bool available();
    // Non-interactive and streaming. The permission mode has to answer for
    // the writer, because a child with no terminal has nobody to ask:
    // acceptEdits takes the edits and refuses everything else.
    static QStringList arguments(const QString &resumeId, const QString &permissionMode,
                                 const QString &allowedTools);
    static QString permissionMode();
    // Tools the turn may use without anyone to ask, as `--allowedTools` takes
    // them. acceptEdits applies edits and refuses everything else, so the
    // editor's own command line has to be named here for the brief's offer of
    // the live buffer to be true.
    static QString allowedTools();
    // What the first turn of a chat is told: which document is in front of the
    // writer, where the caret is, what is selected, and that the running window
    // answers `omawrite --read` with text no file has yet.
    static QString preamble(const QString &documentPath, int line, const QString &selection,
                            const QString &folderPath);
    // What this turn may actually do, in a sentence, so the answer does not
    // offer a patch it could have applied or promise a command it cannot run.
    static QString permissionBrief(const QString &permissionMode, const QString &allowedTools);
    // A turn whose document is not the one the chat started on says so, rather
    // than letting the agent assume the page never moved.
    static QString contextNotice(const QString &documentPath);
    // The folder a turn runs in: the document's own, or the one the sidebar is
    // showing when the document has never been saved.
    static QString directoryFor(const QString &documentPath, const QString &folderPath);
    // "Reading note.md", "Running a command" -- the verb and, for the tools
    // that name a file, its name. Never the arguments themselves.
    static QString activityFor(const QString &tool, const QVariantMap &input);

    bool running() const { return m_process != nullptr; }
    QVariantList messages() const { return m_messages; }
    QString activity() const { return m_activity; }
    QString workingDirectory() const { return m_workingDirectory; }

    // Ask, in the context the window is in this instant. The caller saves the
    // buffer first, so the file the agent reads is the text on screen.
    Q_INVOKABLE void ask(const QString &question, const QUrl &documentUrl, int line,
                         const QString &selection, const QUrl &folderUrl);
    // Stop the turn and everything it started. The answer so far is kept.
    Q_INVOKABLE void interrupt();
    // Forget the conversation, keep the panel open. The kept copy goes with
    // it: clearing is the one thing that means "do not bring this back".
    Q_INVOKABLE void newChat();
    // Show the conversation belonging to this document, keeping the one on
    // screen. A chat is about a document, so it follows the tab rather than
    // the window, and it outlives the window: closing Omawrite in the middle
    // of working something out should not be how you lose it.
    Q_INVOKABLE void showDocument(const QUrl &documentUrl);

    // The conversation as plain text, for taking somewhere else: roles
    // labelled, turns separated, in the order they were said.
    Q_INVOKABLE QString transcript() const;

    // One line of the CLI's output. Public so the parser can be tested without
    // a process behind it.
    void readStreamLine(const QByteArray &line);

signals:
    void runningChanged();
    void messagesChanged();
    void activityChanged();
    void workingDirectoryChanged();
    void answered();

private:
    void appendMessage(const QString &role, const QString &text);
    void loadChat(const QString &documentPath);
    void saveChat();
    QString storePath() const;
    QJsonObject readStore() const;
    void appendToAnswer(const QString &text);
    void finishTurn(const QString &failure);
    void setActivity(const QString &activity);
    void readAvailableOutput();

    QString m_stateDirectory;
    // The document this conversation belongs to, empty for one that has never
    // been saved: a chat with nowhere to hang stays in memory.
    QString m_documentPath;
    // A document the writer moved to while a turn was running. The answer
    // belongs to the chat that asked for it, so the swap waits.
    QString m_deferredDocument;
    bool m_swapDeferred = false;
    QPointer<QProcess> m_process;
    qint64 m_processGroup = 0;
    QByteArray m_pending;
    QVariantList m_messages;
    QString m_activity;
    QString m_workingDirectory;
    // The session the last turn ended with, which the next one resumes.
    QString m_sessionId;
    // The document the running chat was told about, so a move can be reported.
    QString m_contextPath;
    // The answer being streamed, as an index into m_messages.
    int m_answerIndex = -1;
    bool m_answerTruncated = false;
    bool m_cancelled = false;
    bool m_resultSeen = false;
    // Whether this turn's text came through as deltas. Without partial
    // messages the whole answer arrives once, in the assistant message, and
    // taking both would print it twice.
    bool m_streamedText = false;
    // Whether this turn carried a session id. A kept conversation can be gone
    // by the time it is asked for again, and that failure has its own answer.
    bool m_resumedTurn = false;
};
