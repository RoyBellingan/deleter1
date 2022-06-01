TEMPLATE = app
CONFIG += console c++2a
CONFIG -= app_bundle
CONFIG += core

QT -= gui
CONFIG -= qml_debug

SOURCES += \
        main.cpp
		
QMAKE_CXXFLAGS += -msse4.2
QMAKE_CXXFLAGS += -fno-omit-frame-pointer
QMAKE_CXXFLAGS += -O0 -ggdb3

QMAKE_LFLAGS += "-Wl,--dynamic-linker=/srv/lib515_gcc12/ld-linux-x86-64.so.2"


include(QStacker/QStacker.pri)
include(fmt/fmt.pri)


