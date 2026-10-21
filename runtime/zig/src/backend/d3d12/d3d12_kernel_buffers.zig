//! Command-handle storage for the bounded native D3D12 kernel lane.
const std = @import("std");
const timing = @import("../common/timing.zig");
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

pub const MAX_STORAGE_BINDINGS: usize = 8;

pub fn validate(bindings: ?[]const model.KernelBinding) !?[]const model.KernelBinding {
    const entries = bindings orelse return null;
    if (entries.len == 0) return null;
    if (entries.len > MAX_STORAGE_BINDINGS) return error.UnsupportedFeature;
    for (entries, 0..) |entry, index| {
        if (entry.group != 0 or entry.binding >= MAX_STORAGE_BINDINGS or entry.resource_kind != .buffer or
            (entry.buffer_type != binding.WGPUBufferBindingType_Storage and entry.buffer_type != binding.WGPUBufferBindingType_ReadOnlyStorage) or entry.buffer_offset != 0)
            return error.UnsupportedFeature;
        if (entry.resource_handle == 0 or entry.buffer_size == 0 or entry.buffer_size > MAX_RESIDENT_BYTES or entry.buffer_size % 4 != 0)
            return error.InvalidArgument;
        // Aliases need a separate read/write hazard contract, even for distinct bindings.
        for (entries[0..index]) |previous| {
            if (previous.binding == entry.binding or previous.resource_handle == entry.resource_handle) return error.UnsupportedFeature;
        }
    }
    return entries;
}

pub fn validateRange(capacity: u64, offset: u64, size: u64) !void {
    if (offset > capacity or size > capacity - offset) return error.InvalidArgument;
}

pub const TransferMetrics = struct { encode_ns: u64 = 0, submit_wait_ns: u64 = 0, submit_count: u32 = 0 };

const Buffer = struct { native: ?*anyopaque, size: u64 };
const BindingSet = struct {
    handles: [MAX_STORAGE_BINDINGS]u64 = @splat(0),
    count: u32 = 0,
    readonly_mask: u32 = 0,
};
const Table = struct { key: BindingSet, descriptor: u32 };

fn addMetrics(total: *TransferMetrics, transfer: TransferMetrics) void {
    total.encode_ns +|= transfer.encode_ns;
    total.submit_wait_ns +|= transfer.submit_wait_ns;
    total.submit_count += transfer.submit_count;
}

