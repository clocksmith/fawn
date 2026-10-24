//! Backend-neutral WebGPU texture format and view classifications.

const binding = @import("model/model_binding_value_types.zig");
const texture = @import("model/model_texture_value_types.zig");
const texture_format = @import("texture_format.zig");

/// A color target format's render target pixel byte cost and component
/// alignment (https://gpuweb.github.io/gpuweb/#plain-color-formats).
pub const RenderTargetCost = struct { cost: u32, alignment: u32 };

/// Null for a format that is not a color render target. The snorm and 16-bit
/// unorm formats are color targets only with texture-formats-tier1
/// (renderTargetNeedsTier1).
pub fn renderTargetCost(format: texture.WGPUTextureFormat) ?RenderTargetCost {
    return switch (format) {
        texture.WGPUTextureFormat_R8Snorm => .{ .cost = 1, .alignment = 1 },
        texture.WGPUTextureFormat_RG8Snorm => .{ .cost = 2, .alignment = 1 },
        texture.WGPUTextureFormat_RGBA8Snorm => .{ .cost = 4, .alignment = 1 },
        texture.WGPUTextureFormat_R16Unorm, texture.WGPUTextureFormat_R16Snorm => .{ .cost = 2, .alignment = 2 },
        texture.WGPUTextureFormat_RG16Unorm, texture.WGPUTextureFormat_RG16Snorm => .{ .cost = 4, .alignment = 2 },
        texture.WGPUTextureFormat_RGBA16Unorm, texture.WGPUTextureFormat_RGBA16Snorm => .{ .cost = 8, .alignment = 2 },
        texture.WGPUTextureFormat_R8Unorm, texture.WGPUTextureFormat_R8Uint, texture.WGPUTextureFormat_R8Sint => .{ .cost = 1, .alignment = 1 },
        texture.WGPUTextureFormat_RG8Unorm, texture.WGPUTextureFormat_RG8Uint, texture.WGPUTextureFormat_RG8Sint => .{ .cost = 2, .alignment = 1 },
        texture.WGPUTextureFormat_RGBA8Uint, texture.WGPUTextureFormat_RGBA8Sint => .{ .cost = 4, .alignment = 1 },
        texture.WGPUTextureFormat_RGBA8Unorm,
        texture.WGPUTextureFormat_RGBA8UnormSrgb,
        texture.WGPUTextureFormat_BGRA8Unorm,
        texture.WGPUTextureFormat_BGRA8UnormSrgb,
        => .{ .cost = 8, .alignment = 1 },
        texture.WGPUTextureFormat_R16Uint, texture.WGPUTextureFormat_R16Sint, texture.WGPUTextureFormat_R16Float => .{ .cost = 2, .alignment = 2 },
        texture.WGPUTextureFormat_RG16Uint, texture.WGPUTextureFormat_RG16Sint, texture.WGPUTextureFormat_RG16Float => .{ .cost = 4, .alignment = 2 },
        texture.WGPUTextureFormat_RGBA16Uint, texture.WGPUTextureFormat_RGBA16Sint, texture.WGPUTextureFormat_RGBA16Float => .{ .cost = 8, .alignment = 2 },
        texture.WGPUTextureFormat_R32Uint, texture.WGPUTextureFormat_R32Sint, texture.WGPUTextureFormat_R32Float => .{ .cost = 4, .alignment = 4 },
        texture.WGPUTextureFormat_RG32Uint,
        texture.WGPUTextureFormat_RG32Sint,
        texture.WGPUTextureFormat_RG32Float,
        texture.WGPUTextureFormat_RGB10A2Uint,
        texture.WGPUTextureFormat_RGB10A2Unorm,
        texture.WGPUTextureFormat_RG11B10Ufloat,
        => .{ .cost = 8, .alignment = 4 },
        texture.WGPUTextureFormat_RGBA32Uint, texture.WGPUTextureFormat_RGBA32Sint, texture.WGPUTextureFormat_RGBA32Float => .{ .cost = 16, .alignment = 4 },
        else => null,
    };
}

