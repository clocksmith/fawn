const std = @import("std");
const abi_callback = @import("../../core/abi/wgpu_callback_descriptor_types.zig");
const abi_core = @import("../../core/abi/wgpu_core_base_types.zig");
const abi_pipeline = @import("../../core/abi/wgpu_pipeline_descriptor_types.zig");
const abi_texture = @import("../../core/abi/wgpu_texture_base_types.zig");
const device_caps = @import("../support/doe_device_caps.zig");
const native_types = @import("../support/doe_native_object_types.zig");
const texture_contract = @import("../../contracts/texture.zig");
const texture_layout = @import("../../contracts/texture_format_layout.zig");
const abi_feature = @import("../../core/abi/wgpu_feature_base_types.zig");

const DoeDevice = native_types.DoeDevice;
const MAX_TEXTURE_DESCRIPTOR_CHAIN_NODES: u32 = 16;
const VALID_TEXTURE_USAGE = abi_texture.WGPUTextureUsage_CopySrc |
    abi_texture.WGPUTextureUsage_CopyDst |
    abi_texture.WGPUTextureUsage_TextureBinding |
    abi_texture.WGPUTextureUsage_StorageBinding |
    abi_texture.WGPUTextureUsage_RenderAttachment |
    abi_texture.WGPUTextureUsage_TransientAttachment |
    abi_texture.WGPUTextureUsage_StorageAttachment;

pub fn effectiveTextureUsage(desc: *const abi_pipeline.WGPUTextureDescriptor) u64 {
    var usage = desc.usage;
    var visited: u32 = 0;
    var chain = if (desc.nextInChain) |raw|
        @as(*const abi_pipeline.WGPUChainedStruct, @ptrCast(@alignCast(raw)))
    else
        null;

    while (chain) |node| : (visited += 1) {
        if (visited >= MAX_TEXTURE_DESCRIPTOR_CHAIN_NODES) return usage;
        if (node.sType == abi_pipeline.WGPUSType_DawnTextureInternalUsageDescriptor) {
            const internal: *const abi_pipeline.WGPUDawnTextureInternalUsageDescriptor =
                @ptrCast(@alignCast(node));
            usage |= internal.internalUsage;
        }
        chain = node.next;
    }
    return usage;
}

fn chainIsValid(desc: *const abi_pipeline.WGPUTextureDescriptor) bool {
    var visited: u32 = 0;
    var chain = if (desc.nextInChain) |raw|
        @as(*const abi_pipeline.WGPUChainedStruct, @ptrCast(@alignCast(raw)))
    else
        null;
    while (chain) |node| : (visited += 1) {
        if (visited >= MAX_TEXTURE_DESCRIPTOR_CHAIN_NODES) return false;
        if (node.sType != abi_pipeline.WGPUSType_DawnTextureInternalUsageDescriptor) return false;
        chain = node.next;
    }
    return true;
}

fn isKnownFormat(format: u32) bool {
    return format >= abi_texture.WGPUTextureFormat_R8Unorm and
        format <= abi_texture.WGPUTextureFormat_ASTC12x12UnormSrgb;
}

/// The format differing only in srgb-ness, or 0. Texture-view format
/// compatibility (https://gpuweb.github.io/gpuweb/#texture-view-format-compatible)
/// is equality or this pair; the set matches the C provider (rgba8, bgra8 and
/// BC1/2/3/7, each pair sharing one Vulkan compatibility class).
pub fn srgbPair(format: u32) u32 {
    return switch (format) {
        abi_texture.WGPUTextureFormat_RGBA8Unorm => abi_texture.WGPUTextureFormat_RGBA8UnormSrgb,
        abi_texture.WGPUTextureFormat_RGBA8UnormSrgb => abi_texture.WGPUTextureFormat_RGBA8Unorm,
        abi_texture.WGPUTextureFormat_BGRA8Unorm => abi_texture.WGPUTextureFormat_BGRA8UnormSrgb,
        abi_texture.WGPUTextureFormat_BGRA8UnormSrgb => abi_texture.WGPUTextureFormat_BGRA8Unorm,
        abi_texture.WGPUTextureFormat_BC1RGBAUnorm => abi_texture.WGPUTextureFormat_BC1RGBAUnormSrgb,
        abi_texture.WGPUTextureFormat_BC1RGBAUnormSrgb => abi_texture.WGPUTextureFormat_BC1RGBAUnorm,
        abi_texture.WGPUTextureFormat_BC2RGBAUnorm => abi_texture.WGPUTextureFormat_BC2RGBAUnormSrgb,
        abi_texture.WGPUTextureFormat_BC2RGBAUnormSrgb => abi_texture.WGPUTextureFormat_BC2RGBAUnorm,
        abi_texture.WGPUTextureFormat_BC3RGBAUnorm => abi_texture.WGPUTextureFormat_BC3RGBAUnormSrgb,
        abi_texture.WGPUTextureFormat_BC3RGBAUnormSrgb => abi_texture.WGPUTextureFormat_BC3RGBAUnorm,
        abi_texture.WGPUTextureFormat_BC7RGBAUnorm => abi_texture.WGPUTextureFormat_BC7RGBAUnormSrgb,
        abi_texture.WGPUTextureFormat_BC7RGBAUnormSrgb => abi_texture.WGPUTextureFormat_BC7RGBAUnorm,
        else => 0,
    };
}

