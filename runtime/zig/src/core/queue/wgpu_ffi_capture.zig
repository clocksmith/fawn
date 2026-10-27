//! A capture keeps its submitted copy and callback records until both queue
//! completion and mapping have retired. A deadline never releases that lease.
const std = @import("std");
const proc_types = @import("../abi/wgpu_proc_types.zig");
const abi_base = proc_types.base;
const abi_descriptor = proc_types.descriptor;
const runtime_state = @import("../abi/wgpu_runtime_state_defs.zig");
const loader = @import("../abi/wgpu_loader.zig");

pub const PendingCapture = struct {
    buffer: abi_base.WGPUBuffer,
    commands: abi_base.WGPUCommandBuffer,
    queue_state: runtime_state.QueueSubmitState = .{},
    queue_future: abi_base.WGPUFuture = .{ .id = 0 },
    map_state: runtime_state.BufferMapState = .{},
    map_future: abi_base.WGPUFuture = .{ .id = 0 },
    map_requested: bool = false,
};

pub fn requireReady(self: anytype) !void {
    if (self.core.capture_error) |err| return err;
    if (self.core.pending_capture != null) return error.CaptureCompletionUnknown;
}

fn waitForFuture(self: anytype, future: abi_base.WGPUFuture, done: *const bool) !void {
    if (done.*) return;
    if (future.id == 0) return error.CaptureFutureUnavailable;
    var waits = [_]abi_descriptor.WGPUFutureWaitInfo{.{ .future = future, .completed = abi_base.WGPU_FALSE }};
    // The delegate instance does not request TimedWaitAny. Poll this exact
    // future with zero timeout instead of adding an instance capability.
    var deadline = try std.time.Timer.start();
    while (true) {
        const status = self.core.procs.?.wgpuInstanceWaitAny(self.core.instance.?, waits.len, &waits, 0);
        switch (status) {
            .success => {
                if (!done.*) return error.WaitAnyIncomplete;
                return;
            },
            .timedOut => {},
            .@"error" => return error.WaitAnyFailed,
            else => return error.WaitAnyUnsupported,
        }
        if (deadline.read() >= self.core.webgpu_ffi_queue_wait_timeout_ns)
            return error.WaitTimedOut;
        std.Thread.sleep(1_000_000);
    }
}

fn releasePending(self: anytype) void {
    const pending = self.core.pending_capture.?;
    const procs = self.core.procs.?;
    if (pending.map_requested) procs.wgpuBufferUnmap(pending.buffer);
    procs.wgpuCommandBufferRelease(pending.commands);
    procs.wgpuBufferRelease(pending.buffer);
    self.core.pending_capture = null;
    self.core.allocator.destroy(pending);
}

/// Called before any backend resource or library teardown. Failed/cancelled
/// map callbacks retire only mapping; they never substitute for queue success.
pub fn retirePending(self: anytype) bool {
    const pending = self.core.pending_capture orelse return true;
    waitForFuture(self, pending.queue_future, &pending.queue_state.done) catch return false;
    if (pending.queue_state.status != .success) return false;
    if (pending.map_requested) {
        waitForFuture(self, pending.map_future, &pending.map_state.done) catch return false;
    }
    releasePending(self);
    return true;
}

pub fn captureBuffer(
    self: anytype,
    allocator: std.mem.Allocator,
    handle: u64,
    offset: u64,
    size: u64,
) ![]u8 {
    try requireReady(self);
    if (size == 0 or offset % 4 != 0 or size % 4 != 0) return error.InvalidArgument;
    const size_usize = std.math.cast(usize, size) orelse return error.InvalidArgument;
    const procs = self.core.procs orelse return error.ProceduralNotReady;
    const queue = self.core.queue orelse return error.ProceduralNotReady;
    const device = self.core.device orelse return error.ProceduralNotReady;
    const source_record = self.core.buffers.get(handle) orelse return error.InvalidArgument;
    const end = std.math.add(u64, offset, size) catch return error.InvalidArgument;
    if (end > source_record.size) return error.InvalidArgument;
    if ((source_record.usage & abi_base.WGPUBufferUsage_CopySrc) == 0) return error.UnsupportedFeature;

    const pending = try self.core.allocator.create(PendingCapture);
    var submitted = false;
    defer if (!submitted) self.core.allocator.destroy(pending);
    const buffer = procs.wgpuDeviceCreateBuffer(device, &abi_descriptor.WGPUBufferDescriptor{
        .nextInChain = null,
        .label = loader.emptyStringView(),
        .usage = abi_base.WGPUBufferUsage_MapRead | abi_base.WGPUBufferUsage_CopyDst,
        .size = size,
        .mappedAtCreation = abi_base.WGPU_FALSE,
    });
    if (buffer == null) return error.BufferAllocationFailed;
    defer if (!submitted) procs.wgpuBufferRelease(buffer);
    const encoder = procs.wgpuDeviceCreateCommandEncoder(device, &abi_descriptor.WGPUCommandEncoderDescriptor{
        .nextInChain = null,
        .label = loader.emptyStringView(),
    });
    if (encoder == null) return error.CommandEncoderUnavailable;
    defer procs.wgpuCommandEncoderRelease(encoder);
    procs.wgpuCommandEncoderCopyBufferToBuffer(encoder, source_record.buffer, offset, buffer, 0, size);
    const commands = procs.wgpuCommandEncoderFinish(encoder, &abi_descriptor.WGPUCommandBufferDescriptor{
        .nextInChain = null,
        .label = loader.emptyStringView(),
    });
    if (commands == null) return error.CommandEncoderFinishFailed;
    pending.* = .{ .buffer = buffer, .commands = commands };
    self.core.pending_capture = pending;
    submitted = true;
    errdefer |err| {
        if (self.core.capture_error == null) self.core.capture_error = err;
        // Reclaim only when the exact futures establish terminal ownership.
        // Preserve the first failure even if a late callback settles now.
        if (pending.queue_state.done and pending.queue_state.status == .success and
            (!pending.map_requested or pending.map_state.done)) releasePending(self);
    }
    var command_buffers = [_]abi_base.WGPUCommandBuffer{commands};
    procs.wgpuQueueSubmit(queue, command_buffers.len, &command_buffers);
    pending.queue_future = procs.wgpuQueueOnSubmittedWorkDone(queue, .{
        .nextInChain = null,
        .mode = abi_descriptor.WGPUCallbackMode_WaitAnyOnly,
        .callback = loader.queueWorkDoneCallback,
        .userdata1 = &pending.queue_state,
        .userdata2 = null,
    });
    try waitForFuture(self, pending.queue_future, &pending.queue_state.done);
    if (pending.queue_state.status != .success) return error.QueueSubmissionError;
    pending.map_requested = true;
    pending.map_future = procs.wgpuBufferMapAsync(buffer, abi_base.WGPUMapMode_Read, 0, size_usize, .{
        .nextInChain = null,
        .mode = abi_descriptor.WGPUCallbackMode_WaitAnyOnly,
        .callback = loader.bufferMapCallback,
        .userdata1 = &pending.map_state,
        .userdata2 = null,
    });
    try waitForFuture(self, pending.map_future, &pending.map_state.done);
    if (pending.map_state.status != abi_base.WGPUMapAsyncStatus_Success) return error.BufferMapFailed;
    const mapped = procs.wgpuBufferGetConstMappedRange(buffer, 0, size_usize) orelse return error.BufferMapFailed;
    const bytes = @as([*]const u8, @ptrCast(mapped))[0..size_usize];
    const result = try allocator.dupe(u8, bytes);
    releasePending(self);
    return result;
}
