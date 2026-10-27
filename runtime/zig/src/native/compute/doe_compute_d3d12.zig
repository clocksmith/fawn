//! Bounded public D3D12 compute admission and recorded-command integration.
//! Dense group(0) scalar storage arrays with explicit or automatic layouts and one selected compute entry.
const std = @import("std");
const objects = @import("../support/doe_native_object_types.zig");
const helpers = @import("../support/doe_native_object_helpers.zig");
const commands = @import("../support/doe_native_command_types.zig");
const resources = @import("../../backend/dropin_resource_ops.zig");
const bridge = @import("../../backend/dropin_queue_submit.zig").d3d12_bridge.c;
const ir = @import("../../compiler/wgsl/ir/ir.zig");
const compute_translation = @import("../../compiler/wgsl/runtime/runtime_compute_translation.zig");
const bindings = @import("../../compiler/wgsl/pipeline/binding_reflection.zig");
const analysis = @import("../../compiler/wgsl/pipeline/analysis.zig");
const hlsl = @import("../../compiler/wgsl/pipeline/translate_hlsl.zig");
const binding = @import("../../contracts/model/model_binding_value_types.zig");
const abi = @import("../../core/abi/wgpu_core_base_types.zig");
const ShaderDiagnostic = @import("../shader/shader_diagnostic.zig").ShaderDiagnostic;
const preconditions = @import("doe_compute_preconditions_native.zig");
const layouts = @import("../resource/doe_bind_group_native.zig");
const reflection = @import("../shader/shader_binding_reflection.zig");
const abi_pipeline = @import("../../core/abi/wgpu_pipeline_descriptor_types.zig");
const dc = resources.d3d12_constants;
const alloc = helpers.alloc;
const Dispatch = @FieldType(commands.RecordedCmd, "dispatch");
const StorageBinding = objects.D3D12StorageBinding;
const MAX_STORAGE_BINDINGS = objects.D3D12_STORAGE_BINDING_LIMIT;

