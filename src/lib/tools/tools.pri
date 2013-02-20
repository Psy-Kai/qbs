INCLUDEPATH += $$PWD/../.. # for plugins

HEADERS += \
    $$PWD/codelocation.h \
    $$PWD/error.h \
    $$PWD/fileinfo.h \
    $$PWD/filetime.h \
    $$PWD/id.h \
    $$PWD/persistence.h \
    $$PWD/scannerpluginmanager.h \
    $$PWD/scripttools.h \
    $$PWD/settings.h \
    $$PWD/preferences.h \
    $$PWD/profile.h \
    $$PWD/processresult.h \
    $$PWD/progressobserver.h \
    $$PWD/hostosinfo.h \
    $$PWD/buildoptions.h \
    $$PWD/installoptions.h \
    $$PWD/setupprojectparameters.h \
    $$PWD/persistentobject.h \
    $$PWD/weakpointer.h \
    $$PWD/qbs_export.h \
    $$PWD/qbsassert.h \
    $$PWD/propertyfinder.h

SOURCES += \
    $$PWD/codelocation.cpp \
    $$PWD/error.cpp \
    $$PWD/fileinfo.cpp \
    $$PWD/id.cpp \
    $$PWD/persistence.cpp \
    $$PWD/scannerpluginmanager.cpp \
    $$PWD/scripttools.cpp \
    $$PWD/settings.cpp \
    $$PWD/preferences.cpp \
    $$PWD/profile.cpp \
    $$PWD/progressobserver.cpp \
    $$PWD/buildoptions.cpp \
    $$PWD/installoptions.cpp \
    $$PWD/setupprojectparameters.cpp \
    $$PWD/qbsassert.cpp \
    $$PWD/propertyfinder.cpp

win32 {
    SOURCES += $$PWD/filetime_win.cpp
}

unix {
    SOURCES += $$PWD/filetime_unix.cpp
}

all_tests {
    HEADERS += $$PWD/tst_tools.h
    SOURCES += $$PWD/tst_tools.cpp
}

tools_headers.files = \
    $$PWD/codelocation.h \
    $$PWD/error.h \
    $$PWD/settings.h \
    $$PWD/preferences.h \
    $$PWD/profile.h \
    $$PWD/processresult.h \
    $$PWD/buildoptions.h \
    $$PWD/installoptions.h \
    $$PWD/setupprojectparameters.h \
    $$PWD/qbs_export.h
tools_headers.path = /include/qbs/tools
INSTALLS += tools_headers
