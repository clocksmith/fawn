// doe_resource_validation_test.zig — createBuffer, createSampler and
// createTextureView descriptor validation: each invalid descriptor generates a
// device validation error and returns an invalid object, never NULL. Rules
// match runtime/c/resources (doe_resource_buffer_validate,
// doe_resource_sampler_validate, texture-view format compatibility).

const std = @import("std");
const native = @import("../../src/native/mod.zig");
const types = @import("../../src/core/abi/wgpu_runtime_abi.zig");
const error_scope = @import("../../src/runtime/diagnostics/error_scope.zig");
const buffers = @import("../../src/native/resource/doe_buffer_ops_native.zig");
const textures = @import("../../src/native/resource/doe_texture_sampler_native.zig");
const texture_validation = @import("../../src/native/resource/doe_texture_validation.zig");

const MAP_READ: u64 = 0x1;
const MAP_WRITE: u64 = 0x2;
const COPY_SRC: u64 = 0x4;
const COPY_DST: u64 = 0x8;
const STORAGE: u64 = 0x80;
const LINEAR: u32 = 2;
const NEAREST: u32 = 1;

fn recordType(error_type: u32, message: types.WGPUStringView, userdata1: ?*anyopaque, userdata2: ?*anyopaque) callconv(.c) void {
    _ = message;
    _ = userdata2;
    const slot: *u32 = @ptrCast(@alignCast(userdata1.?));
    slot.* = error_type;
}

fn popType(dev: *native.DoeDevice) !u32 {
    var popped: u32 = 0;
    try std.testing.expect(dev.error_scopes.pop(.{ .callback = recordType, .userdata1 = &popped }));
    return popped;
}

fn bufferDescriptor(usage: u64, size: u64, mapped: bool) types.WGPUBufferDescriptor {
    return .{
        .nextInChain = null,
        .label = .{ .data = null, .length = 0 },
        .usage = usage,
        .size = size,
        .mappedAtCreation = if (mapped) 1 else 0,
    };
}

test "createBuffer: invalid usage and mapped size generate validation errors with invalid buffers" {
    var dev = native.DoeDevice{ .backend = .vulkan };
    const cases = [_]types.WGPUBufferDescriptor{
        bufferDescriptor(0, 16, false),
        bufferDescriptor(MAP_READ | STORAGE, 16, false),
        bufferDescriptor(MAP_READ | COPY_SRC, 16, false),
        bufferDescriptor(MAP_WRITE | STORAGE, 16, false),
        bufferDescriptor(MAP_WRITE | COPY_DST, 16, false),
        bufferDescriptor(0x8000, 16, false),
        bufferDescriptor(COPY_DST, 6, true),
    };
    for (cases) |desc| {
        dev.error_scopes.push(error_scope.FILTER_VALIDATION);
        const raw = buffers.doeNativeDeviceCreateBuffer(@ptrCast(&dev), &desc);
        try std.testing.expect(raw != null);
        try std.testing.expectEqual(error_scope.ERROR_TYPE_VALIDATION, try popType(&dev));
        try std.testing.expect(native.cast(native.DoeBuffer, raw).?.error_object);
        buffers.doeNativeBufferRelease(raw);
    }
    // The C provider's accepted combinations stay valid.
    for ([_]u64{ MAP_READ | COPY_DST, MAP_WRITE | COPY_SRC, MAP_READ, STORAGE | COPY_DST | COPY_SRC }) |usage| {
        try std.testing.expect(buffers.bufferDescriptorError(&bufferDescriptor(usage, 16, true)) == null);
    }
}

fn samplerDescriptor(min_clamp: f32, max_clamp: f32, anisotropy: u16, filter: u32) types.WGPUSamplerDescriptor {
    var desc = std.mem.zeroes(types.WGPUSamplerDescriptor);
    desc.lodMinClamp = min_clamp;
    desc.lodMaxClamp = max_clamp;
    desc.maxAnisotropy = anisotropy;
    desc.minFilter = filter;
    desc.magFilter = filter;
    desc.mipmapFilter = filter;
    return desc;
}

