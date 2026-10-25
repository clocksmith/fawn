/* SPDX-License-Identifier: MIT
 * Compile-only comparison with the installed Apple SDK. No device is created. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
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
SAME_LAYOUT(doe_mtl_drawable_size, CGSize);
SAME_FIELD(doe_mtl_drawable_size, CGSize, height);
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
_Static_assert(sizeof(MTLDispatchThreadgroupsIndirectArguments) == 3 * sizeof(uint32_t), "Indirect XYZ byte span");
_Static_assert(_Alignof(MTLDispatchThreadgroupsIndirectArguments) == _Alignof(uint32_t), "Indirect XYZ alignment");
_Static_assert(offsetof(MTLDispatchThreadgroupsIndirectArguments, threadgroupsPerGrid) == 0, "Indirect XYZ offset");
_Static_assert(sizeof(MTLDrawPrimitivesIndirectArguments) == 4*sizeof(uint32_t), "Draw indirect byte span");
_Static_assert(offsetof(MTLDrawPrimitivesIndirectArguments, baseInstance) == 3*sizeof(uint32_t), "Draw base-instance offset");
_Static_assert(sizeof(MTLDrawIndexedPrimitivesIndirectArguments) == 5*sizeof(uint32_t), "Indexed indirect byte span");
_Static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, indexStart) == 2*sizeof(uint32_t), "Indexed first-index offset");
_Static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, baseVertex) == 3*sizeof(uint32_t), "Indexed base-vertex offset");
_Static_assert(offsetof(MTLDrawIndexedPrimitivesIndirectArguments, baseInstance) == 4*sizeof(uint32_t), "Indexed base-instance offset");
_Static_assert(__builtin_types_compatible_p(__typeof__(((MTLDrawIndexedPrimitivesIndirectArguments *)0)->baseVertex), int32_t), "Indirect signed base vertex");

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
SAME_ENUM(DOE_MTL_VERTEX_UCHAR, MTLVertexFormatUChar);
SAME_ENUM(DOE_MTL_VERTEX_UCHAR2, MTLVertexFormatUChar2);
SAME_ENUM(DOE_MTL_VERTEX_UCHAR4, MTLVertexFormatUChar4);
SAME_ENUM(DOE_MTL_VERTEX_CHAR, MTLVertexFormatChar);
SAME_ENUM(DOE_MTL_VERTEX_CHAR2, MTLVertexFormatChar2);
SAME_ENUM(DOE_MTL_VERTEX_CHAR4, MTLVertexFormatChar4);
SAME_ENUM(DOE_MTL_VERTEX_UCHAR_NORMALIZED, MTLVertexFormatUCharNormalized);
SAME_ENUM(DOE_MTL_VERTEX_UCHAR2_NORMALIZED, MTLVertexFormatUChar2Normalized);
SAME_ENUM(DOE_MTL_VERTEX_UCHAR4_NORMALIZED, MTLVertexFormatUChar4Normalized);
SAME_ENUM(DOE_MTL_VERTEX_CHAR_NORMALIZED, MTLVertexFormatCharNormalized);
SAME_ENUM(DOE_MTL_VERTEX_CHAR2_NORMALIZED, MTLVertexFormatChar2Normalized);
SAME_ENUM(DOE_MTL_VERTEX_CHAR4_NORMALIZED, MTLVertexFormatChar4Normalized);
SAME_ENUM(DOE_MTL_VERTEX_USHORT, MTLVertexFormatUShort);
SAME_ENUM(DOE_MTL_VERTEX_USHORT2, MTLVertexFormatUShort2);
SAME_ENUM(DOE_MTL_VERTEX_USHORT4, MTLVertexFormatUShort4);
SAME_ENUM(DOE_MTL_VERTEX_SHORT, MTLVertexFormatShort);
SAME_ENUM(DOE_MTL_VERTEX_SHORT2, MTLVertexFormatShort2);
SAME_ENUM(DOE_MTL_VERTEX_SHORT4, MTLVertexFormatShort4);
SAME_ENUM(DOE_MTL_VERTEX_USHORT_NORMALIZED, MTLVertexFormatUShortNormalized);
SAME_ENUM(DOE_MTL_VERTEX_USHORT2_NORMALIZED, MTLVertexFormatUShort2Normalized);
SAME_ENUM(DOE_MTL_VERTEX_USHORT4_NORMALIZED, MTLVertexFormatUShort4Normalized);
SAME_ENUM(DOE_MTL_VERTEX_SHORT_NORMALIZED, MTLVertexFormatShortNormalized);
SAME_ENUM(DOE_MTL_VERTEX_SHORT2_NORMALIZED, MTLVertexFormatShort2Normalized);
SAME_ENUM(DOE_MTL_VERTEX_SHORT4_NORMALIZED, MTLVertexFormatShort4Normalized);
SAME_ENUM(DOE_MTL_VERTEX_HALF, MTLVertexFormatHalf);
SAME_ENUM(DOE_MTL_VERTEX_HALF2, MTLVertexFormatHalf2);
SAME_ENUM(DOE_MTL_VERTEX_HALF4, MTLVertexFormatHalf4);
SAME_ENUM(DOE_MTL_VERTEX_FLOAT, MTLVertexFormatFloat);
SAME_ENUM(DOE_MTL_VERTEX_FLOAT2, MTLVertexFormatFloat2);
SAME_ENUM(DOE_MTL_VERTEX_FLOAT3, MTLVertexFormatFloat3);
SAME_ENUM(DOE_MTL_VERTEX_FLOAT4, MTLVertexFormatFloat4);
SAME_ENUM(DOE_MTL_VERTEX_UINT, MTLVertexFormatUInt);
SAME_ENUM(DOE_MTL_VERTEX_UINT2, MTLVertexFormatUInt2);
SAME_ENUM(DOE_MTL_VERTEX_UINT3, MTLVertexFormatUInt3);
SAME_ENUM(DOE_MTL_VERTEX_UINT4, MTLVertexFormatUInt4);
SAME_ENUM(DOE_MTL_VERTEX_INT, MTLVertexFormatInt);
SAME_ENUM(DOE_MTL_VERTEX_INT2, MTLVertexFormatInt2);
SAME_ENUM(DOE_MTL_VERTEX_INT3, MTLVertexFormatInt3);
SAME_ENUM(DOE_MTL_VERTEX_INT4, MTLVertexFormatInt4);
SAME_ENUM(DOE_MTL_VERTEX_UINT1010102_NORMALIZED, MTLVertexFormatUInt1010102Normalized);
SAME_ENUM(DOE_MTL_VERTEX_UCHAR4_NORMALIZED_BGRA, MTLVertexFormatUChar4Normalized_BGRA);

/* These methods are declared by the SDK with the types passed through the
 * backend's typed dispatcher. Keep native records at the call site so a changed
 * SDK declaration produces a compile diagnostic rather than an untyped cast. */
