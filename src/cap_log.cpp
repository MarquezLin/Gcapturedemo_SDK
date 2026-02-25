#include "cap_log_internal.h"

#ifdef CAPTURESDK_DEBUG
  #include <cstring>
#endif

static cap_log_callback_t g_log_cb = nullptr;

extern "C" void cap_set_log_callback(cap_log_callback_t cb)
{
    g_log_cb = cb;
}

#ifdef CAPTURESDK_DEBUG

static const char* level_str(cap_log_level_t lv)
{
    switch (lv) {
        case CAP_LOG_INFO: return "INFO";
        case CAP_LOG_WARN: return "WARN";
        case CAP_LOG_ERR:  return "ERR ";
        default:           return "UNKN";
    }
}

void cap_internal_log(cap_log_level_t level, const char* fmt, ...)
{
    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    char final_msg[1200];
    snprintf(final_msg, sizeof(final_msg),
             "[CAPSDK %lld us][%s] %s",
             (long long)us,
             level_str(level),
             buffer);

    std::fprintf(stderr, "%s\n", final_msg);

    if (g_log_cb)
        g_log_cb(level, final_msg);
}

#endif
