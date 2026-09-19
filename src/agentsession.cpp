#include "agentsession.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <limits>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace {
// One turn's answer, and the panel's whole history. Both are held in memory and
// drawn every frame, so neither is allowed to grow without an end.
constexpr int answerLimit = 64 * 1024;
constexpr int historyLimit = 64;
constexpr int promptLimit = 1024 * 1024;
// What the kept conversations are allowed to cost on disk. A transcript is
// cheap; forty of them growing without an end is not.
constexpr int keptChatLimit = 40;
constexpr int storeVersion = 1;
const auto storeFileName = QStringLiteral("agent.json");

const auto permissionModeSetting = QStringLiteral("agent/permissionMode");

QString basename(const QVariantMap &input) {
    const QString path = input.value(QStringLiteral("file_path")).toString();
    return path.isEmpty() ? QString() : QFileInfo(path).fileName();
}
}

AgentSession::AgentSession(QObject *parent)
    : AgentSession(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), parent) {}

AgentSession::AgentSession(const QString &stateDirectory, QObject *parent)
    : QObject(parent), m_stateDirectory(stateDirectory) {}

AgentSession::~AgentSession() {
    interrupt();
}

QString AgentSession::program() {
    const QString overridden = qEnvironmentVariable("OMAWRITE_CLAUDE");
    return overridden.isEmpty() ? QStringLiteral("claude") : overridden;
}

bool AgentSession::available() {
    const QString command = program();
    if (command.contains(QLatin1Char('/')))
        return QFileInfo(command).isExecutable();
    return !QStandardPaths::findExecutable(command).isEmpty();
}

QString AgentSession::permissionMode() {
    const QString mode = QSettings().value(permissionModeSetting).toString();
    return mode.isEmpty() ? QStringLiteral("dontAsk") : mode;
}

QStringList AgentSession::arguments(const QString &resumeId, const QString &permissionMode) {
    QStringList arguments{
        QStringLiteral("-p"),
        QStringLiteral("--verbose"),
        QStringLiteral("--output-format"), QStringLiteral("stream-json"),
        QStringLiteral("--include-partial-messages"),
        QStringLiteral("--permission-mode"),
        permissionMode.isEmpty() ? QStringLiteral("dontAsk") : permissionMode,
    };
    // A fork rather than a plain resume: the panel can be asked to carry on
    // from a turn the writer has since branched away from, and two chats
    // sharing one session id would answer each other's questions.
    if (!resumeId.isEmpty())
        arguments << QStringLiteral("--resume") << resumeId << QStringLiteral("--fork-session");
    return arguments;
}

QString AgentSession::directoryFor(const QString &documentPath, const QString &folderPath) {
    if (!documentPath.isEmpty()) {
        const QString directory = QFileInfo(documentPath).absolutePath();
        if (QFileInfo(directory).isDir())
            return directory;
    }
    if (!folderPath.isEmpty() && QFileInfo(folderPath).isDir())
        return folderPath;
    return QDir::homePath();
}

QString AgentSession::preamble(const QString &documentPath, int line, const QString &selection,
                               const QString &folderPath) {
    const QString directory = directoryFor(documentPath, folderPath);
    QString text = QStringLiteral(
        "You are working in a panel beside Omawrite, the Markdown editor this writer has open.\n"
        "Answer as a writing collaborator: read what is around you before saying anything, and\n"
        "keep answers short enough to read beside a document.\n\n");
    if (documentPath.isEmpty()) {
        text += QStringLiteral("The document in front of them has never been saved, so it has no\n"
                               "path yet. The folder on screen is %1.\n").arg(directory);
    } else {
        text += QStringLiteral("The document in front of them is %1").arg(documentPath);
        if (line > 0)
            text += QStringLiteral(", caret on line %1").arg(line);
        text += QStringLiteral(".\n");
    }
    if (!selection.isEmpty()) {
        // Bounded, and fenced, because a selection is the writer's own text and
        // not a second set of instructions.
        QString excerpt = selection.left(2000);
        if (selection.size() > excerpt.size())
            excerpt += QStringLiteral("\n...");
        text += QStringLiteral("They have this selected:\n```\n%1\n```\n").arg(excerpt);
    }
    // Each command stays on one line: a name broken across a newline is one
    // the reader has to reassemble before it can be typed.
    text += QStringLiteral(
        "\nYou are running in %1 and everything in it is yours to read.\n"
        "The editor is running too, and answers its own command line:\n"
        "  `omawrite --tabs` lists what is open\n"
        "  `omawrite --read [TAB]` prints a tab's text as the editor holds it,\n"
        "  including edits no file has yet\n"
        "  `omawrite --open FILE[:LINE]` moves the window onto something\n"
        "Prefer those over the file on disk when the question is about what is on screen.\n")
        .arg(directory);
    return text;
}