pub fn createPipeline(device: *objects.DoeDevice, shader: *objects.DoeShaderModule, layout: ?*objects.DoePipelineLayout, entry: []const u8, diagnostic: *ShaderDiagnostic) !*objects.DoeComputePipeline {
    if (entry.len == 0) return error.UnsupportedEntryPoint;
    const source = shader.wgsl_source orelse return error.UnsupportedShaderFormat;
    var module = (try analysis.analyze(.{ .allocator = alloc, .source = source, .robustness = compute_translation.compute_runtime_robustness_config(), .diagnostic = &diagnostic.compiler })).module;
    defer module.deinit();
    const workgroup = try selectedWorkgroup(&module, entry);
    var used: [bindings.MAX_BINDINGS]bindings.BindingMeta = undefined;
    const used_count = try bindings.extractEntryPointBindings(alloc, &module, entry, &used);
    var count: u32 = 0;
    var storage: [MAX_STORAGE_BINDINGS]StorageBinding = @splat(.{});
    for (module.globals.items) |global| {
        const point = global.binding orelse continue;
        const required = for (used[0..used_count]) |meta| {
            if (meta.group == point.group and meta.binding == point.binding) break true;
        } else false;
        if (!required) continue;
        if (point.group != 0 or point.binding >= MAX_STORAGE_BINDINGS or global.addr_space != .storage or
            (global.access != .read_write and global.access != .read) or storage[point.binding].minimum_size != 0) return error.UnsupportedBinding;
        const array = switch (module.types.get(global.ty)) {
            .array => |value| value,
            else => return error.UnsupportedBinding,
        };
        // A runtime array requires at least one scalar element. Its actual
        // extent is the bound view, not the underlying allocation's size.
        const length = array.len orelse 1;
        switch (module.types.get(array.elem)) {
            .scalar => |scalar| switch (scalar) {
                .u32, .i32, .f32 => {},
                else => return error.UnsupportedStorageType,
            },
            else => return error.UnsupportedStorageType,
        }
        if (length == 0) return error.UnsupportedBinding;
        storage[point.binding] = .{ .minimum_size = @as(u64, length) * 4, .runtime_array = array.len == null, .read_only = global.access == .read };
        count += 1;
    }
    if (count == 0) return error.UnsupportedBinding;
    // Dense slots preserve the public flat binding contract; sparse CLI admission is separate.
    for (storage[0..count]) |slot| if (slot.minimum_size == 0) return error.UnsupportedBinding;
    const pl = layout orelse try createAutomaticLayout(device, shader, storage[0..count]);
    // Automatic creation owns one reference; the pipeline retains its own below.
    defer if (layout == null) layouts.doeNativePipelineLayoutRelease(helpers.toOpaque(pl));
    if (pl.bind_group_layout_count != 1 or pl.immediate_size != 0) return error.UnsupportedLayout;
    const bgl = pl.bind_group_layouts[0] orelse return error.UnsupportedLayout;
    const entries = bgl.entries orelse return error.UnsupportedLayout;
    if (entries.len != count) return error.UnsupportedLayout;
    var seen: [MAX_STORAGE_BINDINGS]bool = @splat(false);
    for (entries) |item| {
        if (item.binding >= count or seen[item.binding] or item.binding_array_size != 0) return error.UnsupportedLayout;
        const expected = if (storage[item.binding].read_only) binding.WGPUBufferBindingType_ReadOnlyStorage else binding.WGPUBufferBindingType_Storage;
        if (item.buffer_binding_type != expected) return error.UnsupportedLayout;
        seen[item.binding] = true;
    }
    const text = try alloc.alloc(u8, hlsl.MAX_OUTPUT);
    defer alloc.free(text);
    // Emit the same analyzed module whose proof obligations we retain below.
    const length = try hlsl.emitter.emit(&module, text);
    const target_entry = try hlsl.emitter.entryPointName(alloc, entry);
    defer alloc.free(target_entry);
    const bytecode = try resources.d3d12_compute.compileHlslEntryToBytecode(alloc, text[0..length], target_entry);
    defer alloc.free(bytecode);
    var ranges: [MAX_STORAGE_BINDINGS]bridge.D3D12DescriptorRangeDesc = undefined;
    for (storage[0..count], 0..) |slot, index| ranges[index] = .{
        .range_type = if (slot.read_only) dc.DESCRIPTOR_RANGE_TYPE_SRV else dc.DESCRIPTOR_RANGE_TYPE_UAV,
        .num_descriptors = 1,
        .base_shader_register = @intCast(index),
        .register_space = 0,
    };
    const root = bridge.d3d12_bridge_device_create_root_signature_with_tables(device.mtl_device, &ranges, count, 0) orelse return error.RootSignatureCreationFailed;
    errdefer bridge.d3d12_bridge_release(root);
    const pso = bridge.d3d12_bridge_device_create_compute_pipeline(device.mtl_device, root, bytecode.ptr, bytecode.len) orelse return error.ComputePipelineCreationFailed;
    errdefer bridge.d3d12_bridge_release(pso);
    const obligations = try alloc.dupe(@import("../../compiler/wgsl/ir/ir.zig").DispatchPrecondition, module.dispatch_preconditions.items);
    errdefer alloc.free(obligations);
    if (module.texture_dispatch_preconditions.items.len != 0) return error.UnsupportedTexturePreconditions;
    const pipeline = try alloc.create(objects.DoeComputePipeline);
    pipeline.* = .{ .mtl_pso = pso, .d3d12_root_signature = root, .d3d12_storage = storage, .layout = pl, .shader_module = shader, .binding_count = count, .wg_x = workgroup[0], .wg_y = 1, .wg_z = 1, .dispatch_preconditions = obligations };
    helpers.object_add_ref(objects.DoePipelineLayout, helpers.toOpaque(pl));
    helpers.object_add_ref(objects.DoeShaderModule, helpers.toOpaque(shader));
    return pipeline;
}

