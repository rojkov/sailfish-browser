TARGET = tst_webview

CONFIG += link_pkgconfig

PKGCONFIG += nemotransferengine-qt5 mlite5

QT += quick concurrent sql gui-private

include(../test_common.pri)
include(../mocks/downloadmanager/downloadmanager_mock.pri)
include(../mocks/declarativewebutils/declarativewebutils_mock.pri)
include(../common/testobject.pri)
include(../../../src/core/core.pri)
include(../../../src/qtmozembed/qtmozembed.pri)
include(../../../src/factories/factories.pri)
include(../../../src/bookmarks/bookmarks.pri)
include(../../../src/history/history.pri)

SOURCES += tst_webview.cpp

OTHER_FILES = *.qml

RESOURCES = tst_webview.qrc