QString AgentSession::contextNotice(const QString &documentPath) {
    if (documentPath.isEmpty())
        return QStringLiteral("They have moved to a document that has not been saved yet.\n\n");
    return QStringLiteral("They are on %1 now.\n\n").arg(documentPath);
}

QString AgentSession::activityFor(const QString &tool, const QVariantMap &input) {
    const QString file = basename(input);
    const auto withFile = [&file](const QString &verb) {
        return file.isEmpty() ? verb : QStringLiteral("%1 %2").arg(verb, file);
    };
    if (tool == QLatin1String("Read"))
        return withFile(QStringLiteral("Reading"));
    if (tool == QLatin1String("Edit") || tool == QLatin1String("NotebookEdit"))
        return withFile(QStringLiteral("Editing"));
    if (tool == QLatin1String("Write"))
        return withFile(QStringLiteral("Writing"));
    if (tool == QLatin1String("Bash"))
        return QStringLiteral("Running a command");
    if (tool == QLatin1String("Glob") || tool == QLatin1String("Grep"))
        return QStringLiteral("Searching the folder");
    if (tool == QLatin1String("WebFetch") || tool == QLatin1String("WebSearch"))
        return QStringLiteral("Reading the web");
    if (tool == QLatin1String("Task") || tool == QLatin1String("Agent"))
        return QStringLiteral("Working on it");
    if (tool.isEmpty())
        return QStringLiteral("Working");
    return QStringLiteral("Using %1").arg(tool);
}

void AgentSession::ask(const QString &question, const QUrl &documentUrl, int line,
                       const QString &selection, const QUrl &folderUrl) {
    const QString documentPath = documentUrl.isLocalFile() ? documentUrl.toLocalFile() : QString();
    const QString folderPath = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : QString();
    const QString asked = question.trimmed();
    if (asked.isEmpty() || running())
        return;

    // A question asked on a document the panel was not showing files the
    // conversation under that document from here on.
    if (documentPath != m_documentPath && !m_swapDeferred) {
        saveChat();
        m_documentPath = documentPath;
        if (!documentPath.isEmpty())
            loadChat(documentPath);
    }

    if (!available()) {
        appendMessage(QStringLiteral("you"), asked);
        appendMessage(QStringLiteral("trouble"),
                      QStringLiteral("The Claude command line is not installed, so the panel has "
                                     "nothing to ask. Install it and open the panel again."));
        return;
    }

    QString prompt;
    if (m_sessionId.isEmpty()) {
        prompt = preamble(documentPath, line, selection, folderPath)
            + QStringLiteral("\n") + asked;
        m_contextPath = documentPath;
    } else {
        if (documentPath != m_contextPath) {
            prompt = contextNotice(documentPath);
            m_contextPath = documentPath;
        }
        if (!selection.isEmpty()) {
            QString excerpt = selection.left(2000);
            if (selection.size() > excerpt.size())
                excerpt += QStringLiteral("\n...");
            prompt += QStringLiteral("They have this selected:\n```\n%1\n```\n\n").arg(excerpt);
        }
        prompt += asked;
    }

    appendMessage(QStringLiteral("you"), asked);
    if (prompt.toUtf8().size() > promptLimit) {
        appendMessage(QStringLiteral("trouble"),
                      QStringLiteral("That is more than one turn can carry. Start a new chat and "
                                     "ask for less of the document at a time."));
        return;
    }

    m_pending.clear();
    m_resumedTurn = !m_sessionId.isEmpty();
    m_cancelled = false;
    m_resultSeen = false;
    m_streamedText = false;
    m_answerTruncated = false;
    setActivity(QStringLiteral("Thinking"));
    appendMessage(QStringLiteral("claude"), QString());
    m_answerIndex = m_messages.size() - 1;

    const QString directory = directoryFor(documentPath, folderPath);
    if (directory != m_workingDirectory) {
        m_workingDirectory = directory;
        emit workingDirectoryChanged();
    }

    auto *process = new QProcess(this);
    process->setWorkingDirectory(directory);
    // stderr carries login and tool diagnostics that belong in neither the
    // panel nor the session file, so it is dropped rather than read.
    process->setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_UNIX
    // Its own process group, so interrupting the turn takes the tools it
    // started with it rather than leaving them behind.
    process->setChildProcessModifier([]() { ::setsid(); });
#endif
    connect(process, &QProcess::readyReadStandardOutput, this,
            &AgentSession::readAvailableOutput);
    connect(process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        readAvailableOutput();
        if (m_resultSeen || m_cancelled) {
            finishTurn(QString());
            return;
        }
        finishTurn(QStringLiteral("The AI request failed. Check its login or permissions, "
                                  "then ask again."));
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishTurn(QStringLiteral("The Claude command line would not start."));
    });

    // The question goes in on stdin: an argument list is visible to every other
    // process on the machine, and a document's words are not. It is written
    // once the child is there to read it -- a write closed before the pipe
    // exists reaches nothing, and the turn would be asked an empty question.
    // The question goes in on stdin: an argument list is visible to every other
    // process on the machine, and a document's words are not. It is written
    // once the child is there to read it, and the channel closes once the
    // write has actually drained -- closing it on a buffer still holding the
    // question asks the turn nothing at all.
    const QByteArray turnInput = prompt.toUtf8();
    connect(process, &QProcess::started, this, [this, process, turnInput]() {
        process->write(turnInput);
        m_processGroup = process->processId();
    });
    connect(process, &QProcess::bytesWritten, this, [process]() {
        if (process->bytesToWrite() == 0)
            process->closeWriteChannel();
    });

    m_process = process;
    emit runningChanged();
    process->start(program(), arguments(m_sessionId, permissionMode()));
}

