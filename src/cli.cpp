#include "cli.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

QString Cli::usage() {
    return QStringLiteral(
        "Omawrite is a dead-simple Markdown writing app.\n"
        "\n"
        "Usage:\n"
        "  omawrite [FILE]\n"
        "  omawrite --open FILE[:LINE]\n"
        "  omawrite --append FILE\n"
        "  omawrite --list-tabs\n"
        "\n"
        "Opens the Omawrite window. FILE is the Markdown file to open; without\n"
        "one, Omawrite starts on an empty document, or on the draft recovered\n"
        "from the last session. A file that is not there yet is a file you mean\n"
        "to start, so the name is taken for a blank document.\n"
        "\n"
        "Options:\n"
        "  --open FILE[:LINE]  Show FILE in the window that is already open,\n"
        "                      at LINE when one is given. Starts Omawrite when\n"
        "                      nothing is running yet.\n"
        "  --append FILE       Add what is on standard input to the end of\n"
        "                      FILE and exit. No window opens, and a FILE that\n"
        "                      is open in a tab is updated in place.\n"
        "  --list-tabs         Print what the last session left open, one path\n"
        "                      per line, and exit.\n"
        "  -h, --help          Show this message and exit\n"
        "\n"
        "Omawrite is a graphical app: opening a window needs a desktop session,\n"
        "and it keeps running until that window is closed. Scripts and agents\n"
        "that only want a file on screen should use --open, which returns as\n"
        "soon as the running window has it:\n"
        "\n"
        "  omawrite --open notes/standup.md:12\n"
        "  date | omawrite --append notes/log.md\n"
        "\n"
        "Everything else is a keyboard shortcut. Ctrl+? lists them in the app,\n"
        "and https://github.com/omacom-io/omawrite#shortcuts has the same list.\n");
}

Cli::Request Cli::parseTarget(Request::Kind kind, const QString &argument) {
    Request request;
    request.kind = kind;
    request.path = argument;

    // Only a trailing run of digits after a colon is a line number. Anything
    // else belongs to the name: a colon is legal in a filename, and guessing
    // wrong would open a file that is not there under a name nobody typed.
    const int colon = argument.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon == argument.size() - 1)
        return request;

    const QString tail = argument.mid(colon + 1);
    bool numeric = false;
    const int line = tail.toInt(&numeric);
    if (!numeric || line <= 0)
        return request;

    request.path = argument.left(colon);
    request.line = line;
    return request;
}

Cli::Request Cli::parse(const QStringList &arguments) {
    Request request;

    const QStringList rest = arguments.mid(1);
    for (int index = 0; index < rest.size(); ++index) {
        const QString argument = rest.at(index);

        if (argument == QLatin1String("-h") || argument == QLatin1String("--help")) {
            QTextStream(stdout) << usage();
            return {Request::Help, {}, 0, 0};
        }

        if (argument == QLatin1String("--list-tabs"))
            return {Request::ListTabs, {}, 0, 0};

        const bool wantsOpen = argument == QLatin1String("--open");
        const bool wantsAppend = argument == QLatin1String("--append");
        if (wantsOpen || wantsAppend) {
            if (index + 1 >= rest.size()) {
                QTextStream(stderr) << QStringLiteral("omawrite: %1 needs a file\n\n")
                                           .arg(argument)
                                    << usage();
                return {Request::Error, {}, 0, 1};
            }
            // --append takes the name whole: its text arrives on stdin, so a
            // line number would have nothing to mean.
            const QString target = rest.at(++index);
            return wantsOpen ? parseTarget(Request::Open, target)
                             : Request{Request::Append, target, 0, 0};
        }

        // A leading dash means an option was meant, not a file. Answering with
        // the usage beats opening a window for a file that cannot exist.
        if (argument.startsWith(QLatin1Char('-')) && argument != QLatin1String("-")) {
            QTextStream(stderr) << QStringLiteral("omawrite: unrecognized option '%1'\n\n")
                                       .arg(argument)
                                << usage();
            return {Request::Error, {}, 0, 1};
        }

        if (request.path.isEmpty())
            request.path = argument;
    }

    return request;
}

Cli::Request Cli::parse(int argc, char *argv[]) {
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));

    return parse(arguments);
}


int Cli::listTabs() {
    const QString sessionPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("session.json"));

    QFile session(sessionPath);
    if (!session.open(QIODevice::ReadOnly)) {
        // Nothing open is not a failure: a first run has no session yet, and a
        // caller looping over the output should see an empty list, not an error.
        return 0;
    }

    const QJsonDocument document = QJsonDocument::fromJson(session.readAll());
    const QJsonObject root = document.object();

    // Windows hold tabs when a workspace wrote the file; a single window writes
    // its buffers at the top level. Read both rather than guess which ran last.
    QJsonArray entries = root.value(QStringLiteral("buffers")).toArray();
    for (const QJsonValue &window : root.value(QStringLiteral("windows")).toArray()) {
        for (const QJsonValue &tab : window.toObject().value(QStringLiteral("tabs")).toArray())
            entries.append(tab);
    }

    QTextStream out(stdout);
    for (const QJsonValue &entry : entries) {
        const QString fileUrl = entry.toObject().value(QStringLiteral("fileUrl")).toString();
        const QUrl url(fileUrl);
        out << (url.isLocalFile() ? url.toLocalFile() : QStringLiteral("(untitled)"))
            << QLatin1Char('\n');
    }
    return 0;
}

int Cli::appendStdin(const QString &path) {
    QTextStream in(stdin);
    const QString text = in.readAll();

    const QString absolute = QFileInfo(path).absoluteFilePath();
    QFile file(absolute);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(stderr) << QStringLiteral("omawrite: could not append to %1\n").arg(absolute);
        return 1;
    }

    // A file that does not end in a newline would otherwise have the new text
    // run on from its last line, which is never what appending a note means.
    if (file.size() > 0) {
        QFile existing(absolute);
        if (existing.open(QIODevice::ReadOnly)) {
            existing.seek(qMax(qint64(0), existing.size() - 1));
            if (existing.read(1) != QByteArray("\n"))
                file.write("\n");
        }
    }

    file.write(text.toUtf8());
    if (!text.endsWith(QLatin1Char('\n')))
        file.write("\n");
    return file.flush() ? 0 : 1;
}

std::optional<int> Cli::handleArguments(const QStringList &arguments) {
    const Request request = parse(arguments);
    if (request.kind == Request::Help || request.kind == Request::Error)
        return request.exitCode;

    return std::nullopt;
}

std::optional<int> Cli::handleArguments(int argc, char *argv[]) {
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));

    return handleArguments(arguments);
}
