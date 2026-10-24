// Native immediate-data admission. setImmediates validates its range against
// the device's maxImmediateSize; no Zig pipeline layout admits immediates
// (a nonzero immediateSize rejects), so a valid call records nothing a draw or
// dispatch reads.

const std = @import("std");
const recording = @import("doe_command_recording.zig");
const native_types = @import("../support/doe_native_object_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const render_bundle = @import("../../runtime/render/render_bundle.zig");
const bundle_native = @import("../render/doe_bundle_native.zig");
const contract = @import("../../contracts/command_recording.zig");

/// https://gpuweb.github.io/gpuweb/#dom-gpubindingcommandsmixin-setimmediates:
/// a 4-byte aligned offset and size within maxImmediateSize.
fn validate_payload(limit: u32, offset: u32, data_ptr: ?[*]const u8, data_len: usize) contract.Failure!void {
    if (data_len != 0 and data_ptr == null) return error.InvalidArgument;
    if (offset % 4 != 0 or data_len % 4 != 0 or offset > limit or data_len > limit - offset)
        return error.ImmediateDataRange;
}

fn device_limit(device: ?*const native_types.DoeDevice) u32 {
    return if (device) |owner| owner.max_immediate_size else native_types.DEFAULT_MAX_IMMEDIATE_SIZE;
}

pub export fn doeNativeBindingCommandsSetImmediates(
    encoder_raw: ?*anyopaque,
    index: u32,
    data_ptr: ?[*]const u8,
    data_len: usize,
) callconv(.c) void {
    _ = encoder_raw;
    _ = index;
    _ = data_ptr;
    _ = data_len;
    std.log.err("doe: doeNativeBindingCommandsSetImmediates: unsupported — " ++
        "abstract mixin entry has no standalone encoder type; use the concrete " ++
        "compute/render pass setImmediates entry points", .{});
}

pub export fn doeNativeComputePassSetImmediates(
    encoder_raw: ?*anyopaque,
    index: u32,
    data_ptr: ?[*]const u8,
    data_len: usize,
) callconv(.c) void {
    const encoder = native_helpers.cast(native_types.DoeComputePass, encoder_raw) orelse return;
    if (!recording.requirePass(encoder.enc, @intFromPtr(encoder))) return;
    validate_payload(device_limit(encoder.enc.dev), index, data_ptr, data_len) catch |err| recording.fail(encoder.enc, err);
}

pub export fn doeNativeRenderPassSetImmediates(
    encoder_raw: ?*anyopaque,
    index: u32,
    data_ptr: ?[*]const u8,
    data_len: usize,
) callconv(.c) void {
    const encoder = native_helpers.cast(native_types.DoeRenderPass, encoder_raw) orelse return;
    if (!recording.requirePass(encoder.enc, @intFromPtr(encoder))) return;
    validate_payload(device_limit(encoder.enc.dev), index, data_ptr, data_len) catch |err| recording.fail(encoder.enc, err);
}

pub export fn doeNativeRenderBundleEncoderSetImmediates(
    encoder_raw: ?*anyopaque,
    index: u32,
    data_ptr: ?[*]const u8,
    data_len: usize,
) callconv(.c) void {
    const encoder = render_bundle.cast_bundle_encoder(encoder_raw) orelse return;
    if (!bundle_native.requireBundleOpen(encoder)) return;
    const device = if (encoder.device_lease) |lease| native_helpers.cast(native_types.DoeDevice, lease.handle) else null;
    validate_payload(device_limit(device), index, data_ptr, data_len) catch |err| bundle_native.failBundle(encoder, err);
}

test "setImmediates accepts aligned ranges within the limit and invalidates otherwise" {
    const encoders = @import("doe_encoder_native.zig");
    const compute = @import("../compute/doe_compute_ext_native.zig");
    const render = @import("../render/doe_render_native.zig");
    var device = native_types.DoeDevice{};
    const bytes = [_]u8{0} ** 64;
    for ([_]bool{ false, true }) |render_pass| {
        for ([_]struct { offset: u32, len: usize, valid: bool }{
            .{ .offset = 0, .len = 0, .valid = true },
            .{ .offset = 0, .len = 64, .valid = true },
            .{ .offset = 60, .len = 4, .valid = true },
            .{ .offset = 2, .len = 4, .valid = false },
            .{ .offset = 0, .len = 6, .valid = false },
            .{ .offset = 64, .len = 4, .valid = false },
            .{ .offset = 68, .len = 0, .valid = false },
        }) |case| {
            const encoder = try encoders.createEncoder(std.testing.allocator, &device);
            defer encoders.doeNativeCommandEncoderRelease(encoder);
            const pass = if (render_pass)
                render.doeNativeCommandEncoderBeginRenderPass(encoder, null).?
            else
                encoders.doeNativeCommandEncoderBeginComputePass(encoder, null).?;
            defer if (render_pass) render.doeNativeRenderPassRelease(pass) else compute.doeNativeComputePassRelease(pass);
            if (render_pass)
                doeNativeRenderPassSetImmediates(pass, case.offset, &bytes, case.len)
            else
                doeNativeComputePassSetImmediates(pass, case.offset, &bytes, case.len);
            if (case.valid) {
                try std.testing.expect(encoder.state == .pass);
            } else {
                try std.testing.expectEqual(error.ImmediateDataRange, encoder.state.failed);
            }
        }
    }
    var descriptor = std.mem.zeroes(@import("../../full/render/wgpu_render_types.zig").RenderBundleEncoderDescriptor);
    descriptor.sampleCount = 1;
    // A bundle layout names at least one attachment.
    const color_formats = [_]u32{@import("../../core/abi/wgpu_texture_base_types.zig").WGPUTextureFormat_RGBA8Unorm};
    descriptor.colorFormatCount = color_formats.len;
    descriptor.colorFormats = &color_formats;
    const encoder = bundle_native.doeNativeDeviceCreateRenderBundleEncoder(&device, &descriptor).?;
    defer bundle_native.doeNativeRenderBundleEncoderRelease(encoder);
    doeNativeRenderBundleEncoderSetImmediates(encoder, 0, &bytes, 16);
    try std.testing.expect(render_bundle.cast_bundle_encoder(encoder).?.state == .open);
    doeNativeRenderBundleEncoderSetImmediates(encoder, 4, &bytes, 64);
    try std.testing.expectEqual(error.ImmediateDataRange, render_bundle.cast_bundle_encoder(encoder).?.state.failed);
}

test "pipeline layouts validate immediateSize before retaining bindings" {
    const bindings = @import("../resource/doe_bind_group_native.zig");
    const abi = @import("../../core/abi/wgpu_pipeline_descriptor_types.zig");
    var device = native_types.DoeDevice{};
    var group = native_types.DoeBindGroupLayout{};
    try std.testing.expect(bindings.doeNativeDeviceCreatePipelineLayoutOne(&device, &group, 2) == null);
    var descriptor = std.mem.zeroes(abi.WGPUPipelineLayoutDescriptor);
    descriptor.immediateSize = native_types.DEFAULT_MAX_IMMEDIATE_SIZE + 4;
    try std.testing.expect(bindings.doeNativeDeviceCreatePipelineLayout(&device, &descriptor) == null);
    try std.testing.expectEqual(@as(u32, 1), group.ref_count);
    for ([_]u32{ 0, 4, native_types.DEFAULT_MAX_IMMEDIATE_SIZE }) |size| {
        const layout = bindings.doeNativeDeviceCreatePipelineLayoutOne(&device, &group, size).?;
        bindings.doeNativePipelineLayoutRelease(layout);
    }
    try std.testing.expectEqual(@as(u32, 1), group.ref_count);
    try std.testing.expectError(error.InvalidArgument, validate_payload(64, 0, null, 4));
}