void AgentSession::interrupt() {
    if (!m_process)
        return;
    m_cancelled = true;
    finishTurn(QString());
}

void AgentSession::newChat() {
    interrupt();
    m_sessionId.clear();
    m_contextPath.clear();
    m_messages.clear();
    m_answerIndex = -1;
    saveChat();
    emit messagesChanged();
}

void AgentSession::readAvailableOutput() {
    auto *process = qobject_cast<QProcess *>(sender());
    if (!process)
        process = m_process;
    if (!process)
        return;

    m_pending += process->readAllStandardOutput();
    int newline = m_pending.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        readStreamLine(line);
        newline = m_pending.indexOf('\n');
    }
    // A stream that stops mid-line is an incomplete event, never a short answer.
    if (m_pending.size() > answerLimit)
        m_pending.clear();
}

void AgentSession::readStreamLine(const QByteArray &line) {
    if (line.trimmed().isEmpty())
        return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonObject event = document.object();
    const QString type = event.value(QStringLiteral("type")).toString();
    const QString sessionId = event.value(QStringLiteral("session_id")).toString();
    if (!sessionId.isEmpty())
        m_sessionId = sessionId;

    if (type == QLatin1String("stream_event")) {
        const QJsonObject inner = event.value(QStringLiteral("event")).toObject();
        if (inner.value(QStringLiteral("type")).toString() != QLatin1String("content_block_delta"))
            return;
        const QJsonObject delta = inner.value(QStringLiteral("delta")).toObject();
        // Thinking and signature deltas arrive on the same channel and are not
        // the panel's to show.
        if (delta.value(QStringLiteral("type")).toString() != QLatin1String("text_delta"))
            return;
        const QString text = delta.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            return;
        m_streamedText = true;
        setActivity(QString());
        appendToAnswer(text);
        return;
    }

    if (type == QLatin1String("assistant")) {
        const QJsonArray content = event.value(QStringLiteral("message")).toObject()
            .value(QStringLiteral("content")).toArray();
        for (const QJsonValue &value : content) {
            const QJsonObject block = value.toObject();
            const QString blockType = block.value(QStringLiteral("type")).toString();
            if (blockType == QLatin1String("tool_use")) {
                setActivity(activityFor(block.value(QStringLiteral("name")).toString(),
                                        block.value(QStringLiteral("input")).toObject()
                                            .toVariantMap()));
            } else if (blockType == QLatin1String("text") && !m_streamedText) {
                // A stream without partial messages in it still has to answer.
                setActivity(QString());
                appendToAnswer(block.value(QStringLiteral("text")).toString());
            }
        }
        return;
    }

    if (type == QLatin1String("user")) {
        const QJsonArray content = event.value(QStringLiteral("message")).toObject()
            .value(QStringLiteral("content")).toArray();
        for (const QJsonValue &value : content) {
            if (value.toObject().value(QStringLiteral("type")).toString()
                    == QLatin1String("tool_result")) {
                setActivity(QStringLiteral("Thinking"));
                break;
            }
        }
        return;
    }

    if (type == QLatin1String("result")) {
        m_resultSeen = true;
        const bool failed = event.value(QStringLiteral("is_error")).toBool()
            || event.value(QStringLiteral("subtype")).toString() != QLatin1String("success");
        if (!m_streamedText && m_answerIndex >= 0
                && m_messages.at(m_answerIndex).toMap().value(QStringLiteral("text"))
                    .toString().isEmpty()) {
            appendToAnswer(event.value(QStringLiteral("result")).toString());
        }
        finishTurn(failed ? QStringLiteral("The AI request did not finish. Ask again, or start a "
                                           "new chat.")
                          : QString());
    }
}