fn createAutomaticLayout(device: *objects.DoeDevice, shader: *objects.DoeShaderModule, storage: []const StorageBinding) !*objects.DoePipelineLayout {
    // Called only after the analyzed module satisfies this backend's admission.
    // Each automatic pipeline owns a distinct layout, as required by validation.
    var entries: [MAX_STORAGE_BINDINGS]abi_pipeline.WGPUBindGroupLayoutEntry = undefined;
    for (storage, 0..) |slot, index| {
        entries[index] = std.mem.zeroes(abi_pipeline.WGPUBindGroupLayoutEntry);
        entries[index].binding = @intCast(index);
        entries[index].visibility = binding.WGPUShaderStage_Compute;
        entries[index].buffer.type = reflection.shaderBufferBindingType(shader, 0, @intCast(index));
        entries[index].buffer.minBindingSize = slot.minimum_size;
    }
    var descriptor = std.mem.zeroes(abi_pipeline.WGPUBindGroupLayoutDescriptor);
    descriptor.entryCount = storage.len;
    descriptor.entries = @ptrCast(&entries);
    const group = layouts.doeNativeDeviceCreateBindGroupLayout(helpers.toOpaque(device), &descriptor) orelse return error.AutomaticLayoutCreationFailed;
    defer layouts.doeNativeBindGroupLayoutRelease(group);
    (helpers.cast(objects.DoeBindGroupLayout, group) orelse unreachable).automatic = true;
    const raw = layouts.doeNativeDeviceCreatePipelineLayoutOne(helpers.toOpaque(device), group, 0) orelse return error.AutomaticLayoutCreationFailed;
    return helpers.cast(objects.DoePipelineLayout, raw) orelse unreachable;
}

/// Descriptors come from the pipeline's storage slots, so a group made from an
/// equivalent explicit layout binds the same table. Another pipeline's
/// automatic layout stays exclusive to that pipeline. The package recreates
/// automatic layouts explicitly from reflection, and its flat buffer-layout
/// path records only binding numbers: an undefined buffer type is unspecified.
fn sameBindings(actual: ?*objects.DoeBindGroupLayout, expected: ?*objects.DoeBindGroupLayout) bool {
    const have = actual orelse return false;
    const want = expected orelse return false;
    if (have == want) return true;
    if (have.automatic) return false;
    const have_entries = have.entries orelse return false;
    const want_entries = want.entries orelse return false;
    if (have_entries.len != want_entries.len) return false;
    for (want_entries) |entry| {
        var match = for (have_entries) |candidate| {
            if (candidate.binding == entry.binding) break candidate;
        } else return false;
        if (match.buffer_binding_type == binding.WGPUBufferBindingType_Undefined) match.buffer_binding_type = entry.buffer_binding_type;
        if (!std.meta.eql(match, entry)) return false;
    }
    return true;
}

