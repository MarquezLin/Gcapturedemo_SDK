QT -= gui widgets
QT += core

TEMPLATE = lib
CONFIG += dll c++11
TARGET = CaptureSDK

DEFINES += CAPTURESDK_LIBRARY

INCLUDEPATH += $$PWD/include \
               $$PWD/include/internal \
               $$PWD/src \
               $$PWD/src/internal

INCLUDEPATH += $$PWD/include/internal/common \
               $$PWD/src/internal/common


HEADERS += \
    include/capture_sdk.h \
    include/internal/video_card.h \
    include/internal/task.h \
    include/internal/xdma_public.h \
    include/internal/common/utils.h

SOURCES += \
    src/capture_sdk.cpp \
    src/internal/video_card.cpp \
    src/internal/task.cpp \
    src/internal/common/utils.cpp

LIBS += -lSetupAPI