fn isViewFormatCompatible(base: u32, view: u32) bool {
    return base == view or (view != 0 and srgbPair(base) == view);
}

/// The srgb pair a texture created from `desc` may be viewed as, else 0.
pub fn srgbPairViewFormat(desc: *const abi_pipeline.WGPUTextureDescriptor) u32 {
    const pair = srgbPair(desc.format);
    if (pair == 0 or desc.viewFormatCount == 0) return 0;
    const view_formats = desc.viewFormats orelse return 0;
    for (view_formats[0..desc.viewFormatCount]) |view_format| if (view_format == pair) return pair;
    return 0;
}

fn featureEnabled(device: *const DoeDevice, feature: abi_feature.WGPUFeatureName) bool {
    for (device.enabled_features) |enabled| if (enabled == feature) return true;
    return false;
}

/// https://gpuweb.github.io/gpuweb/#abstract-opdef-maximum-miplevel-count:
/// a 1d texture has one mip level.
fn maxMipLevelCount(desc: *const abi_pipeline.WGPUTextureDescriptor) u32 {
    const max_extent = switch (desc.dimension) {
        abi_texture.WGPUTextureDimension_1D => 1,
        abi_texture.WGPUTextureDimension_3D => @max(desc.size.width, @max(desc.size.height, desc.size.depthOrArrayLayers)),
        else => @max(desc.size.width, desc.size.height),
    };
    return @as(u32, @intCast(@bitSizeOf(u32) - @clz(max_extent)));
}

