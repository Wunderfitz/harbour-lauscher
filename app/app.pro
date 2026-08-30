TARGET = harbour-lauscher

CONFIG += sailfishapp
QT += dbus

# Qt 5.6's headers are not C++20-clean, so this half of the build stays at the
# toolchain default and talks to libmdr through its C ABI only.
INCLUDEPATH += $$PWD/../libmdr/upstream/include

LIBS += -L$$OUT_PWD/../libmdr -lmdr
PRE_TARGETDEPS += $$OUT_PWD/../libmdr/libmdr.a

SOURCES += \
    src/main.cpp \
    src/BluezTransport.cpp \
    src/MdrController.cpp

HEADERS += \
    src/BluezTransport.h \
    src/MdrController.h

DISTFILES += \
    qml/harbour-lauscher.qml \
    qml/cover/CoverPage.qml \
    qml/pages/DeviceListPage.qml \
    qml/pages/DevicePage.qml \
    qml/pages/AboutPage.qml \
    harbour-lauscher.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

CONFIG += sailfishapp_i18n
TRANSLATIONS += translations/harbour-lauscher-de.ts
