QT += core gui widgets webenginewidgets webchannel network

CONFIG += c++17
TARGET = VibiBrowser
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/browserwindow.cpp \
    src/tabbar.cpp \
    src/webview.cpp \
    src/settings.cpp \
    src/downloadmanager.cpp \
    src/historymanager.cpp

HEADERS += \
    src/mainwindow.h \
    src/browserwindow.h \
    src/tabbar.h \
    src/webview.h \
    src/settings.h \
    src/downloadmanager.h \
    src/historymanager.h

RESOURCES += resources/resources.qrc

# Version
VERSION = 1.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

# Windows app icon
win32: RC_ICONS = resources/icon.ico

# Linux desktop icon
unix:!macx {
    target.path = /usr/bin
    INSTALLS += target
}
