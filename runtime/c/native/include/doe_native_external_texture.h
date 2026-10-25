/* SPDX-License-Identifier: MIT
 * Retained Doe external-texture ABI from native/resource/doe_external_texture_native.zig.
 * The pinned webgpu.h has lifecycle and binding types but no import descriptor. */
#ifndef DOE_NATIVE_EXTERNAL_TEXTURE_H
#define DOE_NATIVE_EXTERNAL_TEXTURE_H
#include "webgpu.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
#define DOE_NATIVE_EXTENSION __attribute__((visibility("default")))
#else
#define DOE_NATIVE_EXTENSION
#endif

#define DOE_NATIVE_STYPE_EXTERNAL_TEXTURE UINT32_C(0xD0E10100)
enum { DOE_NATIVE_EXTERNAL_IOSURFACE = 1, DOE_NATIVE_EXTERNAL_CVPIXELBUFFER = 2 };
/* The retained descriptor prefix. Additional Dawn conversion fields are not
 * consumed by this ABI; importing does not promise YUV/color-space conversion. */
typedef struct DoeExternalTextureDescriptor {
    const WGPUChainedStruct *nextInChain;
    WGPUStringView label;
    WGPUTextureView plane0;
    WGPUTextureView plane1;
} DoeExternalTextureDescriptor;
typedef struct DoeExternalTextureNativeDescriptor {
    WGPUChainedStruct chain;
    uint32_t sourceType;
    void *handle;
} DoeExternalTextureNativeDescriptor;

/* With no chain, retain one or two same-device texture views. A native chain
 * imports a borrowed IOSurface or IOSurface-backed CVPixelBuffer on Metal only.
 * The caller completes producer writes before import and keeps source contents
 * stable until all resulting command/resource leases settle. Failed import is
 * reported, never replaced by a view fallback. NULL descriptor/device returns
 * NULL. Expire/destroy invalidate future binding/use; refresh revalidates the
 * existing planes and device. Commands already accepted keep their leases. */
DOE_NATIVE_EXTENSION WGPUExternalTexture doeNativeDeviceCreateExternalTexture(WGPUDevice device, const void *descriptor);
DOE_NATIVE_EXTENSION WGPUExternalTexture doeNativeDeviceImportExternalTexture(WGPUDevice device, const void *descriptor);
DOE_NATIVE_EXTENSION void doeNativeExternalTextureAddRef(WGPUExternalTexture texture);
DOE_NATIVE_EXTENSION void doeNativeExternalTextureRelease(WGPUExternalTexture texture);
DOE_NATIVE_EXTENSION void doeNativeExternalTextureDestroy(WGPUExternalTexture texture);
DOE_NATIVE_EXTENSION void doeNativeExternalTextureExpire(WGPUExternalTexture texture);
DOE_NATIVE_EXTENSION void doeNativeExternalTextureRefresh(WGPUExternalTexture texture);
DOE_NATIVE_EXTENSION void doeNativeExternalTextureSetLabel(WGPUExternalTexture texture, const char *label, size_t length);
#ifdef __cplusplus
}
#endif
#endif
