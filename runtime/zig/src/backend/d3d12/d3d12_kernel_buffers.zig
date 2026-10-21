//! Command-handle storage for the bounded native D3D12 kernel lane.
const std = @import("std");
const model = @import("../../contracts/model/model_compute_types.zig");
const binding = @import("../../contracts/model/model_binding_value_types.zig");
const dc = @import("d3d12_constants.zig");
const bridge = @import("d3d12_bridge_decls.zig");
const ir = @import("../../compiler/wgsl/ir/ir.zig");
const preconditions = @import("../../core/compute/dispatch_preconditions.zig");

const dispatch_info = @import("../../contracts/shader_abi/dispatch_info.zig");
pub const STORAGE_ROOT_PARAMETER: u32 = 0;
const MAX_RESIDENT_BYTES: u64 = dc.BASELINE_MAX_BUFFER_SIZE;
const ZERO_CHUNK_BYTES: usize = 64 * 1024;

pub fn validate(bindings: ?[]const model.KernelBinding) !?model.KernelBinding {
    const entries = bindings orelse return null;
    if (entries.len == 0) return null;
    if (entries.len != 1) return error.UnsupportedFeature;
    const entry = entries[0];
    if (entry.group != 0 or entry.binding != 0 or entry.resource_kind != .buffer or
        entry.buffer_type != binding.WGPUBufferBindingType_Storage or entry.buffer_offset != 0)
        return error.UnsupportedFeature;
    if (entry.resource_handle == 0 or entry.buffer_size == 0 or entry.buffer_size > MAX_RESIDENT_BYTES or entry.buffer_size % 4 != 0)
        return error.InvalidArgument;
    return entry;
}

pub fn validateRange(capacity: u64, offset: u64, size: u64) !void {
    if (offset > capacity or size > capacity - offset) return error.InvalidArgument;
}

const Buffer = struct { native: ?*anyopaque, size: u64, descriptor: u32 };

