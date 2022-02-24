TEMPLATE = app
CONFIG += console c++2a
CONFIG -= app_bundle
CONFIG += core

QMAKE_LINK = g++-10
QMAKE_CXX = g++-10
QMAKE_CC = gcc-10

QT -= gui
CONFIG -= qml_debug

SOURCES += \
        main.cpp
		
LIBS += /usr/local/lib64/libfmt.a
QMAKE_CXXFLAGS += -msse4.2
QMAKE_CXXFLAGS += -fno-omit-frame-pointer
QMAKE_CXXFLAGS += -O0 -ggdb3

QMAKE_LFLAGS += "-Wl,--dynamic-linker=/srv/lib515/ld-linux-x86-64.so.2"


include(QStacker/QStacker.pri)