void AgentSession::appendToAnswer(const QString &text) {
    if (text.isEmpty() || m_answerIndex < 0 || m_answerIndex >= m_messages.size())
        return;

    QVariantMap message = m_messages.at(m_answerIndex).toMap();
    QString answer = message.value(QStringLiteral("text")).toString();
    if (answer.size() >= answerLimit) {
        if (!m_answerTruncated) {
            m_answerTruncated = true;
            answer += QStringLiteral("\n\n[That answer is longer than the panel holds. Start a "
                                     "new chat and ask for it in pieces.]");
            message[QStringLiteral("text")] = answer;
            m_messages[m_answerIndex] = message;
            emit messagesChanged();
        }
        return;
    }

    message[QStringLiteral("text")] = answer + text;
    m_messages[m_answerIndex] = message;
    emit messagesChanged();
}

QString AgentSession::storePath() const {
    return QDir(m_stateDirectory).filePath(storeFileName);
}

QJsonObject AgentSession::readStore() const {
    QFile file(storePath());
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()
            || document.object().value(QStringLiteral("version")).toInt() != storeVersion)
        return QJsonObject();
    return document.object().value(QStringLiteral("chats")).toObject();
}

void AgentSession::loadChat(const QString &documentPath) {
    m_messages.clear();
    m_sessionId.clear();
    m_contextPath.clear();
    m_answerIndex = -1;

    const QJsonObject chat = readStore().value(documentPath).toObject();
    m_sessionId = chat.value(QStringLiteral("sessionId")).toString();
    m_contextPath = documentPath;
    for (const QJsonValue &value : chat.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role != QLatin1String("you") && role != QLatin1String("claude")
                && role != QLatin1String("trouble"))
            continue;
        QVariantMap kept;
        kept[QStringLiteral("role")] = role;
        kept[QStringLiteral("text")] = message.value(QStringLiteral("text")).toString();
        m_messages.append(kept);
    }
    while (m_messages.size() > historyLimit)
        m_messages.removeFirst();
    emit messagesChanged();
}

