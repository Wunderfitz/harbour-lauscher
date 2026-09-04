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
    qml/components/BackgroundImage.qml \
    qml/pages/DeviceListPage.qml \
    qml/pages/DevicePage.qml \
    qml/pages/AboutPage.qml \
    harbour-lauscher.desktop \
    icons/harbour-lauscher.svg \
    $$files(images/*.svg)

# sailfishapp.prf deploys qml/ for us but knows nothing about anything else, so
# the cover's artwork needs its own install. CoverPage.qml reaches it as
# ../../images, which holds both here and under /usr/share/harbour-lauscher.
images.files = images
images.path = /usr/share/$${TARGET}
INSTALLS += images

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

CONFIG += sailfishapp_i18n
TRANSLATIONS += translations/harbour-lauscher-de.ts
