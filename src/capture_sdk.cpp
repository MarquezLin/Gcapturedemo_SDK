#include "cap_log_internal.h"
#include "capture_sdk.h"
#include "internal/video_card.h"
#include <new>
#include <mutex>

struct CapCtx
{
    VideoCard card;
    std::mutex out_mtx;
};

static void dbgprintf(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

cap_result_t cap_create(cap_handle_t *out)
{
    if (!out)
        return CAP_E_INVALID;
    auto *ctx = new (std::nothrow) CapCtx();
    if (!ctx)
        return CAP_E_INTERNAL;
    *out = (cap_handle_t)ctx;
    return CAP_OK;
}

cap_result_t cap_destroy(cap_handle_t h)
{
    if (!h)
        return CAP_E_INVALID;
    delete (CapCtx *)h;
    return CAP_OK;
}

cap_result_t cap_init(cap_handle_t h, int width, int height)
{
    if (!h)
        return CAP_E_INVALID;
    auto *ctx = (CapCtx *)h;
    int r = ctx->card.init(width, height);
    // return (r == 0) ? CAP_OK : CAP_E_INTERNAL;
    if (r == 0)
        return CAP_OK;
    if (r == -2)
        return CAP_E_DEVICE_NOT_FOUND;
    return CAP_E_OPEN_DEVICE;
}

cap_result_t cap_uninit(cap_handle_t h)
{
    if (!h)
        return CAP_E_INVALID;
    auto *ctx = (CapCtx *)h;
    ctx->card.uninit();
    return CAP_OK;
}

cap_result_t cap_start_capture(cap_handle_t h, cap_mode_t mode, cap_video_cb_t cb, void *user)
{
    if (!h || !cb)
        return CAP_E_INVALID;
    auto *ctx = (CapCtx *)h;
    dbgprintf("[cap] mode=%d, h=%p, cb=%p\n", mode, h, cb);
    CAP_LOG(CAP_LOG_INFO, "[cap] mode=%d, h=%p, cb=%p\n", mode, h, cb);


    int r = ctx->card.startCapture((mode == CAP_MODE_SINGLE)
                                       ? CaptureMode::Single
                                       : CaptureMode::Continuous,
                                   [=](uchar *data, int w, int hh)
                                   {
        cb((const uint8_t*)data, w, hh, 3, user); });

    CAP_LOG(CAP_LOG_INFO, "[cap] mode=%d, h=%p, cb=%p\n", mode, h, cb);
    return (r == 0) ? CAP_OK : CAP_E_INTERNAL;
}


cap_result_t cap_capture_step(cap_handle_t h)
{
    if (!h)
        return CAP_E_INVALID;
    auto *ctx = (CapCtx *)h;

    int r = ctx->card.captureStep();
    return (r == 0) ? CAP_OK : CAP_E_INTERNAL;
}

cap_result_t cap_capture_step_timeout(cap_handle_t h, int timeout_ms)
{
    if (!h)
        return CAP_E_INVALID;
    auto *ctx = (CapCtx *)h;

    const int r = ctx->card.captureStepTimeout(timeout_ms);
    if (r == 0)
        return CAP_OK;
    if (r == 1)
        return CAP_E_TIMEOUT;
    return CAP_E_INTERNAL;
}

cap_result_t cap_stop_capture(cap_handle_t h)
{
    if (!h)
        return CAP_E_INVALID;
    auto *ctx = (CapCtx *)h;
    ctx->card.stopCapture();
    return CAP_OK;
}
