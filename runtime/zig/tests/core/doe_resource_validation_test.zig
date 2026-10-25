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
    texture.srgb_view_format = types.WGPUTextureFormat_RGBA8UnormSrgb;
    try std.testing.expect(textures.viewFormatAllowed(&texture, types.WGPUTextureFormat_RGBA8UnormSrgb));
    try std.testing.expect(!textures.viewFormatAllowed(&texture, types.WGPUTextureFormat_BGRA8UnormSrgb));
    const view_formats = [_]u32{types.WGPUTextureFormat_RGBA8UnormSrgb};
    var texture_desc = std.mem.zeroes(types.WGPUTextureDescriptor);
    texture_desc.format = types.WGPUTextureFormat_RGBA8Unorm;
    texture_desc.viewFormatCount = view_formats.len;
    texture_desc.viewFormats = &view_formats;
    try std.testing.expectEqual(@as(u32, types.WGPUTextureFormat_RGBA8UnormSrgb), texture_validation.srgbPairViewFormat(&texture_desc));
    texture_desc.format = types.WGPUTextureFormat_R8Unorm;
    try std.testing.expectEqual(@as(u32, 0), texture_validation.srgbPairViewFormat(&texture_desc));
    try std.testing.expectEqual(@as(u32, types.WGPUTextureFormat_BC1RGBAUnormSrgb), texture_validation.srgbPair(types.WGPUTextureFormat_BC1RGBAUnorm));
}

test "automatic compute layouts bind ONNX Runtime storage buffers" {
    // ORT takes getBindGroupLayout(0) from an automatic compute pipeline and
    // binds Storage|CopySrc|CopyDst|Indirect buffers without Uniform usage.
    const compute = @import("../../src/native/compute/doe_compute_ext_native.zig");
    const bind_groups = @import("../../src/native/resource/doe_bind_group_native.zig");
    const ir = @import("../../src/compiler/wgsl/ir/ir.zig");
    const binding = @import("../../src/contracts/binding.zig");
    const abi_binding = @import("../../src/contracts/model/model_binding_value_types.zig");
    var shader = native.DoeShaderModule{ .binding_count = 3, .bindings_ready = true };
    shader.bindings[0] = .{ .group = 0, .binding = 0, .kind = @intFromEnum(binding.ShaderKind.buffer), .addr_space = @intFromEnum(ir.AddressSpace.storage), .access = @intFromEnum(ir.AccessMode.read) };
    shader.bindings[1] = .{ .group = 0, .binding = 1, .kind = @intFromEnum(binding.ShaderKind.buffer), .addr_space = @intFromEnum(ir.AddressSpace.storage), .access = @intFromEnum(ir.AccessMode.read_write) };
    shader.bindings[2] = .{ .group = 0, .binding = 2, .kind = @intFromEnum(binding.ShaderKind.buffer), .addr_space = @intFromEnum(ir.AddressSpace.uniform) };
    var pipeline = native.DoeComputePipeline{ .shader_module = &shader };
    const layout_raw = compute.doeNativeComputePipelineGetBindGroupLayout(@ptrCast(&pipeline), 0);
    try std.testing.expect(layout_raw != null);
    defer bind_groups.doeNativeBindGroupLayoutRelease(layout_raw);
    const layout = native.cast(native.DoeBindGroupLayout, layout_raw).?;
    const layout_entries = layout.entries.?;
    try std.testing.expectEqual(abi_binding.WGPUBufferBindingType_ReadOnlyStorage, layout_entries[0].buffer_binding_type);
    try std.testing.expectEqual(abi_binding.WGPUBufferBindingType_Storage, layout_entries[1].buffer_binding_type);
    try std.testing.expectEqual(abi_binding.WGPUBufferBindingType_Uniform, layout_entries[2].buffer_binding_type);
    for (layout_entries) |entry| try std.testing.expectEqual(abi_binding.WGPUShaderStage_Compute, entry.visibility);

    var dev = native.DoeDevice{ .backend = .vulkan };
    const indirect_usage: u64 = 0x100; // webgpu.h WGPUBufferUsage_Indirect
    const storage_usage = STORAGE | COPY_SRC | COPY_DST | indirect_usage;
    var input = native.DoeBuffer{ .size = 6912, .usage = storage_usage, .device_ref = &dev };
    var output = native.DoeBuffer{ .size = 6912, .usage = storage_usage, .device_ref = &dev };
    var uniforms = native.DoeBuffer{ .size = 16, .usage = types.WGPUBufferUsage_Uniform | COPY_DST, .device_ref = &dev };
    var entries = [_]types.WGPUBindGroupEntry{std.mem.zeroes(types.WGPUBindGroupEntry)} ** 3;
    for (&entries, 0..) |*entry, i| {
        entry.binding = @intCast(i);
        entry.size = types.WGPU_WHOLE_SIZE;
    }
    entries[0].buffer = @ptrCast(&input);
    entries[1].buffer = @ptrCast(&output);
    entries[2].buffer = @ptrCast(&uniforms);
    var desc = std.mem.zeroes(types.WGPUBindGroupDescriptor);
    desc.layout = @ptrCast(layout_raw);
    desc.entryCount = entries.len;
    desc.entries = &entries;
    dev.error_scopes.push(error_scope.FILTER_VALIDATION);
    const group = bind_groups.doeNativeDeviceCreateBindGroup(@ptrCast(&dev), &desc);
    try std.testing.expect(group != null);
    bind_groups.doeNativeBindGroupRelease(group);
    try std.testing.expectEqual(error_scope.ERROR_TYPE_NO_ERROR, try popType(&dev));
    // Both usage checks stay enforced after correcting the automatic layout.
    entries[2].buffer = @ptrCast(&input);
    dev.error_scopes.push(error_scope.FILTER_VALIDATION);
    try std.testing.expect(bind_groups.doeNativeDeviceCreateBindGroup(@ptrCast(&dev), &desc) == null);
    try std.testing.expectEqual(error_scope.ERROR_TYPE_VALIDATION, try popType(&dev));
    entries[2].buffer = @ptrCast(&uniforms);
    entries[0].buffer = @ptrCast(&uniforms);
    dev.error_scopes.push(error_scope.FILTER_VALIDATION);
    try std.testing.expect(bind_groups.doeNativeDeviceCreateBindGroup(@ptrCast(&dev), &desc) == null);
    try std.testing.expectEqual(error_scope.ERROR_TYPE_VALIDATION, try popType(&dev));
}