/// Channels a color target format stores
/// (https://gpuweb.github.io/gpuweb/#plain-color-formats), or null for a format
/// that is not a color target.
pub fn colorComponentCount(format: texture.WGPUTextureFormat) ?u32 {
    if (renderTargetCost(format) == null) return null;
    return switch (format) {
        texture.WGPUTextureFormat_R8Unorm,
        texture.WGPUTextureFormat_R8Snorm,
        texture.WGPUTextureFormat_R8Uint,
        texture.WGPUTextureFormat_R8Sint,
        texture.WGPUTextureFormat_R16Unorm,
        texture.WGPUTextureFormat_R16Snorm,
        texture.WGPUTextureFormat_R16Uint,
        texture.WGPUTextureFormat_R16Sint,
        texture.WGPUTextureFormat_R16Float,
        texture.WGPUTextureFormat_R32Uint,
        texture.WGPUTextureFormat_R32Sint,
        texture.WGPUTextureFormat_R32Float,
        => 1,
        texture.WGPUTextureFormat_RG8Unorm,
        texture.WGPUTextureFormat_RG8Snorm,
        texture.WGPUTextureFormat_RG8Uint,
        texture.WGPUTextureFormat_RG8Sint,
        texture.WGPUTextureFormat_RG16Unorm,
        texture.WGPUTextureFormat_RG16Snorm,
        texture.WGPUTextureFormat_RG16Uint,
        texture.WGPUTextureFormat_RG16Sint,
        texture.WGPUTextureFormat_RG16Float,
        texture.WGPUTextureFormat_RG32Uint,
        texture.WGPUTextureFormat_RG32Sint,
        texture.WGPUTextureFormat_RG32Float,
        => 2,
        texture.WGPUTextureFormat_RG11B10Ufloat => 3,
        else => 4,
    };
}

/// Color target formats that can blend: renderable float formats, the 32-bit
/// float ones only with float32-blendable
/// (https://gpuweb.github.io/gpuweb/#plain-color-formats).
pub fn blendable(format: texture.WGPUTextureFormat, float32_blendable: bool) bool {
    if (renderTargetCost(format) == null or inferSampleType(format) != binding.WGPUTextureSampleType_Float) return false;
    return switch (format) {
        texture.WGPUTextureFormat_R32Float, texture.WGPUTextureFormat_RG32Float, texture.WGPUTextureFormat_RGBA32Float => float32_blendable,
        else => true,
    };
}

/// Formats that can be multisampled: every depth-stencil format, and the
/// color formats with the multisampling capability in the WebGPU format
/// tables (https://gpuweb.github.io/gpuweb/#plain-color-formats); 32-bit
/// integer and two- and four-component 32-bit formats cannot. The
/// texture-formats-tier1 color targets multisample once they are renderable.
pub fn multisampleCapable(format: texture.WGPUTextureFormat) bool {
    return switch (format) {
        texture.WGPUTextureFormat_Stencil8,
        texture.WGPUTextureFormat_Depth16Unorm,
        texture.WGPUTextureFormat_Depth24Plus,
        texture.WGPUTextureFormat_Depth24PlusStencil8,
        texture.WGPUTextureFormat_Depth32Float,
        texture.WGPUTextureFormat_Depth32FloatStencil8,
        => true,
        texture.WGPUTextureFormat_R32Uint,
        texture.WGPUTextureFormat_R32Sint,
        texture.WGPUTextureFormat_RG32Uint,
        texture.WGPUTextureFormat_RG32Sint,
        texture.WGPUTextureFormat_RG32Float,
        texture.WGPUTextureFormat_RGBA32Uint,
        texture.WGPUTextureFormat_RGBA32Sint,
        texture.WGPUTextureFormat_RGBA32Float,
        => false,
        else => renderTargetCost(format) != null,
    };
}

