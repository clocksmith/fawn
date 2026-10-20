const std = @import("std");
const queue_submit_ops = @import("../../backend/dropin_queue_submit.zig");
const bridge = queue_submit_ops.d3d12_bridge;
const native_types = @import("../support/doe_native_object_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const native_rt_helpers = @import("../support/doe_native_runtime_helpers.zig");
const shared = @import("doe_queue_submit_shared.zig");
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
