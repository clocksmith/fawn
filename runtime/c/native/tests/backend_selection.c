/* Fallback descriptor admission shares this existing selection-policy fixture. */
#if defined(DOE_ADAPTER_REQUEST_UNIT_ONLY)
/* SPDX-License-Identifier: MIT */
#include "../src/adapter_request.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* Explicit Windows C payload, never a host-Itanium native C++ object. */
    native_windows_adapter_luid luid = {
        .chain = {.sType = (WGPUSType)NATIVE_DAWN_REQUEST_ADAPTER_LUID},
        .luid_low = UINT32_C(0xfedcba98), .luid_high = INT32_MIN + 17
    };
    native_adapter_selection selected = {0};
    WGPURequestAdapterOptions options = {.backendType = WGPUBackendType_D3D12,
        .nextInChain = &luid.chain};
    native_windows_adapter_luid original = luid;
    assert(native_adapter_request_decode(WGPUBackendType_D3D12, &options, &selected));
    assert(selected.has_luid && selected.luid_low == luid.luid_low && selected.luid_high == luid.luid_high);
    assert(memcmp(&original,&luid,sizeof(luid))==0);
    options.forceFallbackAdapter = true;
    assert(!native_adapter_request_decode(WGPUBackendType_D3D12, &options, &selected));
    options.forceFallbackAdapter = false;
    assert(!native_adapter_request_decode(WGPUBackendType_Vulkan, &options, &selected));
    assert(!native_adapter_request_decode(WGPUBackendType_Metal, &options, &selected));
    luid.chain.next = &luid.chain;
    assert(!native_adapter_request_decode(WGPUBackendType_D3D12, &options, &selected));
    native_windows_adapter_luid duplicate = original;
    luid.chain.next = &duplicate.chain;
    assert(!native_adapter_request_decode(WGPUBackendType_D3D12, &options, &selected));
    WGPUChainedStruct unknown = {.sType=(WGPUSType)UINT32_C(0x7fffff00)};
    luid.chain.next = &unknown;
    assert(!native_adapter_request_decode(WGPUBackendType_D3D12, &options, &selected));
    options.nextInChain = &unknown;
    unknown.next = &unknown;
    assert(!native_adapter_request_decode(WGPUBackendType_D3D12, &options, &selected));
    assert(native_adapter_request_decode(WGPUBackendType_Vulkan, NULL, &selected));
    assert(!selected.has_luid);
    assert(!native_adapter_request_decode(WGPUBackendType_D3D12, &options, NULL));
    puts("adapter request: Windows payload and strict chain host controls PASS; no SDK/adapter claim");
    return 0;
}

#elif defined(DOE_ADAPTER_REQUEST_API_CONTROL)
#include "consumer.h"
#include "../src/adapter_request.h"
#include "../src/backend.h"

static struct { unsigned calls,replies; uint32_t low; int32_t high; } request_ledger;
native_result __wrap_native_backend_open_luid(uint32_t low,int32_t high,
    uint32_t surface,native_backend *out)
{
    (void)out;
    assert(!surface);
    ++request_ledger.calls;
    request_ledger.low=low;
    request_ledger.high=high;
    return NATIVE_UNAVAILABLE; /* Typed refusal: never fake an adapter/device. */
}
static void request_refused(WGPURequestAdapterStatus status,WGPUAdapter adapter,
    WGPUStringView message,void *first,void *second)
{
    (void)message; (void)first; (void)second;
    assert(status==WGPURequestAdapterStatus_Unavailable && !adapter);
    ++request_ledger.replies;
}
int main(void)
{
    WGPUInstance instance=wgpuCreateInstance(NULL);
    assert(instance);
    native_windows_adapter_luid luid={.chain={.sType=(WGPUSType)NATIVE_DAWN_REQUEST_ADAPTER_LUID},
        .luid_low=UINT32_C(0xfedcba98),.luid_high=INT32_MIN+17};
    WGPURequestAdapterOptions options={.backendType=WGPUBackendType_D3D12,.nextInChain=&luid.chain};
    WGPURequestAdapterCallbackInfo callback={.mode=WGPUCallbackMode_AllowSpontaneous,.callback=request_refused};
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    assert(request_ledger.calls==1 && request_ledger.replies==1);
    assert(request_ledger.low==luid.luid_low && request_ledger.high==luid.luid_high);
    options.forceFallbackAdapter=WGPU_TRUE;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    assert(request_ledger.calls==1 && request_ledger.replies==2);
    options.forceFallbackAdapter=WGPU_FALSE;
    luid.chain.next=&luid.chain;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    assert(request_ledger.calls==1 && request_ledger.replies==3);
    WGPUChainedStruct unknown={.sType=(WGPUSType)UINT32_C(0x7fffff00)};
    luid.chain.next=&unknown;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    assert(request_ledger.calls==1 && request_ledger.replies==4);
    options.backendType=WGPUBackendType_Metal;
    luid.chain.next=NULL;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    assert(request_ledger.calls==1 && request_ledger.replies==5);
    wgpuInstanceRelease(instance);
    puts("PASS public adapter LUID request: actual callback/conversion with refusing lower call ledger; no GPU/SDK");
    return 0;
}

#else
/* SPDX-License-Identifier: MIT */
#include "consumer.h"

static void unavailable(WGPURequestAdapterStatus status, WGPUAdapter adapter,
    WGPUStringView message, void *userdata, void *unused)
{
    (void)message;
    (void)unused;
    assert(status == WGPURequestAdapterStatus_Unavailable && !adapter);
    ++*(unsigned *)userdata;
}

int main(void)
{
    WGPUInstance instance = wgpuCreateInstance(NULL);
    unsigned replies = 0;
    WGPURequestAdapterCallbackInfo callback = {.mode=WGPUCallbackMode_AllowSpontaneous,
        .callback=unavailable,.userdata1=&replies};
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = WGPUBackendType_D3D12;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    options.backendType = WGPUBackendType_Metal;
    options.forceFallbackAdapter = WGPU_TRUE;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
#if !defined(__APPLE__)
    options.forceFallbackAdapter = WGPU_FALSE;
    assert(wgpuInstanceRequestAdapter(instance,&options,callback).id);
    assert(replies == 3);
#else
    assert(replies == 2);
#endif
    wgpuInstanceRelease(instance);
    puts("PASS explicit backend selection: unsupported backend and Metal CPU request do not fall back; host loader absence remains unavailable");
}

#endif
