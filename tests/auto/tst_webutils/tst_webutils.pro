TARGET = tst_webutils
CONFIG += link_pkgconfig
QMAKE_LFLAGS += -lgtest -lgmock

include(../mocks/qmozcontext/qmozcontext.pri)
include(../test_common.pri)
include(../../../common/common.pri)

INCLUDEPATH += ../../../src/

SOURCES += tst_webutils.cpp \
   ../../../src/declarativewebutils.cpp

HEADERS += ../../../src/declarativewebutils.h

INCLUDEPATH -= $$absolute_path(../../../../qtmozembed/src)
