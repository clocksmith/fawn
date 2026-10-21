const std = @import("std");
const queue_submit_ops = @import("../../backend/dropin_queue_submit.zig");
const bridge = queue_submit_ops.d3d12_bridge;
const native_types = @import("../support/doe_native_object_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const native_rt_helpers = @import("../support/doe_native_runtime_helpers.zig");
const shared = @import("doe_queue_submit_shared.zig");
const commands = @import("../support/doe_native_command_types.zig");
const abi = @import("../../core/abi/wgpu_core_base_types.zig");
const errors = @import("../../runtime/diagnostics/error_scope.zig");
const compute = @import("../compute/doe_compute_d3d12.zig");

const alloc = native_helpers.alloc;
const cast = native_helpers.cast;
const DoeCommandBuffer = native_types.DoeCommandBuffer;
const DoeQueue = native_types.DoeQueue;
const d3d12_native_render_pass = queue_submit_ops.d3d12_native_render_pass;

pub fn submit_d3d12_commands(q: *DoeQueue, count: usize, cmd_bufs: [*]const ?*anyopaque) void {
    const rt = native_rt_helpers.device_d3d12_runtime(q.dev) orelse return;
    // Validate the entire submission before recording or executing any part.
    var has_copy = false;
    var has_compute = false;
    var has_render = false;
    for (cmd_bufs[0..count]) |raw| {
        const cb = cast(DoeCommandBuffer, raw) orelse return;
        for (cb.cmds.items) |cmd| switch (cmd) {
            .copy_buf => has_copy = true,
            .clear_buffer => |clear| {
                _ = validateClear(q.dev, clear) catch |err| {
                    q.dev.error_scopes.deliver(errors.ERROR_TYPE_VALIDATION, @errorName(err));
                    return;
                };
                has_copy = true;
            },
            .dispatch => |dispatch| {
                compute.validate(q.dev, dispatch) catch |err| {
                    shared.deliverInternalError(q.dev, "D3D12 compute admission: {s}", .{@errorName(err)});
                    return;
                };
                has_compute = true;
            },
            .render_pass => has_render = true,
            else => {
                shared.deliverInternalError(q.dev, "D3D12 unsupported recorded command: {s}", .{@tagName(cmd)});
                return;
            },
        };
    }
    if ((has_copy or has_compute) and has_render) {
        shared.deliverInternalError(q.dev, "D3D12 mixed render/copy/compute submission is unsupported", .{});
        return;
    }
    rt.flush_before_dropin_submit_if_needed() catch |err| {
        shared.deliverInternalError(q.dev, "doe_queue_submit: d3d12 pre-submit flush: {s}", .{@errorName(err)});
        return;
    };

    const cmd_allocator = bridge.c.d3d12_bridge_device_create_command_allocator(rt.device) orelse return;
    var owns_cmd_allocator = true;
    defer if (owns_cmd_allocator) bridge.c.d3d12_bridge_release(cmd_allocator);

    const cmd_list = bridge.c.d3d12_bridge_device_create_command_list(rt.device, cmd_allocator) orelse return;
    var owns_cmd_list = true;
    defer if (owns_cmd_list) bridge.c.d3d12_bridge_release(cmd_list);

    var retained_handles: std.ArrayListUnmanaged(?*anyopaque) = .{};
    var owns_retained_handles = true;
    defer if (owns_retained_handles) {
        for (retained_handles.items) |maybe_handle| {
            if (maybe_handle) |handle| bridge.c.d3d12_bridge_release(handle);
        }
        retained_handles.deinit(alloc);
    };

    var has_gpu_work = false;
    for (cmd_bufs[0..count]) |raw| {
        const cb = cast(DoeCommandBuffer, raw) orelse continue;
        for (cb.cmds.items) |cmd| {
            switch (cmd) {
                .dispatch => |dispatch| {
                    compute.record(q.dev, cmd_list, dispatch, &retained_handles) catch |err| {
                        shared.deliverInternalError(q.dev, "D3D12 compute recording: {s}", .{@errorName(err)});
                        return;
                    };
                    has_gpu_work = has_gpu_work or dispatch.x != 0;
                },
                .clear_buffer => |clear| {
                    recordClear(q.dev, rt.device, cmd_list, clear, &retained_handles) catch |err| {
                        shared.deliverInternalError(q.dev, "D3D12 clear recording: {s}", .{@errorName(err)});
                        return;
                    };
                    has_gpu_work = has_gpu_work or clear.size != 0;
                },
                .copy_buf => |copy| {
                    const src = cast(native_types.DoeBuffer, copy.src) orelse return;
                    const dst = cast(native_types.DoeBuffer, copy.dst) orelse return;
                    retained_handles.ensureUnusedCapacity(alloc, 2) catch |err| {
                        shared.deliverInternalError(q.dev, "D3D12 copy retention: {s}", .{@errorName(err)});
                        return;
                    };
                    if (bridge.c.d3d12_bridge_command_list_copy_buffer_region(cmd_list, dst.mtl, copy.dst_off, src.mtl, copy.src_off, copy.size) != 0) {
                        shared.deliverInternalError(q.dev, "D3D12 copy rejected invalid buffers, ranges or heap types", .{});
                        return;
                    }
                    bridge.c.d3d12_bridge_retain(src.mtl);
                    bridge.c.d3d12_bridge_retain(dst.mtl);
                    retained_handles.appendAssumeCapacity(src.mtl);
                    retained_handles.appendAssumeCapacity(dst.mtl);
                    has_gpu_work = true;
                },
                .render_pass => |render_pass_cmd| {
                    d3d12_native_render_pass.record_render_pass_command(
                        alloc,
                        &retained_handles,
                        rt.device,
                        cmd_list,
                        render_pass_cmd,
                        &rt.descriptor_state,
                        &rt.texture_view_state,
                        &rt.sampler_state,
                    ) catch |err| {
                        shared.deliverInternalError(q.dev, "doe_queue_submit: d3d12 render recording: {s}", .{@errorName(err)});
                        return;
                    };
                    has_gpu_work = true;
                },
                else => unreachable, // Exhaustive admission above.
            }
        }
    }

    if (!has_gpu_work) return;

    if (bridge.c.d3d12_bridge_command_list_close_checked(cmd_list) != 0) {
        shared.deliverInternalError(q.dev, "doe_queue_submit: d3d12 command close failed", .{});
        return;
    }
    if (rt.fence_value == std.math.maxInt(u64)) {
        shared.deliverInternalError(q.dev, "D3D12 submission fence exhausted", .{});
        return;
    }
    bridge.c.d3d12_bridge_queue_execute_command_list(rt.queue, cmd_list);
    rt.fence_value += 1;
    bridge.check_signal(bridge.c.d3d12_bridge_queue_signal_checked(rt.queue, rt.fence, rt.fence_value)) catch |err| {
        _ = rt.drainForRetirement();
        shared.deliverInternalError(q.dev, "D3D12 submission signal: {s}", .{@errorName(err)});
        return;
    };
    rt.trackDropinSubmission(cmd_allocator, cmd_list, &retained_handles) catch |err| {
        shared.deliverInternalError(q.dev, "doe_queue_submit: d3d12 retirement: {s}", .{@errorName(err)});
        return;
    };
    owns_cmd_allocator = false;
    owns_cmd_list = false;
    owns_retained_handles = false;
}

const Clear = @FieldType(commands.RecordedCmd, "clear_buffer");
// Bound upload memory independently of the requested clear range.
const CLEAR_UPLOAD_BYTES: usize = 64 * 1024;

fn validateClear(device: *native_types.DoeDevice, clear: Clear) !*native_types.DoeBuffer {
    const buffer = cast(native_types.DoeBuffer, clear.buffer) orelse return error.InvalidClearBuffer;
    if (buffer.dev != device or buffer.backend != .d3d12) return error.ClearDeviceMismatch;
    if (buffer.error_object or buffer.destroyed or buffer.mapped or buffer.map_pending or buffer.mtl == null) return error.ClearBufferUnavailable;
    if ((buffer.usage & abi.WGPUBufferUsage_CopyDst) == 0) return error.ClearRequiresCopyDst;
    if (buffer.d3d12_heap_type != 1 and buffer.d3d12_heap_type != 3) return error.UnsupportedClearHeap;
    if ((clear.offset | clear.size) & 3 != 0 or clear.offset > buffer.size or clear.size > buffer.size - clear.offset) return error.InvalidClearRange;
    return buffer;
}

fn recordClear(device: *native_types.DoeDevice, native_device: ?*anyopaque, list: ?*anyopaque, clear: Clear, retained: *std.ArrayListUnmanaged(?*anyopaque)) !void {
    const buffer = try validateClear(device, clear);
    if (clear.size == 0) return;
    const upload_size: usize = @intCast(@min(clear.size, CLEAR_UPLOAD_BYTES));
    try retained.ensureUnusedCapacity(alloc, 2);
    const upload = bridge.c.d3d12_bridge_device_create_buffer(native_device, upload_size, 2) orelse return error.ClearUploadAllocationFailed;
    errdefer bridge.c.d3d12_bridge_release(upload);
    const mapped = bridge.c.d3d12_bridge_resource_map(upload) orelse return error.ClearUploadMapFailed;
    @memset(@as([*]u8, @ptrCast(mapped))[0..upload_size], 0);
    bridge.c.d3d12_bridge_resource_unmap(upload);
    var written: u64 = 0;
    while (written < clear.size) {
        const chunk = @min(clear.size - written, upload_size);
        if (bridge.c.d3d12_bridge_command_list_copy_buffer_region(list, buffer.mtl, clear.offset + written, upload, 0, chunk) != 0) return error.ClearCopyFailed;
        written += chunk;
    }
    // Both the immutable zero source and destination outlive command recording.
    bridge.c.d3d12_bridge_retain(buffer.mtl);
    retained.appendAssumeCapacity(upload);
    retained.appendAssumeCapacity(buffer.mtl);
}