pub const State = struct {
    buffers: std.AutoHashMapUnmanaged(u64, Buffer) = .{},
    active: ?u64 = null,
    selected: BindingSet = .{},
    tables: std.ArrayListUnmanaged(Table) = .{},
    table_descriptor: u32 = 0,
    resident_bytes: u64 = 0,
    obligations: []ir.DispatchPrecondition = &.{},
    workgroup: [3]u32 = .{ 0, 0, 0 },
    minimum_bytes: [MAX_STORAGE_BINDINGS]u64 = @splat(0),
    runtime_array: [MAX_STORAGE_BINDINGS]bool = @splat(false),
    shader_valid: bool = false,

    pub fn deinit(self: *State, allocator: std.mem.Allocator) void {
        var iterator = self.buffers.valueIterator();
        while (iterator.next()) |buffer| bridge.c.d3d12_bridge_release(buffer.native);
        self.buffers.deinit(allocator);
        self.tables.deinit(allocator);
        allocator.free(self.obligations);
        self.* = .{};
    }

    pub fn layoutKey(self: *const State) u32 {
        var occupied: u32 = 0;
        for (self.selected.handles, 0..) |handle, index| {
            if (handle != 0) occupied |= @as(u32, 1) << @intCast(index);
        }
        return self.selected.count | (self.selected.readonly_mask << 8) | (occupied << 16);
    }

    fn ensureBuffer(self: *State, rt: anytype, handle: u64, size: u64, initialize: bool) !TransferMetrics {
        if (self.buffers.get(handle)) |existing| {
            if (existing.size != size) return error.InvalidArgument;
            return .{};
        }
        if (rt.device_lost) return error.DeviceLost;
        if (size > MAX_RESIDENT_BYTES - self.resident_bytes) return error.UnsupportedFeature;
        try self.buffers.ensureUnusedCapacity(rt.allocator, 1);
        const resource = bridge.c.d3d12_bridge_device_create_storage_buffer(rt.device, @intCast(size)) orelse return error.OutOfMemory;
        errdefer bridge.c.d3d12_bridge_release(resource);
        const initialization = if (initialize) try zeroBuffer(rt, resource, size) else TransferMetrics{};
        self.buffers.putAssumeCapacity(handle, .{ .native = resource, .size = size });
        self.resident_bytes += size;
        return initialization;
    }

    pub fn prepare(self: *State, rt: anytype, bindings: ?[]const model.KernelBinding, initialize: bool) !TransferMetrics {
        self.shader_valid = false;
        self.active = null;
        self.selected = .{};
        const entries = (try validate(bindings)) orelse return .{};
        if (rt.device_lost) return error.DeviceLost;
        var key = BindingSet{ .count = @intCast(entries.len) };
        var added: u64 = 0;
        // Complete layout and size validation before any native allocation.
        for (entries) |entry| {
            key.handles[entry.binding] = entry.resource_handle;
            if (entry.buffer_type == binding.WGPUBufferBindingType_ReadOnlyStorage) key.readonly_mask |= @as(u32, 1) << @intCast(entry.binding);
            if (self.buffers.get(entry.resource_handle)) |existing| {
                if (existing.size != entry.buffer_size) return error.InvalidArgument;
            } else added += entry.buffer_size;
        }
        if (added > MAX_RESIDENT_BYTES - self.resident_bytes) return error.UnsupportedFeature;
        var metrics = TransferMetrics{};
        for (entries) |entry| addMetrics(&metrics, try self.ensureBuffer(rt, entry.resource_handle, entry.buffer_size, initialize));
        for (self.tables.items) |table| {
            if (std.meta.eql(table.key, key)) {
                self.selected = key;
                self.active = entries[0].resource_handle;
                self.table_descriptor = table.descriptor;
                return metrics;
            }
        }
        try self.tables.ensureUnusedCapacity(rt.allocator, 1);
        try rt.descriptor_state.ensure_heaps(rt.device);
        if (rt.descriptor_state.cbv_srv_uav_capacity - rt.descriptor_state.cbv_srv_uav_next < key.count + 1) return error.UnsupportedFeature;
        if (rt.dispatch_info_buffer == null) {
            rt.dispatch_info_buffer = bridge.c.d3d12_bridge_device_create_buffer(rt.device, dispatch_info.DISPATCH_INFO_BUFFER_BYTES, dc.HEAP_TYPE_UPLOAD) orelse return error.OutOfMemory;
        }
        // The bridge combines all resource ranges into one table, ordered by binding.
        const descriptor = rt.descriptor_state.cbv_srv_uav_next;
        var ordinal: u32 = 0;
        for (key.handles, 0..) |handle, index| {
            if (handle == 0) continue;
            const buffer = self.buffers.get(handle).?;
            const actual = if (key.readonly_mask & (@as(u32, 1) << @intCast(index)) != 0)
                try rt.descriptor_state.allocate_srv_buffer(rt.device, buffer.native, @intCast(buffer.size / 4), 4)
            else
                try rt.descriptor_state.allocate_uav_buffer(rt.device, buffer.native, @intCast(buffer.size / 4), 4);
            std.debug.assert(actual == descriptor + ordinal);
            ordinal += 1;
        }
        const constants = try rt.descriptor_state.allocate_cbv(rt.device, rt.dispatch_info_buffer, dispatch_info.DISPATCH_INFO_BUFFER_BYTES);
        std.debug.assert(constants == descriptor + key.count);
        self.tables.appendAssumeCapacity(.{ .key = key, .descriptor = descriptor });
        self.selected = key;
        self.active = entries[0].resource_handle;
        self.table_descriptor = descriptor;
        return metrics;
    }

    // Admit the same analyzed module that is emitted, retaining every bounds obligation.
    pub fn admitShader(self: *State, allocator: std.mem.Allocator, module: *const ir.Module) !void {
        self.shader_valid = false;
        var count: u32 = 0;
        var minimum: [MAX_STORAGE_BINDINGS]u64 = @splat(0);
        var dynamic: [MAX_STORAGE_BINDINGS]bool = @splat(false);
        var seen: u32 = 0;
        for (module.globals.items) |global| {
            const point = global.binding orelse continue;
            if (point.group != 0 or point.binding >= MAX_STORAGE_BINDINGS or self.selected.handles[point.binding] == 0 or global.addr_space != .storage) return error.UnsupportedFeature;
            const bit = @as(u32, 1) << @intCast(point.binding);
            if (seen & bit != 0) return error.UnsupportedFeature;
            seen |= bit;
            const readonly = self.selected.readonly_mask & bit != 0;
            if (global.access != (if (readonly) ir.AccessMode.read else ir.AccessMode.read_write)) return error.UnsupportedFeature;
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
            minimum[point.binding] = @as(u64, length) * 4;
            dynamic[point.binding] = array.len == null;
            count += 1;
        }
        if (count != self.selected.count or count == 0 or module.texture_dispatch_preconditions.items.len != 0) return error.UnsupportedFeature;
        var workgroup: [3]u32 = .{ 0, 0, 0 };
        for (module.functions.items) |function| {
            if (function.stage != null) {
                if (function.stage != .compute or !std.mem.eql(u8, function.name, "main") or workgroup[0] != 0) return error.UnsupportedFeature;
                workgroup = function.workgroup_size;
            }
        }
        if (workgroup[0] == 0 or workgroup[1] == 0 or workgroup[2] == 0 or workgroup[0] > 1024 or workgroup[1] > 1024 or workgroup[2] > 64 or
            @as(u64, workgroup[0]) * workgroup[1] * workgroup[2] > 1024) return error.UnsupportedFeature;
        for (module.dispatch_preconditions.items) |obligation| {
            if (obligation.storage_binding.group != 0 or obligation.storage_binding.binding >= MAX_STORAGE_BINDINGS or self.selected.handles[obligation.storage_binding.binding] == 0 or obligation.kind == .uniform_extent) return error.UnsupportedFeature;
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
        if (self.active == null) return;
        if (!self.shader_valid) return error.InvalidState;
        if (x == 0 or x > 65535 or y == 0 or y > 65535 or z == 0 or z > 65535) return error.UnsupportedFeature;
        var extents: [MAX_STORAGE_BINDINGS]u64 = @splat(0);
        for (self.selected.handles, 0..) |handle, index| {
            if (handle == 0) continue;
            const buffer = self.buffers.get(handle) orelse return error.InvalidState;
            if (buffer.size < self.minimum_bytes[index]) return error.InvalidArgument;
            extents[index] = if (self.runtime_array[index]) buffer.size else self.minimum_bytes[index];
            // Preserve the conservative lane bound as well as the compiler's precise obligations.
            const lanes = @as(u64, x) * self.workgroup[0] * y * self.workgroup[1] * z * self.workgroup[2];
            if (lanes > extents[index] / 4) return error.UnsupportedFeature;
        }
        for (self.obligations) |obligation| {
            const required = try preconditions.required_buffer_bytes(obligation, .{ x, y, z }, self.workgroup);
            if (required > extents[obligation.storage_binding.binding]) return error.DispatchPreconditionFailed;
        }
    }

    pub fn bind(self: *const State, rt: anytype, list: ?*anyopaque) void {
        if (self.active == null) return;
        bridge.c.d3d12_bridge_command_list_set_descriptor_heaps(list, rt.descriptor_state.cbv_srv_uav_heap, rt.descriptor_state.sampler_heap);
        bridge.c.d3d12_bridge_command_list_set_compute_root_descriptor_table(list, STORAGE_ROOT_PARAMETER, rt.descriptor_state.cbv_srv_uav_heap, self.table_descriptor);
    }

    pub fn transition(self: *const State, list: ?*anyopaque, before_dispatch: bool) void {
        for (self.selected.handles, 0..) |handle, index| {
            if (handle == 0) continue;
            const buffer = self.buffers.get(handle).?;
            const state = if (self.selected.readonly_mask & (@as(u32, 1) << @intCast(index)) != 0) dc.RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE else dc.RESOURCE_STATE_UNORDERED_ACCESS;
            bridge.c.d3d12_bridge_command_list_resource_barrier_transition(list, buffer.native, if (before_dispatch) dc.RESOURCE_STATE_COPY_DEST else state, if (before_dispatch) state else dc.RESOURCE_STATE_COPY_DEST);
        }
    }

    pub fn write(self: *State, rt: anytype, handle: u64, offset: u64, declared_size: u64, data: []const u8) !TransferMetrics {
        if (handle == 0 or data.len == 0 or offset % 4 != 0 or data.len % 4 != 0) return error.InvalidArgument;
        const end = std.math.add(u64, offset, data.len) catch return error.InvalidArgument;
        const required = @max(declared_size, end);
        if (required > MAX_RESIDENT_BYTES or required % 4 != 0) return error.InvalidArgument;
        var metrics = TransferMetrics{};
        if (self.buffers.get(handle)) |existing| {
            if (required > existing.size) return error.InvalidArgument;
        } else {
            metrics = try self.ensureBuffer(rt, handle, required, true);
        }
        if (rt.device_lost) return error.DeviceLost;
        const buffer = self.buffers.get(handle).?;
        try validateRange(buffer.size, offset, data.len);
        const chunk = @min(data.len, ZERO_CHUNK_BYTES);
        const upload = bridge.c.d3d12_bridge_device_create_buffer(rt.device, chunk, dc.HEAP_TYPE_UPLOAD) orelse return error.OutOfMemory;
        defer bridge.c.d3d12_bridge_release(upload);
        var copied: usize = 0;
        while (copied < data.len) {
            const count = @min(data.len - copied, chunk);
            const mapped = bridge.c.d3d12_bridge_resource_map(upload) orelse return error.InvalidState;
            @memcpy(@as([*]u8, @ptrCast(mapped))[0..count], data[copied..][0..count]);
            bridge.c.d3d12_bridge_resource_unmap(upload);
            const transfer = try copyAndWait(rt, buffer.native, offset + copied, upload, 0, count);
            metrics.encode_ns +|= transfer.encode_ns;
            metrics.submit_wait_ns +|= transfer.submit_wait_ns;
            metrics.submit_count += transfer.submit_count;
            copied += count;
        }
        return metrics;
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
        _ = try copyAndWait(rt, readback, 0, buffer.native, offset, size);
        const mapped = bridge.c.d3d12_bridge_resource_map(readback) orelse return error.InvalidState;
        defer bridge.c.d3d12_bridge_resource_unmap(readback);
        @memcpy(output, @as([*]const u8, @ptrCast(mapped))[0..output.len]);
        return output;
    }
};

fn zeroBuffer(rt: anytype, resource: ?*anyopaque, size: u64) !TransferMetrics {
    const chunk: usize = @intCast(@min(size, ZERO_CHUNK_BYTES));
    const upload = bridge.c.d3d12_bridge_device_create_buffer(rt.device, chunk, dc.HEAP_TYPE_UPLOAD) orelse return error.OutOfMemory;
    defer bridge.c.d3d12_bridge_release(upload);
    const mapped = bridge.c.d3d12_bridge_resource_map(upload) orelse return error.InvalidState;
    @memset(@as([*]u8, @ptrCast(mapped))[0..chunk], 0);
    bridge.c.d3d12_bridge_resource_unmap(upload);
    var metrics = TransferMetrics{};
    var offset: u64 = 0;
    while (offset < size) {
        const count = @min(size - offset, chunk);
        const transfer = try copyAndWait(rt, resource, offset, upload, 0, count);
        metrics.encode_ns +|= transfer.encode_ns;
        metrics.submit_wait_ns +|= transfer.submit_wait_ns;
        metrics.submit_count += transfer.submit_count;
        offset += count;
    }
    return metrics;
}

fn copyAndWait(rt: anytype, destination: ?*anyopaque, destination_offset: u64, source: ?*anyopaque, source_offset: u64, size: u64) !TransferMetrics {
    if (rt.device_lost) return error.DeviceLost;
    if (rt.fence_value >= std.math.maxInt(u64) - 1) return error.InvalidState;
    _ = try rt.flush_queue();
    const encode_start = timing.now_ns();
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
    const encode_ns = timing.ns_delta(timing.now_ns(), encode_start);
    const submit_start = timing.now_ns();
    bridge.c.d3d12_bridge_queue_execute_command_list(rt.queue, list);
    submitted = true;
    rt.fence_value += 1;
    try bridge.check_signal(bridge.c.d3d12_bridge_queue_signal_checked(rt.queue, rt.fence, rt.fence_value));
    try bridge.check_wait(bridge.c.d3d12_bridge_fence_wait_checked(rt.fence, rt.fence_value));
    rt.noteCompletedFenceWait();
    if (rt.device_lost) return error.DeviceLost;
    return .{ .encode_ns = encode_ns, .submit_wait_ns = timing.ns_delta(timing.now_ns(), submit_start), .submit_count = 1 };
}