// Read, replace this document's entry, write. Two windows can hold the file
// at once, and a blind overwrite would take the other one's chat with it.
void AgentSession::saveChat() {
    if (m_stateDirectory.isEmpty() || m_documentPath.isEmpty())
        return;

    QJsonObject chats = readStore();
    if (m_messages.isEmpty() && m_sessionId.isEmpty()) {
        chats.remove(m_documentPath);
    } else {
        QJsonArray messages;
        for (const QVariant &value : m_messages) {
            const QVariantMap message = value.toMap();
            messages.append(QJsonObject{
                {QStringLiteral("role"), message.value(QStringLiteral("role")).toString()},
                {QStringLiteral("text"), message.value(QStringLiteral("text")).toString()}});
        }
        chats[m_documentPath] = QJsonObject{
            {QStringLiteral("sessionId"), m_sessionId},
            {QStringLiteral("updated"), QDateTime::currentSecsSinceEpoch()},
            {QStringLiteral("messages"), messages}};
    }

    // The oldest conversations go first when there are too many of them.
    while (chats.size() > keptChatLimit) {
        QString oldestKey;
        qint64 oldest = std::numeric_limits<qint64>::max();
        for (auto it = chats.constBegin(); it != chats.constEnd(); ++it) {
            const qint64 updated = qint64(it.value().toObject()
                                          .value(QStringLiteral("updated")).toDouble());
            if (updated < oldest) {
                oldest = updated;
                oldestKey = it.key();
            }
        }
        if (oldestKey.isEmpty() || oldestKey == m_documentPath)
            break;
        chats.remove(oldestKey);
    }

    QDir().mkpath(m_stateDirectory);
    QSaveFile file(storePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject store{{QStringLiteral("version"), storeVersion},
                            {QStringLiteral("chats"), chats}};
    if (file.write(QJsonDocument(store).toJson(QJsonDocument::Compact)) < 0)
        return;
    file.commit();
    // Questions and answers about the writer's own documents: nobody else's
    // to read.
    QFile::setPermissions(storePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void AgentSession::showDocument(const QUrl &documentUrl) {
    const QString path = documentUrl.isLocalFile() ? documentUrl.toLocalFile() : QString();
    if (path == m_documentPath && (!m_messages.isEmpty() || !path.isEmpty()))
        return;

    // A turn belongs to the chat that asked for it, so a tab change during
    // one waits for its answer rather than filing it under the new document.
    if (running()) {
        m_deferredDocument = path;
        m_swapDeferred = true;
        return;
    }

    saveChat();
    m_documentPath = path;
    if (path.isEmpty()) {
        m_messages.clear();
        m_sessionId.clear();
        m_contextPath.clear();
        m_answerIndex = -1;
        emit messagesChanged();
        return;
    }
    loadChat(path);
}

void AgentSession::appendMessage(const QString &role, const QString &text) {
    QVariantMap message;
    message[QStringLiteral("role")] = role;
    message[QStringLiteral("text")] = text;
    m_messages.append(message);
    while (m_messages.size() > historyLimit) {
        m_messages.removeFirst();
        if (m_answerIndex >= 0)
            --m_answerIndex;
    }
    emit messagesChanged();
}

void AgentSession::finishTurn(const QString &failure) {
    if (!m_process)
        return;

    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
#ifdef Q_OS_UNIX
        if (m_processGroup > 0)
            ::kill(static_cast<pid_t>(-m_processGroup), SIGTERM);
#endif
        process->terminate();
        if (!process->waitForFinished(200)) {
            process->kill();
            process->waitForFinished(200);
        }
    }
    process->deleteLater();
    m_processGroup = 0;
    m_pending.clear();
    setActivity(QString());

    // An answer that never arrived leaves an empty bubble behind; say what
    // happened in its place rather than showing nothing.
    const bool empty = m_answerIndex >= 0 && m_answerIndex < m_messages.size()
        && m_messages.at(m_answerIndex).toMap().value(QStringLiteral("text")).toString().isEmpty();
    if (empty) {
        m_messages.removeAt(m_answerIndex);
        emit messagesChanged();
    }
    m_answerIndex = -1;

    QString said = failure;
    if (!said.isEmpty() && m_resumedTurn) {
        // The conversation it was told to carry on is not there any more --
        // a cleared CLI history, another machine, a session too old. Drop it
        // rather than failing the same way every time from here on.
        m_sessionId.clear();
        said = QStringLiteral("That conversation could not be picked up again. Ask once more "
                              "and Claude starts a fresh one.");
    }
    if (!said.isEmpty())
        appendMessage(QStringLiteral("trouble"), said);
    else if (empty && m_cancelled)
        appendMessage(QStringLiteral("trouble"), QStringLiteral("Stopped."));

    saveChat();
    emit runningChanged();
    emit answered();

    if (m_swapDeferred) {
        m_swapDeferred = false;
        const QString next = m_deferredDocument;
        m_deferredDocument.clear();
        showDocument(next.isEmpty() ? QUrl() : QUrl::fromLocalFile(next));
    }
}

void AgentSession::setActivity(const QString &activity) {
    if (m_activity == activity)
        return;
    m_activity = activity;
    emit activityChanged();
}
