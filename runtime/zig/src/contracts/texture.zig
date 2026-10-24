//! Backend-neutral WebGPU texture format and view classifications.

const binding = @import("model/model_binding_value_types.zig");
const texture = @import("model/model_texture_value_types.zig");

/// The sample type of a view: its stencil aspect (a stencil-only view, or a
/// stencil8 texture) samples as uint, a depth-only view as depth, and any other
/// view as its format does
/// (https://gpuweb.github.io/gpuweb/#depth-formats, "aspect-specific format").
pub fn inferViewSampleType(format: texture.WGPUTextureFormat, aspect: u32) u32 {
    if (aspect == texture.WGPUTextureAspect_StencilOnly or format == texture.WGPUTextureFormat_Stencil8)
        return binding.WGPUTextureSampleType_Uint;
    return inferSampleType(format);
}

test "stencil aspects sample as uint and depth aspects as depth" {
    const depth_stencil = texture.WGPUTextureFormat_Depth24PlusStencil8;
    try @import("std").testing.expectEqual(binding.WGPUTextureSampleType_Uint, inferViewSampleType(depth_stencil, texture.WGPUTextureAspect_StencilOnly));
    try @import("std").testing.expectEqual(binding.WGPUTextureSampleType_Depth, inferViewSampleType(depth_stencil, texture.WGPUTextureAspect_DepthOnly));
    try @import("std").testing.expectEqual(binding.WGPUTextureSampleType_Uint, inferViewSampleType(texture.WGPUTextureFormat_Stencil8, texture.WGPUTextureAspect_All));
}

pub fn inferSampleType(format: texture.WGPUTextureFormat) u32 {
    return switch (format) {
        texture.WGPUTextureFormat_Depth16Unorm,
        texture.WGPUTextureFormat_Depth24Plus,
        texture.WGPUTextureFormat_Depth24PlusStencil8,
        texture.WGPUTextureFormat_Depth32Float,
        texture.WGPUTextureFormat_Depth32FloatStencil8,
        => binding.WGPUTextureSampleType_Depth,
        texture.WGPUTextureFormat_R8Uint,
        texture.WGPUTextureFormat_R16Uint,
        texture.WGPUTextureFormat_RG8Uint,
        texture.WGPUTextureFormat_R32Uint,
        texture.WGPUTextureFormat_RG16Uint,
        texture.WGPUTextureFormat_RGBA8Uint,
        texture.WGPUTextureFormat_RGB10A2Uint,
        texture.WGPUTextureFormat_RG32Uint,
        texture.WGPUTextureFormat_RGBA16Uint,
        texture.WGPUTextureFormat_RGBA32Uint,
        => binding.WGPUTextureSampleType_Uint,
        texture.WGPUTextureFormat_R8Sint,
        texture.WGPUTextureFormat_R16Sint,
        texture.WGPUTextureFormat_RG8Sint,
        texture.WGPUTextureFormat_R32Sint,
        texture.WGPUTextureFormat_RG16Sint,
        texture.WGPUTextureFormat_RGBA8Sint,
        texture.WGPUTextureFormat_RG32Sint,
        texture.WGPUTextureFormat_RGBA16Sint,
        texture.WGPUTextureFormat_RGBA32Sint,
        => binding.WGPUTextureSampleType_Sint,
        else => binding.WGPUTextureSampleType_Float,
    };
}

pub fn defaultViewDimension(dimension: u32) u32 {
    return switch (dimension) {
        texture.WGPUTextureDimension_1D => texture.WGPUTextureViewDimension_1D,
        texture.WGPUTextureDimension_3D => texture.WGPUTextureViewDimension_3D,
        else => texture.WGPUTextureViewDimension_2D,
    };
}

pub fn aspectMatches(format: texture.WGPUTextureFormat, aspect: u32) bool {
    return switch (aspect) {
        texture.WGPUTextureAspect_All => true,
        texture.WGPUTextureAspect_DepthOnly => switch (format) {
            texture.WGPUTextureFormat_Depth16Unorm,
            texture.WGPUTextureFormat_Depth24Plus,
            texture.WGPUTextureFormat_Depth24PlusStencil8,
            texture.WGPUTextureFormat_Depth32Float,
            texture.WGPUTextureFormat_Depth32FloatStencil8,
            => true,
            else => false,
        },
        texture.WGPUTextureAspect_StencilOnly => switch (format) {
            texture.WGPUTextureFormat_Stencil8,
            texture.WGPUTextureFormat_Depth24PlusStencil8,
            texture.WGPUTextureFormat_Depth32FloatStencil8,
            => true,
            else => false,
        },
        else => false,
    };
}

/// https://gpuweb.github.io/gpuweb/#abstract-opdef-resolving-gputextureaspect:
/// the format of a single-aspect view, or null when the format lacks the aspect.
pub fn aspectFormat(format: texture.WGPUTextureFormat, aspect: u32) ?texture.WGPUTextureFormat {
    if (!aspectMatches(format, aspect)) return null;
    return switch (aspect) {
        texture.WGPUTextureAspect_DepthOnly => switch (format) {
            texture.WGPUTextureFormat_Depth24PlusStencil8 => texture.WGPUTextureFormat_Depth24Plus,
            texture.WGPUTextureFormat_Depth32FloatStencil8 => texture.WGPUTextureFormat_Depth32Float,
            else => format,
        },
        texture.WGPUTextureAspect_StencilOnly => texture.WGPUTextureFormat_Stencil8,
        else => format,
    };
}

test "texture format classifications are stable" {
    const std = @import("std");
    try std.testing.expectEqual(binding.WGPUTextureSampleType_Uint, inferSampleType(texture.WGPUTextureFormat_RGBA8Uint));
    try std.testing.expectEqual(binding.WGPUTextureSampleType_Depth, inferSampleType(texture.WGPUTextureFormat_Depth32Float));
    try std.testing.expect(aspectMatches(texture.WGPUTextureFormat_Depth24PlusStencil8, texture.WGPUTextureAspect_StencilOnly));
    try std.testing.expectEqual(@as(?texture.WGPUTextureFormat, texture.WGPUTextureFormat_Depth24Plus), aspectFormat(texture.WGPUTextureFormat_Depth24PlusStencil8, texture.WGPUTextureAspect_DepthOnly));
    try std.testing.expectEqual(@as(?texture.WGPUTextureFormat, texture.WGPUTextureFormat_Stencil8), aspectFormat(texture.WGPUTextureFormat_Depth32FloatStencil8, texture.WGPUTextureAspect_StencilOnly));
    try std.testing.expectEqual(@as(?texture.WGPUTextureFormat, null), aspectFormat(texture.WGPUTextureFormat_RGBA8Unorm, texture.WGPUTextureAspect_DepthOnly));
}