test "createSampler: clamp and anisotropy violations generate validation errors with invalid samplers" {
    var dev = native.DoeDevice{ .backend = .vulkan };
    const cases = [_]types.WGPUSamplerDescriptor{
        samplerDescriptor(5, 1, 1, NEAREST),
        samplerDescriptor(-1, 32, 1, NEAREST),
        samplerDescriptor(std.math.nan(f32), 32, 1, NEAREST),
        samplerDescriptor(0, std.math.inf(f32), 1, NEAREST),
        samplerDescriptor(0, 32, 0, LINEAR),
        samplerDescriptor(0, 32, 4, NEAREST),
    };
    for (cases) |desc| {
        dev.error_scopes.push(error_scope.FILTER_VALIDATION);
        const raw = textures.doeNativeDeviceCreateSampler(@ptrCast(&dev), &desc);
        try std.testing.expect(raw != null);
        try std.testing.expectEqual(error_scope.ERROR_TYPE_VALIDATION, try popType(&dev));
        try std.testing.expect(native.cast(native.DoeSampler, raw).?.error_object);
        textures.doeNativeSamplerRelease(raw);
    }
    try std.testing.expect(textures.samplerDescriptorError(&samplerDescriptor(0, 32, 16, LINEAR)) == null);
    try std.testing.expect(textures.samplerDescriptorError(&samplerDescriptor(1, 1, 1, NEAREST)) == null);
}

test "createTextureView: a format outside the texture format and viewFormats is an invalid view" {
    var dev = native.DoeDevice{ .backend = .vulkan };
    var texture = native.DoeTexture{
        .device_ref = &dev,
        .backend = .vulkan,
        .format = types.WGPUTextureFormat_RGBA8Unorm,
        .width = 4,
        .height = 4,
        .dimension = types.WGPUTextureDimension_2D,
        .usage = 0x4,
        .ref_count = 2,
    };
    var desc = std.mem.zeroes(types.WGPUTextureViewDescriptor);
    for ([_]u32{ types.WGPUTextureFormat_BGRA8Unorm, types.WGPUTextureFormat_RGBA8UnormSrgb }) |format| {
        desc.format = format;
        dev.error_scopes.push(error_scope.FILTER_VALIDATION);
        const raw = textures.doeNativeTextureCreateView(@ptrCast(&texture), &desc);
        try std.testing.expect(raw != null);
        try std.testing.expectEqual(error_scope.ERROR_TYPE_VALIDATION, try popType(&dev));
        const view = native.cast(native.DoeTextureView, raw).?;
        try std.testing.expect(view.tex.isUnavailable() and view.tex != &texture);
        textures.doeNativeTextureViewRelease(raw);
    }
    try std.testing.expectEqual(@as(u32, 2), texture.ref_count);
    // srgb/non-srgb: the pair is allowed only when viewFormats named it.
    try std.testing.expect(textures.viewFormatAllowed(&texture, types.WGPUTextureFormat_RGBA8Unorm));
    try std.testing.expect(!textures.viewFormatAllowed(&texture, types.WGPUTextureFormat_RGBA8UnormSrgb));
    texture.srgb_view_allowed = true;
    try std.testing.expect(textures.viewFormatAllowed(&texture, types.WGPUTextureFormat_RGBA8UnormSrgb));
    try std.testing.expect(!textures.viewFormatAllowed(&texture, types.WGPUTextureFormat_BGRA8UnormSrgb));
    const view_formats = [_]u32{types.WGPUTextureFormat_RGBA8UnormSrgb};
    var texture_desc = std.mem.zeroes(types.WGPUTextureDescriptor);
    texture_desc.format = types.WGPUTextureFormat_RGBA8Unorm;
    texture_desc.viewFormatCount = view_formats.len;
    texture_desc.viewFormats = &view_formats;
    try std.testing.expect(texture_validation.allowsSrgbPairView(&texture_desc));
    texture_desc.format = types.WGPUTextureFormat_R8Unorm;
    try std.testing.expect(!texture_validation.allowsSrgbPairView(&texture_desc));
}
