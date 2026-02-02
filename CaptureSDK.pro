QT -= gui widgets
QT += core

TEMPLATE = lib
CONFIG += dll c++11
TARGET = CaptureSDK

DEFINES += CAPTURESDK_LIBRARY

INCLUDEPATH += $$PWD/include \
               $$PWD/src

SOURCES += src/capture_sdk.cpp
HEADERS += include/capture_sdk.h

LIBS += -lSetupAPI
