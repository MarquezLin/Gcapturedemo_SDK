#ifndef CAPTURE_SDK_H
#define CAPTURE_SDK_H

#pragma once
#include <stdint.h>

#ifdef _WIN32
  #ifdef CAPTURESDK_LIBRARY
    #define CAPSDK_API __declspec(dllexport)
  #else
    #define CAPSDK_API __declspec(dllimport)
  #endif
#else
  #define CAPSDK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* cap_handle_t;

typedef enum {
    CAP_OK = 0,
    CAP_E_INVALID = -1,
    CAP_E_INTERNAL = -100
} cap_result_t;

CAPSDK_API cap_result_t cap_create(cap_handle_t* out);
CAPSDK_API cap_result_t cap_destroy(cap_handle_t h);

#ifdef __cplusplus
}
#endif


#endif // CAPTURE_SDK_H
