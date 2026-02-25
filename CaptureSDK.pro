QT -= gui widgets
QT += core \
    widgets

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
    include/capture_sdk_log.h \
    include/internal/video_card.h \
    include/internal/task.h \
    include/internal/xdma_public.h \
    include/internal/common/utils.h \
    include/internal/cap_log_internal.h

SOURCES += \
    src/capture_sdk.cpp \
    src/cap_log.cpp \
    src/internal/video_card.cpp \
    src/internal/common/utils.cpp

LIBS += -lSetupAPI