pub fn validateTextureDescriptor(
    device: *DoeDevice,
    desc: *const abi_pipeline.WGPUTextureDescriptor,
) ?[]const u8 {
    if (!chainIsValid(desc)) return "texture descriptor contains an unsupported or cyclic chain";
    const usage = effectiveTextureUsage(desc);
    if (desc.usage == abi_texture.WGPUTextureUsage_None) return "texture usage must not be zero";
    if ((usage & ~VALID_TEXTURE_USAGE) != 0) return "texture usage contains unsupported flags";
    if (!isKnownFormat(desc.format)) return "texture format is undefined or unsupported";
    if (desc.size.width == 0 or desc.size.height == 0 or desc.size.depthOrArrayLayers == 0)
        return "texture extent must be non-zero";
    if (desc.mipLevelCount == 0) return "texture mip level count must be non-zero";
    if (desc.sampleCount != 1 and desc.sampleCount != 4) return "texture sample count must be 1 or 4";

    var limits: abi_callback.WGPULimits = undefined;
    _ = device_caps.doeNativeDeviceGetLimits(@ptrCast(device), &limits);
    switch (desc.dimension) {
        abi_texture.WGPUTextureDimension_1D => {
            if (desc.size.width > limits.maxTextureDimension1D or
                desc.size.height != 1 or desc.size.depthOrArrayLayers != 1)
                return "1D texture extent exceeds device limits or has non-unit height/layers";
        },
        abi_texture.WGPUTextureDimension_2D => {
            if (desc.size.width > limits.maxTextureDimension2D or
                desc.size.height > limits.maxTextureDimension2D or
                desc.size.depthOrArrayLayers > limits.maxTextureArrayLayers)
                return "2D texture extent exceeds device limits";
        },
        abi_texture.WGPUTextureDimension_3D => {
            if (desc.size.width > limits.maxTextureDimension3D or
                desc.size.height > limits.maxTextureDimension3D or
                desc.size.depthOrArrayLayers > limits.maxTextureDimension3D)
                return "3D texture extent exceeds device limits";
        },
        else => return "texture dimension is undefined or unsupported",
    }

    if (desc.mipLevelCount > maxMipLevelCount(desc)) return "texture mip level count exceeds its extent";
    // https://gpuweb.github.io/gpuweb/#dom-gpudevice-createtexture: a
    // multisampled texture is 2D with one layer and mip, has
    // RENDER_ATTACHMENT and not STORAGE_BINDING usage (copy usages are
    // allowed), and has a format that supports multisampling.
    if (desc.sampleCount > 1) {
        if (desc.dimension != abi_texture.WGPUTextureDimension_2D or desc.size.depthOrArrayLayers != 1 or desc.mipLevelCount != 1)
            return "a multisampled texture must be 2D with one array layer and one mip level";
        if ((usage & abi_texture.WGPUTextureUsage_StorageBinding) != 0) return "a multisampled texture cannot have STORAGE_BINDING usage";
        if ((usage & abi_texture.WGPUTextureUsage_RenderAttachment) == 0) return "a multisampled texture needs RENDER_ATTACHMENT usage";
        if (!texture_contract.multisampleCapable(desc.format)) return "texture format does not support multisampling";
    }
    // TRANSIENT_ATTACHMENT: exactly RENDER_ATTACHMENT | TRANSIENT_ATTACHMENT,
    // one array layer and mip level, and no viewFormats.
    if ((usage & abi_texture.WGPUTextureUsage_TransientAttachment) != 0) {
        if (desc.usage != (abi_texture.WGPUTextureUsage_RenderAttachment | abi_texture.WGPUTextureUsage_TransientAttachment))
            return "a transient attachment's usage must be exactly RENDER_ATTACHMENT | TRANSIENT_ATTACHMENT";
        if (desc.size.depthOrArrayLayers != 1 or desc.mipLevelCount != 1)
            return "a transient attachment must have one array layer and one mip level";
        if (desc.viewFormatCount != 0) return "a transient attachment cannot list viewFormats";
    }
    if (abi_texture.isDepthStencilFormat(desc.format) and desc.dimension != abi_texture.WGPUTextureDimension_2D)
        return "depth-stencil textures must be 2D";
    if (usageFormatMessage(device, desc)) |message| return message;
    // Mip level 0 of a block-compressed texture covers whole texel blocks
    // (no texture-compression-unaligned in webgpu.h).
    const block = texture_layout.copy_block_extent(desc.format);
    if (desc.size.width % block[0] != 0 or desc.size.height % block[1] != 0)
        return "a compressed texture's width and height must be multiples of its block size";
    if ((abi_texture.isBCFormat(desc.format) or abi_texture.isETC2Format(desc.format) or abi_texture.isASTCFormat(desc.format)) and
        desc.dimension == abi_texture.WGPUTextureDimension_1D)
        return "compressed textures cannot be 1D";

    if (desc.viewFormatCount > 0) {
        const view_formats = desc.viewFormats orelse return "texture view formats pointer is null";
        for (view_formats[0..desc.viewFormatCount]) |view_format| {
            if (!isKnownFormat(view_format) or !isViewFormatCompatible(desc.format, view_format))
                return "texture view format is incompatible with the base format";
        }
        // As in the C backend: a storage image of a MUTABLE_FORMAT image would
        // need every listed view format to support storage (srgb formats do not).
        if (srgbPairViewFormat(desc) != 0 and (usage & abi_texture.WGPUTextureUsage_StorageBinding) != 0)
            return "storage usage with an srgb view format is unsupported";
    }
    return null;
}

/// https://gpuweb.github.io/gpuweb/#dom-gpudevice-createtexture: STORAGE_BINDING
/// needs a storage format, RENDER_ATTACHMENT a renderable format and a 2d or
/// 3d texture, and TRANSIENT_ATTACHMENT a 2d texture.
fn usageFormatMessage(device: *const DoeDevice, desc: *const abi_pipeline.WGPUTextureDescriptor) ?[]const u8 {
    const tier1 = featureEnabled(device, abi_feature.WGPUFeatureName_TextureFormatsTier1);
    if ((desc.usage & abi_texture.WGPUTextureUsage_StorageBinding) != 0 and
        !texture_contract.storageCapable(desc.format, tier1, featureEnabled(device, abi_feature.WGPUFeatureName_BGRA8UnormStorage)))
        return "texture format does not support STORAGE_BINDING usage";
    if ((desc.usage & abi_texture.WGPUTextureUsage_RenderAttachment) != 0) {
        if (desc.dimension == abi_texture.WGPUTextureDimension_1D) return "a 1D texture cannot have RENDER_ATTACHMENT usage";
        if (!abi_texture.isDepthStencilFormat(desc.format) and (texture_contract.renderTargetCost(desc.format) == null or
            (texture_contract.renderTargetNeedsTier1(desc.format) and !tier1)))
            return "texture format is not renderable";
    }
    if ((desc.usage & abi_texture.WGPUTextureUsage_TransientAttachment) != 0 and desc.dimension != abi_texture.WGPUTextureDimension_2D)
        return "a transient attachment must be 2D";
    return null;
}

