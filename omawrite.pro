!versionAtLeast(QT_VERSION, 6.5.0): error("Omawrite requires Qt 6.5 or newer (the QtCore Settings QML type)")

QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release
TARGET = omawrite
TEMPLATE = app

HEADERS += \
    src/buffersession.h \
    src/workspacesession.h \
    src/windowmanager.h \
    src/backend.h \
    src/cli.h \
    src/markdownhighlighter.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/buffersession.cpp \
    src/workspacesession.cpp \
    src/windowmanager.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