pub fn validate(device: *objects.DoeDevice, dispatch: Dispatch) !void {
    const pipeline = helpers.cast(objects.DoeComputePipeline, dispatch.compute_pipeline) orelse return error.InvalidPipeline;
    if (pipeline.error_object or pipeline.d3d12_root_signature == null or pipeline.mtl_pso == null) return error.InvalidPipeline;
    const shader = pipeline.shader_module orelse return error.InvalidPipeline;
    if (shader.device != device) return error.DeviceMismatch;
    const layout = pipeline.layout orelse return error.InvalidPipeline;
    const group = helpers.cast(objects.DoeBindGroup, dispatch.bind_groups[0]) orelse return error.MissingBinding;
    const count = pipeline.binding_count;
    if (count == 0 or count > MAX_STORAGE_BINDINGS or !sameBindings(group.layout, layout.bind_group_layouts[0]) or group.count != count or dispatch.buf_count != count) return error.UnsupportedBinding;
    for (dispatch.bind_groups[1..]) |other| if (other != null) return error.UnsupportedBinding;
    if (dispatch.x > 65535 or dispatch.y != 1 or dispatch.z != 1) return error.UnsupportedDispatchDimensions;
    for (pipeline.d3d12_storage[0..count], 0..) |slot, index| {
        const buffer = group.retained_buffers[index] orelse return error.MissingBinding;
        if (buffer.dev != device or buffer.backend != .d3d12 or buffer.d3d12_heap_type != 1 or buffer.mtl == null or buffer.error_object or buffer.destroyed or buffer.mapped or buffer.map_pending) return error.InvalidBuffer;
        if ((buffer.usage & abi.WGPUBufferUsage_Storage) == 0 or dispatch.bufs[index] != buffer.mtl) return error.InvalidBuffer;
        // Aliased descriptors require a unified usage scope; reject before any recording.
        for (dispatch.bufs[0..index]) |other| if (other == buffer.mtl) return error.UnsupportedBinding;
        const size = dispatch.buf_sizes[index];
        const offset = dispatch.buf_offsets[index];
        if (offset % dc.MIN_STORAGE_BUFFER_OFFSET_ALIGNMENT != 0 or offset > buffer.size or
            size < slot.minimum_size or size > buffer.size - offset or size % 4 != 0 or
            size / 4 > std.math.maxInt(u32)) return error.UnsupportedBindingRange;
        // Runtime-array stores use the compiler's bound-view write guards.
        // Keep fixed-array admission bounded and validate any elided guards'
        // dispatch obligations below; padding is not permission to bypass proofs.
        if (!slot.runtime_array and @as(u64, dispatch.x) * pipeline.wg_x > slot.minimum_size / 4) return error.UnsupportedDispatchDimensions;
    }
    try preconditions.validate_bind_groups(pipeline.dispatch_preconditions, &.{}, &.{group}, .{ dispatch.x, dispatch.y, dispatch.z }, .{ pipeline.wg_x, 1, 1 });
}

pub fn record(device: *objects.DoeDevice, list: ?*anyopaque, dispatch: Dispatch, retained: *std.ArrayListUnmanaged(?*anyopaque)) !void {
    try validate(device, dispatch);
    if (dispatch.x == 0) return;
    const pipeline = helpers.cast(objects.DoeComputePipeline, dispatch.compute_pipeline).?;
    const count = pipeline.binding_count;
    try retained.ensureUnusedCapacity(alloc, count + 3);
    const heap = bridge.d3d12_bridge_device_create_cbv_srv_uav_heap(device.mtl_device, count) orelse return error.DescriptorHeapCreationFailed;
    // Publish retained native ownership before any GPU submission can occur.
    retained.appendAssumeCapacity(heap);
    for ([_]?*anyopaque{ pipeline.mtl_pso, pipeline.d3d12_root_signature }) |handle| {
        bridge.d3d12_bridge_retain(handle);
        retained.appendAssumeCapacity(handle);
    }
    for (pipeline.d3d12_storage[0..count], 0..) |slot, index| {
        const buffer = dispatch.bufs[index].?;
        bridge.d3d12_bridge_retain(buffer);
        retained.appendAssumeCapacity(buffer);
        const elements: u32 = @intCast(dispatch.buf_sizes[index] / 4);
        if (slot.read_only) {
            bridge.d3d12_bridge_device_create_srv_buffer_range(device.mtl_device, heap, @intCast(index), buffer, dispatch.buf_offsets[index] / 4, elements, 4);
        } else {
            bridge.d3d12_bridge_device_create_uav_buffer_range(device.mtl_device, heap, @intCast(index), buffer, dispatch.buf_offsets[index] / 4, elements, 4);
        }
    }
    bridge.d3d12_bridge_command_list_set_compute_root_signature(list, pipeline.d3d12_root_signature);
    bridge.d3d12_bridge_command_list_set_pipeline_state(list, pipeline.mtl_pso);
    bridge.d3d12_bridge_command_list_set_descriptor_heaps(list, heap, null);
    bridge.d3d12_bridge_command_list_set_compute_root_descriptor_table(list, 0, heap, 0);
    const repeats = @max(dispatch.repeat_count, 1);
    for (0..repeats) |_| {
        // COPY_DEST is the shared copy/compute boundary, also valid after COMMON decay.
        for (pipeline.d3d12_storage[0..count], 0..) |slot, index| {
            const state = if (slot.read_only) dc.RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE else dc.RESOURCE_STATE_UNORDERED_ACCESS;
            bridge.d3d12_bridge_command_list_resource_barrier_transition(list, dispatch.bufs[index], dc.RESOURCE_STATE_COPY_DEST, state);
        }
        bridge.d3d12_bridge_command_list_dispatch(list, dispatch.x, dispatch.y, dispatch.z);
        for (pipeline.d3d12_storage[0..count], 0..) |slot, index| {
            const state = if (slot.read_only) dc.RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE else dc.RESOURCE_STATE_UNORDERED_ACCESS;
            bridge.d3d12_bridge_command_list_resource_barrier_transition(list, dispatch.bufs[index], state, dc.RESOURCE_STATE_COPY_DEST);
        }
    }
}

