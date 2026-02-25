#pragma once

// Define CAPTURESDK_DEBUG to enable CAP_LOG output.
// Example: add target_compile_definitions(CaptureSDK PRIVATE CAPTURESDK_DEBUG) in CMake.
// #define CAPTURESDK_DEBUG

#include "capture_sdk_log.h"

#ifdef CAPTURESDK_DEBUG
  #include <chrono>
  #include <cstdio>
  #include <cstdarg>

  void cap_internal_log(cap_log_level_t level, const char* fmt, ...);

  #define CAP_LOG(level, fmt, ...)           cap_internal_log(level, fmt, ##__VA_ARGS__)
#else
  #define CAP_LOG(level, fmt, ...) ((void)0)
#endif
