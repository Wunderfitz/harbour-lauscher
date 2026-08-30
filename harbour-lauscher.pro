# harbour-lauscher - Sony headphone control for Sailfish OS
#
# Two subprojects, deliberately: libmdr needs C++20 (coroutines, concepts) while
# Qt 5.6's headers are only safe up to C++17. They meet at libmdr's pure-C ABI
# (mdr-c/*.h), so no C++ ABI ever crosses the boundary.

TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = libmdr app

OTHER_FILES += \
    rpm/harbour-lauscher.spec \
    CLAUDE.md \