fn selectedWorkgroup(module: *const ir.Module, name: []const u8) ![3]u32 {
    var found: ?[3]u32 = null;
    for (module.entry_points.items) |entry| {
        if (!std.mem.eql(u8, module.functions.items[entry.function].name, name)) continue;
        if (found != null or entry.stage != .compute) return error.UnsupportedEntryPoint;
        found = entry.workgroup_size;
    }
    const size = found orelse return error.UnsupportedEntryPoint;
    if (size[0] == 0 or size[0] > 1024 or size[1] != 1 or size[2] != 1) return error.UnsupportedWorkgroupSize;
    return size;
}

test "D3D12 selected compute entry owns workgroup and reachable bindings" {
    const source =
        \\@group(0) @binding(0) var<storage,read_write> selected_values: array<u32>;
        \\@group(1) @binding(7) var<storage,read> unused: array<u32>;
        \\fn helper() { selected_values[0] = 3u; }
        \\@compute @workgroup_size(8) fn transform() { helper(); }
        \\@compute @workgroup_size(1) fn other() { selected_values[0] = unused[0]; }
        \\@compute @workgroup_size(8,2) fn two_dimensional() {}
        \\@vertex fn vertex() -> @builtin(position) vec4f { return vec4f(0.0); }
    ;
    var diagnostic = analysis.Diagnostic{};
    var module = (try analysis.analyze(.{ .allocator = std.testing.allocator, .source = source, .robustness = analysis.default_translation_robustness_config(), .diagnostic = &diagnostic })).module;
    defer module.deinit();
    try std.testing.expectEqual([3]u32{ 8, 1, 1 }, try selectedWorkgroup(&module, "transform"));
    try std.testing.expectEqual([3]u32{ 1, 1, 1 }, try selectedWorkgroup(&module, "other"));
    try std.testing.expectError(error.UnsupportedEntryPoint, selectedWorkgroup(&module, "missing"));
    try std.testing.expectError(error.UnsupportedEntryPoint, selectedWorkgroup(&module, "helper"));
    try std.testing.expectError(error.UnsupportedEntryPoint, selectedWorkgroup(&module, "vertex"));
    try std.testing.expectError(error.UnsupportedWorkgroupSize, selectedWorkgroup(&module, "two_dimensional"));
    var reflected: [bindings.MAX_BINDINGS]bindings.BindingMeta = undefined;
    const count = try bindings.extractEntryPointBindings(std.testing.allocator, &module, "transform", &reflected);
    try std.testing.expectEqual(@as(usize, 1), count);
    try std.testing.expectEqual(@as(u32, 0), reflected[0].group);
    try std.testing.expectEqual(@as(u32, 0), reflected[0].binding);
    try module.entry_points.append(std.testing.allocator, module.entry_points.items[0]);
    try std.testing.expectError(error.UnsupportedEntryPoint, selectedWorkgroup(&module, "transform"));
}