/// https://gpuweb.github.io/gpuweb/#abstract-opdef-gputextureviewdescriptor-valid-usage
/// for a view of `tex` with resolved `dimension` and `layer_count` and the
/// descriptor's `usage` (0 inherits the texture's): the usage is a subset of
/// the texture's (all of it for a transient attachment), the dimension fits
/// the texture's, 1d/2d/3d views have one layer, cube views six and
/// cube-array views a multiple of six, and cube faces are square.
pub fn viewDescriptorMessage(tex: *const native_types.DoeTexture, dimension: u32, usage: u64, layer_count: u32) ?[]const u8 {
    if ((usage & ~tex.usage) != 0) return "texture view usage must be a subset of the texture's usage";
    if ((tex.usage & abi_texture.WGPUTextureUsage_TransientAttachment) != 0 and usage != 0 and usage != tex.usage)
        return "a view of a transient attachment must have all of its usages";
    const T = abi_texture;
    const fits = switch (tex.dimension) {
        T.WGPUTextureDimension_1D => dimension == T.WGPUTextureViewDimension_1D,
        T.WGPUTextureDimension_3D => dimension == T.WGPUTextureViewDimension_3D,
        else => switch (dimension) {
            T.WGPUTextureViewDimension_2D, T.WGPUTextureViewDimension_2DArray, T.WGPUTextureViewDimension_Cube, T.WGPUTextureViewDimension_CubeArray => true,
            else => false,
        },
    };
    if (!fits) return "texture view dimension is incompatible with the texture dimension";
    const layers_ok = switch (dimension) {
        T.WGPUTextureViewDimension_1D, T.WGPUTextureViewDimension_2D, T.WGPUTextureViewDimension_3D => layer_count == 1,
        T.WGPUTextureViewDimension_Cube => layer_count == 6,
        T.WGPUTextureViewDimension_CubeArray => layer_count % 6 == 0,
        else => true,
    };
    if (!layers_ok) return "texture view array layer count does not fit its dimension";
    if ((dimension == T.WGPUTextureViewDimension_Cube or dimension == T.WGPUTextureViewDimension_CubeArray) and tex.width != tex.height)
        return "cube texture view faces must be square";
    return null;
}

test "browser canvas texture descriptor validates locally" {
    var device: DoeDevice = .{};
    var internal = abi_pipeline.WGPUDawnTextureInternalUsageDescriptor{
        .chain = .{ .next = null, .sType = abi_pipeline.WGPUSType_DawnTextureInternalUsageDescriptor },
        .internalUsage = abi_texture.WGPUTextureUsage_CopySrc | abi_texture.WGPUTextureUsage_CopyDst,
    };
    const view_formats = [_]u32{abi_texture.WGPUTextureFormat_BGRA8UnormSrgb};
    var desc = abi_pipeline.WGPUTextureDescriptor{
        .nextInChain = @ptrCast(&internal.chain),
        .label = .{ .data = null, .length = 0 },
        .usage = abi_texture.WGPUTextureUsage_RenderAttachment,
        .dimension = abi_texture.WGPUTextureDimension_2D,
        .size = .{ .width = 1280, .height = 720, .depthOrArrayLayers = 1 },
        .format = abi_texture.WGPUTextureFormat_BGRA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1,
        .viewFormatCount = view_formats.len,
        .viewFormats = &view_formats,
    };

    try std.testing.expect(validateTextureDescriptor(&device, &desc) == null);
    desc.size.width = 0;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
}