pub const State = struct {
    buffers: std.AutoHashMapUnmanaged(u64, Buffer) = .{},
    active: ?u64 = null,
    resident_bytes: u64 = 0,
    obligations: []ir.DispatchPrecondition = &.{},
    workgroup: [3]u32 = .{ 0, 0, 0 },
    minimum_bytes: u64 = 0,
    runtime_array: bool = false,
    shader_valid: bool = false,

    pub fn deinit(self: *State, allocator: std.mem.Allocator) void {
        var iterator = self.buffers.valueIterator();
        while (iterator.next()) |buffer| bridge.c.d3d12_bridge_release(buffer.native);
        self.buffers.deinit(allocator);
        allocator.free(self.obligations);
        self.* = .{};
    }

    pub fn prepare(self: *State, rt: anytype, bindings: ?[]const model.KernelBinding, initialize: bool) !void {
        self.shader_valid = false;
        const entry = (try validate(bindings)) orelse {
            self.active = null;
            return;
        };
        if (rt.device_lost) return error.DeviceLost;
        if (self.buffers.get(entry.resource_handle)) |existing| {
            if (existing.size != entry.buffer_size) return error.InvalidArgument;
            self.active = entry.resource_handle;
            return;
        }
        if (entry.buffer_size > MAX_RESIDENT_BYTES - self.resident_bytes) return error.UnsupportedFeature;
        try self.buffers.ensureUnusedCapacity(rt.allocator, 1);
        const resource = bridge.c.d3d12_bridge_device_create_storage_buffer(rt.device, @intCast(entry.buffer_size)) orelse return error.OutOfMemory;
        errdefer bridge.c.d3d12_bridge_release(resource);
        if (initialize) try zeroBuffer(rt, resource, entry.buffer_size);
        try rt.descriptor_state.ensure_heaps(rt.device);
        if (rt.descriptor_state.cbv_srv_uav_capacity - rt.descriptor_state.cbv_srv_uav_next < 2) return error.UnsupportedFeature;
        if (rt.dispatch_info_buffer == null) {
            rt.dispatch_info_buffer = bridge.c.d3d12_bridge_device_create_buffer(rt.device, dispatch_info.DISPATCH_INFO_BUFFER_BYTES, dc.HEAP_TYPE_UPLOAD) orelse return error.OutOfMemory;
        }
        // The bridge combines resource ranges into one table. Reserve the
        // storage UAV followed immediately by this dispatch-info CBV.
        const descriptor = try rt.descriptor_state.allocate_uav_buffer(rt.device, resource, @intCast(entry.buffer_size / 4), 4);
        const constants = try rt.descriptor_state.allocate_cbv(rt.device, rt.dispatch_info_buffer, dispatch_info.DISPATCH_INFO_BUFFER_BYTES);
        std.debug.assert(constants == descriptor + 1);
        self.buffers.putAssumeCapacity(entry.resource_handle, .{ .native = resource, .size = entry.buffer_size, .descriptor = descriptor });
        self.resident_bytes += entry.buffer_size;
        self.active = entry.resource_handle;
    }

    // Admit the same analyzed module that is emitted, retaining every bounds obligation.
    pub fn admitShader(self: *State, allocator: std.mem.Allocator, module: *const ir.Module) !void {
        self.shader_valid = false;
        var count: u32 = 0;
        var minimum: u64 = 0;
        var dynamic = false;
        for (module.globals.items) |global| {
            const point = global.binding orelse continue;
            if (point.group != 0 or point.binding != 0 or global.addr_space != .storage or global.access != .read_write) return error.UnsupportedFeature;
            const array = switch (module.types.get(global.ty)) {
                .array => |value| value,
                else => return error.UnsupportedFeature,
            };
            switch (module.types.get(array.elem)) {
                .scalar => |scalar| switch (scalar) {
                    .u32, .i32, .f32 => {},
                    else => return error.UnsupportedFeature,
                },
                else => return error.UnsupportedFeature,
            }
            const length = array.len orelse 1;
            if (length == 0) return error.UnsupportedFeature;
            minimum = @as(u64, length) * 4;
            dynamic = array.len == null;
            count += 1;
        }
        if (count != 1 or module.texture_dispatch_preconditions.items.len != 0) return error.UnsupportedFeature;
        var workgroup: [3]u32 = .{ 0, 0, 0 };
        for (module.functions.items) |function| {
            if (function.stage != null) {
                if (function.stage != .compute or !std.mem.eql(u8, function.name, "main") or workgroup[0] != 0) return error.UnsupportedFeature;
                workgroup = function.workgroup_size;
            }
        }
        if (workgroup[0] == 0 or workgroup[0] > 1024 or workgroup[1] != 1 or workgroup[2] != 1) return error.UnsupportedFeature;
        for (module.dispatch_preconditions.items) |obligation| {
            if (obligation.storage_binding.group != 0 or obligation.storage_binding.binding != 0 or obligation.kind == .uniform_extent) return error.UnsupportedFeature;
        }
        const obligations = try allocator.dupe(ir.DispatchPrecondition, module.dispatch_preconditions.items);
        allocator.free(self.obligations);
        self.obligations = obligations;
        self.workgroup = workgroup;
        self.minimum_bytes = minimum;
        self.runtime_array = dynamic;
        self.shader_valid = true;
    }

    pub fn validateDispatch(self: *const State, x: u32, y: u32, z: u32) !void {
        const handle = self.active orelse return;
        if (!self.shader_valid) return error.InvalidState;
        const buffer = self.buffers.get(handle) orelse return error.InvalidState;
        if (buffer.size < self.minimum_bytes) return error.InvalidArgument;
        const extent = if (self.runtime_array) buffer.size else self.minimum_bytes;
        if (x == 0 or x > 65535 or y != 1 or z != 1 or @as(u64, x) * self.workgroup[0] > extent / 4) return error.UnsupportedFeature;
        for (self.obligations) |obligation| {
            const required = try preconditions.required_buffer_bytes(obligation, .{ x, y, z }, self.workgroup);
            if (required > extent) return error.DispatchPreconditionFailed;
        }
    }

    pub fn bind(self: *const State, rt: anytype, list: ?*anyopaque) void {
        const buffer = self.buffers.get(self.active orelse return).?;
        bridge.c.d3d12_bridge_command_list_set_descriptor_heaps(list, rt.descriptor_state.cbv_srv_uav_heap, rt.descriptor_state.sampler_heap);
        bridge.c.d3d12_bridge_command_list_set_compute_root_descriptor_table(list, STORAGE_ROOT_PARAMETER, rt.descriptor_state.cbv_srv_uav_heap, buffer.descriptor);
    }

    pub fn transition(self: *const State, list: ?*anyopaque, before_dispatch: bool) void {
        const buffer = self.buffers.get(self.active orelse return).?;
        bridge.c.d3d12_bridge_command_list_resource_barrier_transition(list, buffer.native, if (before_dispatch) dc.RESOURCE_STATE_COPY_DEST else dc.RESOURCE_STATE_UNORDERED_ACCESS, if (before_dispatch) dc.RESOURCE_STATE_UNORDERED_ACCESS else dc.RESOURCE_STATE_COPY_DEST);
    }

    pub fn capture(self: *State, rt: anytype, allocator: std.mem.Allocator, handle: u64, offset: u64, size: u64) ![]u8 {
        const buffer = self.buffers.get(handle) orelse return error.InvalidArgument;
        try validateRange(buffer.size, offset, size);
        if (rt.device_lost) return error.DeviceLost;
        if (size == 0) return allocator.alloc(u8, 0);
        const output = try allocator.alloc(u8, @intCast(size));
        errdefer allocator.free(output);
        const readback = bridge.c.d3d12_bridge_device_create_buffer(rt.device, @intCast(size), dc.HEAP_TYPE_READBACK) orelse return error.OutOfMemory;
        defer bridge.c.d3d12_bridge_release(readback);
        try copyAndWait(rt, readback, 0, buffer.native, offset, size);
        const mapped = bridge.c.d3d12_bridge_resource_map(readback) orelse return error.InvalidState;
        defer bridge.c.d3d12_bridge_resource_unmap(readback);
        @memcpy(output, @as([*]const u8, @ptrCast(mapped))[0..output.len]);
        return output;
    }
};

