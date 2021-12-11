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

include(QStacker/QStacker.pri)
include(fmt/fmt.pri)