/// Formats a texture may have with STORAGE_BINDING usage: the base storage
/// formats, bgra8unorm with bgra8unorm-storage, and the formats
/// texture-formats-tier1 adds
/// (https://gpuweb.github.io/gpuweb/#texture-formats-tier1).
pub fn storageCapable(format: texture.WGPUTextureFormat, tier1: bool, bgra8unorm_storage: bool) bool {
    if (texture_format.isStorageTextureFormat(format, bgra8unorm_storage)) return true;
    return tier1 and switch (format) {
        texture.WGPUTextureFormat_R8Unorm,
        texture.WGPUTextureFormat_R8Snorm,
        texture.WGPUTextureFormat_R8Uint,
        texture.WGPUTextureFormat_R8Sint,
        texture.WGPUTextureFormat_RG8Unorm,
        texture.WGPUTextureFormat_RG8Snorm,
        texture.WGPUTextureFormat_RG8Uint,
        texture.WGPUTextureFormat_RG8Sint,
        texture.WGPUTextureFormat_R16Unorm,
        texture.WGPUTextureFormat_R16Snorm,
        texture.WGPUTextureFormat_R16Uint,
        texture.WGPUTextureFormat_R16Sint,
        texture.WGPUTextureFormat_R16Float,
        texture.WGPUTextureFormat_RG16Unorm,
        texture.WGPUTextureFormat_RG16Snorm,
        texture.WGPUTextureFormat_RG16Uint,
        texture.WGPUTextureFormat_RG16Sint,
        texture.WGPUTextureFormat_RG16Float,
        texture.WGPUTextureFormat_RGBA16Unorm,
        texture.WGPUTextureFormat_RGBA16Snorm,
        texture.WGPUTextureFormat_RGB10A2Uint,
        texture.WGPUTextureFormat_RGB10A2Unorm,
        texture.WGPUTextureFormat_RG11B10Ufloat,
        => true,
        else => false,
    };
}

/// Color targets that texture-formats-tier1 adds
/// (https://gpuweb.github.io/gpuweb/#texture-formats-tier1).
pub fn renderTargetNeedsTier1(format: texture.WGPUTextureFormat) bool {
    return switch (format) {
        texture.WGPUTextureFormat_R8Snorm,
        texture.WGPUTextureFormat_RG8Snorm,
        texture.WGPUTextureFormat_RGBA8Snorm,
        texture.WGPUTextureFormat_R16Unorm,
        texture.WGPUTextureFormat_R16Snorm,
        texture.WGPUTextureFormat_RG16Unorm,
        texture.WGPUTextureFormat_RG16Snorm,
        texture.WGPUTextureFormat_RGBA16Unorm,
        texture.WGPUTextureFormat_RGBA16Snorm,
        => true,
        else => false,
    };
}

/// Color attachment bytes per sample of a list of color formats, skipping
/// undefined slots: each target is aligned to its component alignment, then
/// adds its byte cost
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-calculating-color-attachment-bytes-per-sample).
/// Null when a format is not a color render target.
pub fn colorAttachmentBytesPerSample(formats: []const texture.WGPUTextureFormat) ?u32 {
    var total: u32 = 0;
    for (formats) |format| {
        if (format == texture.WGPUTextureFormat_Undefined) continue;
        const target = renderTargetCost(format) orelse return null;
        total = (total + target.alignment - 1) / target.alignment * target.alignment + target.cost;
    }
    return total;
}

test "colorAttachmentBytesPerSample aligns each target before adding its cost" {
    const formats = [_]texture.WGPUTextureFormat{ texture.WGPUTextureFormat_R8Unorm, texture.WGPUTextureFormat_RGBA32Uint, texture.WGPUTextureFormat_Undefined, texture.WGPUTextureFormat_RG16Uint };
    // r8unorm at 0 (1 byte), rgba32uint aligned to 4 (4..20), rg16uint at 20 (20..24).
    try @import("std").testing.expectEqual(@as(?u32, 24), colorAttachmentBytesPerSample(&formats));
    try @import("std").testing.expectEqual(@as(?u32, null), colorAttachmentBytesPerSample(&.{texture.WGPUTextureFormat_Depth32Float}));
}

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