test "multisampled and transient texture descriptors follow WebGPU createTexture" {
    var device: DoeDevice = .{};
    const T = abi_texture;
    var desc = abi_pipeline.WGPUTextureDescriptor{
        .nextInChain = null,
        .label = .{ .data = null, .length = 0 },
        .usage = T.WGPUTextureUsage_RenderAttachment | T.WGPUTextureUsage_CopySrc | T.WGPUTextureUsage_CopyDst,
        .dimension = T.WGPUTextureDimension_2D,
        .size = .{ .width = 16, .height = 16, .depthOrArrayLayers = 1 },
        .format = T.WGPUTextureFormat_R32Float,
        .mipLevelCount = 1,
        .sampleCount = 4,
        .viewFormatCount = 0,
        .viewFormats = null,
    };
    // Copy usages are allowed on a multisampled texture.
    try std.testing.expect(validateTextureDescriptor(&device, &desc) == null);
    desc.usage = T.WGPUTextureUsage_RenderAttachment | T.WGPUTextureUsage_TransientAttachment;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) == null);
    desc.usage = T.WGPUTextureUsage_TextureBinding;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    desc.usage = T.WGPUTextureUsage_RenderAttachment | T.WGPUTextureUsage_StorageBinding;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    desc.usage = T.WGPUTextureUsage_RenderAttachment;
    desc.format = T.WGPUTextureFormat_RGBA32Float;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    // Transient attachments: exact usage, one layer and mip, no viewFormats.
    desc.format = T.WGPUTextureFormat_RGBA8Unorm;
    desc.sampleCount = 1;
    desc.usage = T.WGPUTextureUsage_RenderAttachment | T.WGPUTextureUsage_TransientAttachment | T.WGPUTextureUsage_CopySrc;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    desc.usage = T.WGPUTextureUsage_RenderAttachment | T.WGPUTextureUsage_TransientAttachment;
    desc.size.depthOrArrayLayers = 2;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    desc.size.depthOrArrayLayers = 1;
    const formats = [_]u32{T.WGPUTextureFormat_RGBA8Unorm};
    desc.viewFormatCount = formats.len;
    desc.viewFormats = &formats;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
}

test "texture usages need capable formats and dimensions; 1d has one mip; compressed sizes cover whole blocks" {
    var device: DoeDevice = .{};
    const T = abi_texture;
    var desc = abi_pipeline.WGPUTextureDescriptor{
        .nextInChain = null,
        .label = .{ .data = null, .length = 0 },
        .usage = T.WGPUTextureUsage_StorageBinding,
        .dimension = T.WGPUTextureDimension_2D,
        .size = .{ .width = 8, .height = 8, .depthOrArrayLayers = 1 },
        .format = T.WGPUTextureFormat_R16Float,
        .mipLevelCount = 1,
        .sampleCount = 1,
        .viewFormatCount = 0,
        .viewFormats = null,
    };
    // r16float storage needs texture-formats-tier1.
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    var tier1 = [_]abi_feature.WGPUFeatureName{abi_feature.WGPUFeatureName_TextureFormatsTier1};
    device.enabled_features = &tier1;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) == null);
    desc.format = T.WGPUTextureFormat_RGBA8UnormSrgb;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    // Render attachments are renderable, and not 1d.
    desc.usage = T.WGPUTextureUsage_RenderAttachment;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) == null);
    desc.format = T.WGPUTextureFormat_RGB9E5Ufloat;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    desc.format = T.WGPUTextureFormat_RGBA8Unorm;
    desc.dimension = T.WGPUTextureDimension_1D;
    desc.size = .{ .width = 8, .height = 1, .depthOrArrayLayers = 1 };
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    // A 1d texture has one mip level.
    desc.usage = T.WGPUTextureUsage_TextureBinding;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) == null);
    desc.mipLevelCount = 2;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    // Transient attachments are 2d.
    desc.mipLevelCount = 1;
    desc.dimension = T.WGPUTextureDimension_3D;
    desc.size = .{ .width = 8, .height = 8, .depthOrArrayLayers = 1 };
    desc.usage = T.WGPUTextureUsage_RenderAttachment | T.WGPUTextureUsage_TransientAttachment;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
    // Compressed mip level 0 covers whole 4x4 blocks.
    desc.dimension = T.WGPUTextureDimension_2D;
    desc.usage = T.WGPUTextureUsage_TextureBinding;
    desc.format = T.WGPUTextureFormat_BC1RGBAUnorm;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) == null);
    desc.size.width = 6;
    try std.testing.expect(validateTextureDescriptor(&device, &desc) != null);
}
