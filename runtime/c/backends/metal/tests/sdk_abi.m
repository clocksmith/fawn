/* SPDX-License-Identifier: MIT
 * Compile-only comparison with the installed Apple SDK. No device is created. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../src/objc_abi.h"
#include <stddef.h>

#define SAME_LAYOUT(local, sdk) \
    _Static_assert(sizeof(local) == sizeof(sdk), #local " size differs from Apple SDK"); \
    _Static_assert(_Alignof(local) == _Alignof(sdk), #local " alignment differs from Apple SDK")
#define SAME_FIELD(local, sdk, field) \
    _Static_assert(offsetof(local, field) == offsetof(sdk, field), #local "." #field " offset differs from Apple SDK")
#define SAME_ENUM(local, sdk) \
    _Static_assert((unsigned long)(local) == (unsigned long)(sdk), #local " differs from Apple SDK")

_Static_assert(__builtin_types_compatible_p(size_t, NSUInteger), "NSUInteger call ABI");
_Static_assert(__builtin_types_compatible_p(intptr_t, NSInteger), "NSInteger call ABI");
_Static_assert(__builtin_types_compatible_p(doe_objc_bool, BOOL), "BOOL call ABI");
SAME_LAYOUT(doe_mtl_size, MTLSize);
SAME_FIELD(doe_mtl_size, MTLSize, width);
SAME_FIELD(doe_mtl_size, MTLSize, height);
SAME_FIELD(doe_mtl_size, MTLSize, depth);
SAME_LAYOUT(doe_mtl_origin, MTLOrigin);
SAME_FIELD(doe_mtl_origin, MTLOrigin, z);
SAME_LAYOUT(doe_mtl_range_abi, NSRange);
SAME_FIELD(doe_mtl_range_abi, NSRange, length);
SAME_LAYOUT(doe_mtl_swizzle_abi, MTLTextureSwizzleChannels);
SAME_FIELD(doe_mtl_swizzle_abi, MTLTextureSwizzleChannels, alpha);
SAME_LAYOUT(doe_mtl_clear_color, MTLClearColor);
SAME_FIELD(doe_mtl_clear_color, MTLClearColor, alpha);
SAME_LAYOUT(doe_mtl_viewport, MTLViewport);
_Static_assert(offsetof(doe_mtl_viewport, min_depth) == offsetof(MTLViewport, znear), "Viewport near-depth offset");
_Static_assert(offsetof(doe_mtl_viewport, max_depth) == offsetof(MTLViewport, zfar), "Viewport far-depth offset");
SAME_LAYOUT(doe_mtl_scissor_abi, MTLScissorRect);
SAME_FIELD(doe_mtl_scissor_abi, MTLScissorRect, height);
_Static_assert(sizeof(MTLCounterResultTimestamp) == sizeof(uint64_t), "Timestamp resolve result ABI");

SAME_ENUM(DOE_MTL_TEXTURE_1D, MTLTextureType1D);
SAME_ENUM(DOE_MTL_TEXTURE_2D_ARRAY, MTLTextureType2DArray);
SAME_ENUM(DOE_MTL_TEXTURE_2D_MS, MTLTextureType2DMultisample);
SAME_ENUM(DOE_MTL_TEXTURE_2D_MS_ARRAY, MTLTextureType2DMultisampleArray);
SAME_ENUM(DOE_MTL_TEXTURE_CUBE_ARRAY, MTLTextureTypeCubeArray);
SAME_ENUM(DOE_MTL_TEXTURE_3D, MTLTextureType3D);
SAME_ENUM(DOE_MTL_RGBA8_UNORM, MTLPixelFormatRGBA8Unorm);
SAME_ENUM(DOE_MTL_BGRA8_SRGB, MTLPixelFormatBGRA8Unorm_sRGB);
SAME_ENUM(DOE_MTL_RGBA32_FLOAT, MTLPixelFormatRGBA32Float);
SAME_ENUM(DOE_MTL_DEPTH24_STENCIL8, MTLPixelFormatDepth24Unorm_Stencil8);
SAME_ENUM(DOE_MTL_DEPTH32_STENCIL8, MTLPixelFormatDepth32Float_Stencil8);
SAME_ENUM(DOE_MTL_X32_STENCIL8, MTLPixelFormatX32_Stencil8);
SAME_ENUM(DOE_MTL_BC6H_RGB_FLOAT, MTLPixelFormatBC6H_RGBFloat);
SAME_ENUM(DOE_MTL_BC6H_RGB_UFLOAT, MTLPixelFormatBC6H_RGBUfloat);
SAME_ENUM(DOE_MTL_LOAD_DISCARD, MTLLoadActionDontCare);
SAME_ENUM(DOE_MTL_LOAD, MTLLoadActionLoad);
SAME_ENUM(DOE_MTL_CLEAR, MTLLoadActionClear);
SAME_ENUM(DOE_MTL_STORE_DISCARD, MTLStoreActionDontCare);
SAME_ENUM(DOE_MTL_STORE, MTLStoreActionStore);
SAME_ENUM(DOE_MTL_RESOLVE, MTLStoreActionMultisampleResolve);
SAME_ENUM(DOE_MTL_STORE_RESOLVE, MTLStoreActionStoreAndMultisampleResolve);
SAME_ENUM(DOE_MTL_SWIZZLE_ALPHA, MTLTextureSwizzleAlpha);

/* These methods are declared by the SDK with the types passed through the
 * backend's typed dispatcher. Keep native records at the call site so a changed
 * SDK declaration produces a compile diagnostic rather than an untyped cast. */
void doe_mtl_sdk_calls(id<MTLComputeCommandEncoder> compute,
    id<MTLRenderCommandEncoder> render, id<MTLBlitCommandEncoder> blit,
    id<MTLBuffer> buffer, id<MTLCounterSampleBuffer> counters) {
    [compute dispatchThreadgroups:(MTLSize){1, 1, 1} threadsPerThreadgroup:(MTLSize){1, 1, 1}];
    [render setViewport:(MTLViewport){0, 0, 1, 1, 0, 1}];
    [render setScissorRect:(MTLScissorRect){0, 0, 1, 1}];
    [render drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:3
        indexType:MTLIndexTypeUInt32 indexBuffer:buffer indexBufferOffset:0
        instanceCount:1 baseVertex:(NSInteger)-1 baseInstance:0];
    [blit fillBuffer:buffer range:(NSRange){0, 4} value:(uint8_t)0];
    [blit resolveCounters:counters inRange:(NSRange){0, 1}
        destinationBuffer:buffer destinationOffset:0];
}
