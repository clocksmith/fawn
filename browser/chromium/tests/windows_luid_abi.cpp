// Compile-only control with actual pinned Windows headers; no driver execution.
// clang-cl /std:c++20, Windowsx64 MSVC ABI, Chromium generated Dawn includes.
#if !defined(_WIN32) || !defined(_M_X64)
#error "This control requires the Windowsx64 compiler/SDK; do not spoof platform macros"
#endif
#include "dawn/native/D3DBackend.h"
#include "src/dawn/native/webgpu/InnerRuntimeDescriptor.h"
#include "../../../runtime/c/native/src/adapter_request.h"
#include <cstddef>
#include <cstdint>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
using NativeLuid = dawn::native::d3d::RequestAdapterOptionsLUID;
static_assert(sizeof(NativeLuid)==24 && alignof(NativeLuid)==8);
static_assert(sizeof(native_windows_adapter_luid)==sizeof(NativeLuid));
static_assert(alignof(native_windows_adapter_luid)==alignof(NativeLuid));
static_assert(offsetof(NativeLuid,adapterLUID)==offsetof(native_windows_adapter_luid,luid_low));
static_assert(offsetof(NativeLuid,adapterLUID)+offsetof(LUID,HighPart)==
              offsetof(native_windows_adapter_luid,luid_high));
static_assert(sizeof(LUID)==8 && sizeof(LUID::LowPart)==4 && sizeof(LUID::HighPart)==4);
static_assert(static_cast<uint32_t>(wgpu::SType::RequestAdapterOptionsLUID)==
              NATIVE_DAWN_REQUEST_ADAPTER_LUID);
using ApiLuid = dawn::native::webgpu::InnerRuntimeWindowsAdapterLuid;
static_assert(std::is_standard_layout_v<ApiLuid>);
static_assert(sizeof(ApiLuid)==sizeof(native_windows_adapter_luid));
static_assert(alignof(ApiLuid)==alignof(native_windows_adapter_luid));
static_assert(offsetof(ApiLuid,adapterLUID)==offsetof(native_windows_adapter_luid,luid_low));
static_assert(offsetof(ApiLuid,adapterLUID)+offsetof(LUID,HighPart)==
              offsetof(native_windows_adapter_luid,luid_high));
WGPUChainedStruct* CompileActualWindowsLuidProjection(
    WGPUDawnTogglesDescriptor* toggles, WGPUChainedStruct* chain,
    const NativeLuid* luid, bool external, ApiLuid& storage) {
    return dawn::native::webgpu::InnerRuntimeWindowsAdapterChain(
        toggles,chain,luid,external,storage);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
