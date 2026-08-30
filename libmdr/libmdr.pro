# Static build of the upstream SonyHeadphonesClient MDR protocol library.
#
# The sources are vendored under upstream/ at a state known to work with this app;
# UPSTREAM.md says which commit they came from and how to refresh them. fmt is
# vendored too because upstream pulls it in via CMake FetchContent, which is no use
# here: Sailfish has no fmt package and RPM builds should not reach the network.

TEMPLATE = lib
CONFIG += staticlib
CONFIG -= qt
QT -= core gui

TARGET = mdr

MDR_ROOT = $$PWD/upstream
FMT_ROOT = $$PWD/3rdparty/fmt

# GCC 13 in the Sailfish target; upstream requires C++20.
QMAKE_CXXFLAGS += -std=c++20
QMAKE_CXXFLAGS += -Wall -Wno-unused-function -Wno-unused-variable

# Upstream builds with -fno-rtti; keep that, it only ever returns MDRResult.
# Exceptions stay ON here (unlike upstream's MDR_NO_EXCEPTIONS default) so the
# stock fmt release drops in unpatched - libmdr itself never throws.
QMAKE_CXXFLAGS += -fno-rtti -ffunction-sections -fdata-sections

DEFINES += MDR_ENABLE_LOG=1

INCLUDEPATH += \
    $$MDR_ROOT/include \
    $$MDR_ROOT/src \
    $$FMT_ROOT/include

SOURCES += \
    $$MDR_ROOT/src/Command.cpp \
    $$MDR_ROOT/src/Headphones.cpp \
    $$MDR_ROOT/src/HeadphonesV1.cpp \
    $$MDR_ROOT/src/HeadphonesV1T1.cpp \
    $$MDR_ROOT/src/HeadphonesV1T2.cpp \
    $$MDR_ROOT/src/HeadphonesV2.cpp \
    $$MDR_ROOT/src/HeadphonesV2T1.cpp \
    $$MDR_ROOT/src/HeadphonesV2T2.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV1T1Serialization.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV1T1Validation.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV1T2Serialization.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV1T2Validation.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV2T1Serialization.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV2T1Validation.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV2T2Serialization.cpp \
    $$MDR_ROOT/src/Generated/ProtocolV2T2Validation.cpp \
    $$FMT_ROOT/src/format.cc

# Nothing is installed: the app links this statically.