fn zeroBuffer(rt: anytype, resource: ?*anyopaque, size: u64) !void {
    const chunk: usize = @intCast(@min(size, ZERO_CHUNK_BYTES));
    const upload = bridge.c.d3d12_bridge_device_create_buffer(rt.device, chunk, dc.HEAP_TYPE_UPLOAD) orelse return error.OutOfMemory;
    defer bridge.c.d3d12_bridge_release(upload);
    const mapped = bridge.c.d3d12_bridge_resource_map(upload) orelse return error.InvalidState;
    @memset(@as([*]u8, @ptrCast(mapped))[0..chunk], 0);
    bridge.c.d3d12_bridge_resource_unmap(upload);
    var offset: u64 = 0;
    while (offset < size) {
        const count = @min(size - offset, chunk);
        try copyAndWait(rt, resource, offset, upload, 0, count);
        offset += count;
    }
}

fn copyAndWait(rt: anytype, destination: ?*anyopaque, destination_offset: u64, source: ?*anyopaque, source_offset: u64, size: u64) !void {
    if (rt.device_lost) return error.DeviceLost;
    if (rt.fence_value >= std.math.maxInt(u64) - 1) return error.InvalidState;
    _ = try rt.flush_queue();
    const allocator = bridge.c.d3d12_bridge_device_create_command_allocator(rt.device) orelse return error.OutOfMemory;
    defer bridge.c.d3d12_bridge_release(allocator);
    const list = bridge.c.d3d12_bridge_device_create_command_list(rt.device, allocator) orelse return error.OutOfMemory;
    defer bridge.c.d3d12_bridge_release(list);
    var submitted = false;
    // Even a failed signal/wait keeps transfer resources alive until terminal retirement.
    errdefer if (submitted) {
        _ = rt.drainForRetirement();
    };
    if (bridge.c.d3d12_bridge_command_list_copy_buffer_region(list, destination, destination_offset, source, source_offset, size) != 0) return error.InvalidArgument;
    if (bridge.c.d3d12_bridge_command_list_close_checked(list) != 0) return error.InvalidState;
    bridge.c.d3d12_bridge_queue_execute_command_list(rt.queue, list);
    submitted = true;
    rt.fence_value += 1;
    try bridge.check_signal(bridge.c.d3d12_bridge_queue_signal_checked(rt.queue, rt.fence, rt.fence_value));
    try bridge.check_wait(bridge.c.d3d12_bridge_fence_wait_checked(rt.fence, rt.fence_value));
    rt.noteCompletedFenceWait();
}
