#include "capture_sdk.h"
#include <new>

struct CapCtx {
    int dummy;
};

cap_result_t cap_create(cap_handle_t* out) {
    if (!out) return CAP_E_INVALID;
    auto* ctx = new (std::nothrow) CapCtx();
    if (!ctx) return CAP_E_INTERNAL;
    *out = (cap_handle_t)ctx;
    return CAP_OK;
}

cap_result_t cap_destroy(cap_handle_t h) {
    if (!h) return CAP_E_INVALID;
    delete (CapCtx*)h;
    return CAP_OK;
}