void doe_mtl_sdk_calls(id<MTLComputeCommandEncoder> compute,
    id<MTLRenderCommandEncoder> render, id<MTLBlitCommandEncoder> blit,
    id<MTLBuffer> buffer, id<MTLCounterSampleBuffer> counters, MTLRenderPipelineDescriptor *pipeline) {
    [pipeline setAlphaToCoverageEnabled:(BOOL)YES];
    [render drawPrimitives:MTLPrimitiveTypeTriangle indirectBuffer:buffer indirectBufferOffset:(NSUInteger)0];
    [render drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt16
        indexBuffer:buffer indexBufferOffset:(NSUInteger)2 indirectBuffer:buffer indirectBufferOffset:(NSUInteger)0];
    [compute dispatchThreadgroups:(MTLSize){1, 1, 1} threadsPerThreadgroup:(MTLSize){1, 1, 1}];
    [compute dispatchThreadgroupsWithIndirectBuffer:buffer indirectBufferOffset:(NSUInteger)4
        threadsPerThreadgroup:(MTLSize){1, 1, 1}];
    [render setViewport:(MTLViewport){0, 0, 1, 1, 0, 1}];
    [render setScissorRect:(MTLScissorRect){0, 0, 1, 1}];
    [render drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:3
        indexType:MTLIndexTypeUInt32 indexBuffer:buffer indexBufferOffset:0
        instanceCount:1 baseVertex:(NSInteger)-1 baseInstance:0];
    [blit fillBuffer:buffer range:(NSRange){0, 4} value:(uint8_t)0];
    [blit resolveCounters:counters inRange:(NSRange){0, 1}
        destinationBuffer:buffer destinationOffset:0];
}

void doe_mtl_sdk_surface_calls(CAMetalLayer *layer, id<MTLDevice> device,
    id<MTLCommandBuffer> command, id<CAMetalDrawable> drawable) {
    [layer setDevice:device];
    [layer setPixelFormat:MTLPixelFormatBGRA8Unorm];
    [layer setDrawableSize:(CGSize){64, 32}];
    [layer setFramebufferOnly:(BOOL)YES];
    [layer setAllowsNextDrawableTimeout:(BOOL)YES];
    [layer setDisplaySyncEnabled:(BOOL)YES];
    [layer setOpaque:(BOOL)YES];
    (void)[layer nextDrawable];
    (void)[drawable texture];
    CFTimeInterval presented = [drawable presentedTime];
    (void)presented;
    [command presentDrawable:drawable];
}

void doe_mtl_sdk_timestamp_ordering(id<MTLDevice> device, id<MTLCommandBuffer> command) {
    id<MTLSharedEvent> event = [device newSharedEvent];
    [command encodeSignalEvent:event value:(uint64_t)1];
    [command encodeWaitForEvent:event value:(uint64_t)1];
    [event release];
}
