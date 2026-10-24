const recording = @import("../command/doe_command_recording.zig");
// doe_command_texture_native.zig — clearBuffer, copyTextureToTexture, writeTexture C-ABI exports.
// Sharded from doe_wgpu_native.zig to keep texture command concerns cohesive.

const std = @import("std");
const native_types = @import("../support/doe_native_object_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const references = @import("../command/doe_command_references.zig");
const resource_ops = @import("../../backend/dropin_resource_ops.zig");
const texture_values = @import("../../contracts/model/model_texture_value_types.zig");
const texture_copy = @import("../../contracts/texture_copy.zig");
const command_contract = @import("../../contracts/command_recording.zig");
const texture_abi = @import("../../core/abi/wgpu_texture_base_types.zig");
const texture_validation = @import("doe_texture_validation.zig");
const bridge = resource_ops.metal_bridge;

const alloc = native_helpers.alloc;
const cast = native_helpers.cast;
const toOpaque = native_helpers.toOpaque;

const DoeBuffer = native_types.DoeBuffer;
const DoeCommandEncoder = native_types.DoeCommandEncoder;
const DoeQueue = native_types.DoeQueue;
const DoeTexture = native_types.DoeTexture;

// ============================================================
// GPUCommandEncoder.clearBuffer(buffer, offset, size)
// Encodes a zero-fill of [offset, offset+size) in buffer.
// ============================================================

pub export fn doeNativeCommandEncoderClearBuffer(
    enc_raw: ?*anyopaque,
    buffer_raw: ?*anyopaque,
    offset: u64,
    size: u64,
) callconv(.c) void {
    const enc = cast(DoeCommandEncoder, enc_raw) orelse return;
    if (!recording.requireOpen(enc)) return;
    const buf = cast(DoeBuffer, buffer_raw) orelse return;
    if ((buf.error_object or buf.destroyed) and enc.dev.backend != .d3d12) return;
    // Resolve WGPU_WHOLE_SIZE sentinel: if size is u64 max, fill to end of buffer.
    const fill_size: u64 = if (size == std.math.maxInt(u64))
        buf.size -| offset
    else
        size;
    // D3D12 keeps zero-sized commands for queue-time range and resource validation.
    if (fill_size == 0 and enc.dev.backend != .d3d12) return;
    if (!recording.reserve(enc, 0, 1)) return;
    references.retainBufferAssumeCapacity(&enc.references, buf);
    if (!recording.append(enc, .{ .clear_buffer = .{
        .buffer = if (enc.dev.backend == .metal) buf.mtl else toOpaque(buf),
        .offset = offset,
        .size = fill_size,
    } })) return;
}

// ============================================================
// GPUCommandEncoder.copyTextureToTexture(source, destination, copySize)
// Encodes a texture-to-texture blit.
// ============================================================

fn copySide(texture: *const DoeTexture) texture_copy.Texture {
    return .{
        .width = texture.width,
        .height = texture.height,
        .layers = texture.depth_or_array_layers,
        .mip_levels = texture.mip_level_count,
        .samples = texture.sample_count,
        .dimension = texture.dimension,
        .format = texture.format,
    };
}

/// Encoder-time WebGPU rules; a failure invalidates the encoder (error at
/// finish), matching the C resource owner. Mip/x/y/z/aspect per side, then size.
fn validateTextureToTexture(device: *native_types.DoeDevice, src: *const DoeTexture, src_at: [5]u32, dst: *const DoeTexture, dst_at: [5]u32, size: [3]u32) command_contract.Failure!void {
    if (src.isUnavailable() or dst.isUnavailable()) return error.InvalidArgument;
    if (src.device_ref != device or dst.device_ref != device) return error.TextureCopyDeviceMismatch;
    if (src.usage & texture_abi.WGPUTextureUsage_CopySrc == 0 or dst.usage & texture_abi.WGPUTextureUsage_CopyDst == 0)
        return error.TextureCopyUsageMissing;
    const compatible = src.format == dst.format or
        (texture_validation.srgbPair(src.format) != 0 and texture_validation.srgbPair(src.format) == dst.format);
    const copy = struct {
        fn at(position: [5]u32, extent: [3]u32) texture_copy.Copy {
            return .{ .offset = 0, .bytes_per_row = 0, .rows_per_image = 0, .mip = position[0], .width = extent[0], .height = extent[1], .depth_or_layers = extent[2], .origin = .{ position[1], position[2], position[3] }, .aspect = position[4] };
        }
    }.at;
    try texture_copy.validateWebGPUTextureToTexture(copySide(src), copy(src_at, size), copySide(dst), copy(dst_at, size), compatible, src == dst);
}

/// Copies every aspect (`WGPUTextureAspect_All`); wgpuCommandEncoderCopyTextureToTexture
/// carries the caller's aspects.
pub export fn doeNativeCommandEncoderCopyTextureToTexture(
    enc_raw: ?*anyopaque,
    src_texture_raw: ?*anyopaque,
    src_mip: u32,
    src_slice: u32,
    src_x: u32,
    src_y: u32,
    src_z: u32,
    dst_texture_raw: ?*anyopaque,
    dst_mip: u32,
    dst_slice: u32,
    dst_x: u32,
    dst_y: u32,
    dst_z: u32,
    width: u32,
    height: u32,
    depth_or_layers: u32,
) callconv(.c) void {
    const all = texture_values.WGPUTextureAspect_All;
    copyTextureToTexture(enc_raw, src_texture_raw, src_mip, src_slice, src_x, src_y, src_z, all, dst_texture_raw, dst_mip, dst_slice, dst_x, dst_y, dst_z, all, width, height, depth_or_layers);
}

/// Records a texture-to-texture copy after WebGPU validation; the aspects
/// must cover each format (the backend copies the format's whole aspect mask).
pub fn copyTextureToTexture(
    enc_raw: ?*anyopaque,
    src_texture_raw: ?*anyopaque,
    src_mip: u32,
    src_slice: u32,
    src_x: u32,
    src_y: u32,
    src_z: u32,
    src_aspect: u32,
    dst_texture_raw: ?*anyopaque,
    dst_mip: u32,
    dst_slice: u32,
    dst_x: u32,
    dst_y: u32,
    dst_z: u32,
    dst_aspect: u32,
    width: u32,
    height: u32,
    depth_or_layers: u32,
) void {
    const enc = cast(DoeCommandEncoder, enc_raw) orelse return;
    if (!recording.requireOpen(enc)) return;
    const src = cast(DoeTexture, src_texture_raw) orelse return recording.fail(enc, error.InvalidArgument);
    const dst = cast(DoeTexture, dst_texture_raw) orelse return recording.fail(enc, error.InvalidArgument);
    validateTextureToTexture(enc.dev, src, .{ src_mip, src_x, src_y, src_z +| src_slice, src_aspect }, dst, .{ dst_mip, dst_x, dst_y, dst_z +| dst_slice, dst_aspect }, .{ width, height, depth_or_layers }) catch |err| {
        recording.fail(enc, err);
        return;
    };
    if (!recording.reserve(enc, 0, 1)) return;
    references.retainTextureAssumeCapacity(&enc.references, src);
    if (!recording.reserve(enc, 0, 1)) return;
    references.retainTextureAssumeCapacity(&enc.references, dst);
    // A valid empty copy changes no texel but still uses both textures at
    // submit; the backend region would treat a zero depth as one layer.
    if (width == 0 or height == 0 or depth_or_layers == 0) return;
    if (!recording.append(enc, .{ .copy_texture_to_texture = .{
        .src_texture = if (enc.dev.backend == .vulkan) toOpaque(src) else src.mtl,
        .src_mip = src_mip,
        .src_slice = src_slice,
        .src_x = src_x,
        .src_y = src_y,
        .src_z = src_z,
        .dst_texture = if (enc.dev.backend == .vulkan) toOpaque(dst) else dst.mtl,
        .dst_mip = dst_mip,
        .dst_slice = dst_slice,
        .dst_x = dst_x,
        .dst_y = dst_y,
        .dst_z = dst_z,
        .width = width,
        .height = height,
        .depth_or_layers = depth_or_layers,
    } })) return;
}

// ============================================================
// GPUQueue.writeTexture(destination, data, dataLayout, size)
// CPU-direct texture upload via Metal replaceRegion (shared unified memory).
// Mirrors writeBuffer: immediate memcpy, no command recording needed.
// ============================================================

/// Writes every aspect of the destination (`WGPUTextureAspect_All`); a
/// single-aspect write goes through wgpuQueueWriteTexture.
pub export fn doeNativeQueueWriteTexture(
    queue_raw: ?*anyopaque,
    texture_raw: ?*anyopaque,
    data_ptr: [*]const u8,
    data_len: usize,
    bytes_per_row: u32,
    rows_per_image: u32,
    dst_x: u32,
    dst_y: u32,
    dst_z: u32,
    dst_mip: u32,
    dst_slice: u32,
    width: u32,
    height: u32,
    depth_or_layers: u32,
) callconv(.c) void {
    queueWriteTexture(queue_raw, texture_raw, data_ptr, data_len, bytes_per_row, rows_per_image, dst_x, dst_y, dst_z, dst_mip, dst_slice, width, height, depth_or_layers, texture_values.WGPUTextureAspect_All);
}

/// `dst_slice` is an extra array-layer base added to `dst_z` on Vulkan; both
/// public entries pass 0 and carry the layer in `dst_z`.
pub fn queueWriteTexture(
    queue_raw: ?*anyopaque,
    texture_raw: ?*anyopaque,
    data_ptr: [*]const u8,
    data_len: usize,
    bytes_per_row: u32,
    rows_per_image: u32,
    dst_x: u32,
    dst_y: u32,
    dst_z: u32,
    dst_mip: u32,
    dst_slice: u32,
    width: u32,
    height: u32,
    depth_or_layers: u32,
    aspect: u32,
) void {
    const q = cast(DoeQueue, queue_raw);
    const tex = cast(DoeTexture, texture_raw) orelse return;
    if (tex.isUnavailable()) {
        if (q) |queue| queue.dev.error_scopes.deliver(@import("../../runtime/diagnostics/error_scope.zig").ERROR_TYPE_VALIDATION, "texture write requires a valid, undestroyed texture");
        return;
    }
    if (resource_ops.handleVulkanQueueWriteTexture(q, tex, .{
        .data_ptr = data_ptr,
        .data_len = data_len,
        .bytes_per_row = bytes_per_row,
        .rows_per_image = rows_per_image,
        .dst_mip = dst_mip,
        .origin = .{ dst_x, dst_y, dst_z +| dst_slice },
        .width = width,
        .height = height,
        .depth_or_layers = depth_or_layers,
        .aspect = aspect,
    })) {
        return;
    }
    _ = bridge.metal_bridge_texture_write_region(
        tex.mtl,
        @ptrCast(data_ptr),
        bytes_per_row,
        rows_per_image,
        dst_x,
        dst_y,
        dst_z,
        dst_mip,
        dst_slice,
        width,
        height,
        depth_or_layers,
    );
}
