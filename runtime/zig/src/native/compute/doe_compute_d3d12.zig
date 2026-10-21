//! Bounded public D3D12 compute admission and recorded-command integration.
//! Dense group(0) scalar storage arrays with explicit or automatic layouts, entry main.
const std = @import("std");
const objects = @import("../support/doe_native_object_types.zig");
const helpers = @import("../support/doe_native_object_helpers.zig");
const commands = @import("../support/doe_native_command_types.zig");
const resources = @import("../../backend/dropin_resource_ops.zig");
const bridge = @import("../../backend/dropin_queue_submit.zig").d3d12_bridge.c;
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
    if (!std.mem.eql(u8, entry, "main")) return error.UnsupportedEntryPoint;
    const source = shader.wgsl_source orelse return error.UnsupportedShaderFormat;
    var module = (try analysis.analyze(.{ .allocator = alloc, .source = source, .robustness = analysis.default_translation_robustness_config(), .diagnostic = &diagnostic.compiler })).module;
    defer module.deinit();
    var count: u32 = 0;
    var storage: [MAX_STORAGE_BINDINGS]StorageBinding = @splat(.{});
    for (module.globals.items) |global| {
        const point = global.binding orelse continue;
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
    var workgroup: [3]u32 = .{ 0, 0, 0 };
    for (module.functions.items) |function| {
        if (function.stage != null) {
            if (function.stage != .compute or !std.mem.eql(u8, function.name, entry) or workgroup[0] != 0) return error.UnsupportedEntryPoint;
            workgroup = function.workgroup_size;
        }
    }
    if (workgroup[0] == 0 or workgroup[0] > 1024 or workgroup[1] != 1 or workgroup[2] != 1) return error.UnsupportedWorkgroupSize;
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
        if (item.buffer_binding_type != expected or item.buffer_has_dynamic_offset) return error.UnsupportedLayout;
        seen[item.binding] = true;
    }
    const text = try alloc.alloc(u8, hlsl.MAX_OUTPUT);
    defer alloc.free(text);
    // Emit the same analyzed module whose proof obligations we retain below.
    const length = try hlsl.emitter.emit(&module, text);
    const bytecode = try resources.d3d12_compute.compileHlslToBytecode(alloc, text[0..length]);
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
    const raw = layouts.doeNativeDeviceCreatePipelineLayoutOne(helpers.toOpaque(device), group, 0) orelse return error.AutomaticLayoutCreationFailed;
    return helpers.cast(objects.DoePipelineLayout, raw) orelse unreachable;
}

pub fn validate(device: *objects.DoeDevice, dispatch: Dispatch) !void {
    const pipeline = helpers.cast(objects.DoeComputePipeline, dispatch.compute_pipeline) orelse return error.InvalidPipeline;
    if (pipeline.error_object or pipeline.d3d12_root_signature == null or pipeline.mtl_pso == null) return error.InvalidPipeline;
    const shader = pipeline.shader_module orelse return error.InvalidPipeline;
    if (shader.device != device) return error.DeviceMismatch;
    const layout = pipeline.layout orelse return error.InvalidPipeline;
    const group = helpers.cast(objects.DoeBindGroup, dispatch.bind_groups[0]) orelse return error.MissingBinding;
    const count = pipeline.binding_count;
    if (count == 0 or count > MAX_STORAGE_BINDINGS or group.layout != layout.bind_group_layouts[0] or group.count != count or dispatch.buf_count != count) return error.UnsupportedBinding;
    for (dispatch.bind_groups[1..]) |other| if (other != null) return error.UnsupportedBinding;
    if (dispatch.x > 65535 or dispatch.y != 1 or dispatch.z != 1) return error.UnsupportedDispatchDimensions;
    for (pipeline.d3d12_storage[0..count], 0..) |slot, index| {
        const buffer = group.retained_buffers[index] orelse return error.MissingBinding;
        if (buffer.dev != device or buffer.backend != .d3d12 or buffer.d3d12_heap_type != 1 or buffer.mtl == null or buffer.error_object or buffer.destroyed or buffer.mapped or buffer.map_pending) return error.InvalidBuffer;
        if ((buffer.usage & abi.WGPUBufferUsage_Storage) == 0 or dispatch.bufs[index] != buffer.mtl) return error.InvalidBuffer;
        // Aliased descriptors require a unified usage scope; reject before any recording.
        for (dispatch.bufs[0..index]) |other| if (other == buffer.mtl) return error.UnsupportedBinding;
        const size = dispatch.buf_sizes[index];
        if (dispatch.buf_offsets[index] != 0 or size < slot.minimum_size or size > buffer.size or size % 4 != 0 or size / 4 > std.math.maxInt(u32)) return error.UnsupportedBindingRange;
        const extent = if (slot.runtime_array) size else slot.minimum_size;
        if (@as(u64, dispatch.x) * pipeline.wg_x > extent / 4) return error.UnsupportedDispatchDimensions;
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
            bridge.d3d12_bridge_device_create_srv_buffer(device.mtl_device, heap, @intCast(index), buffer, elements, 4);
        } else {
            bridge.d3d12_bridge_device_create_uav_buffer(device.mtl_device, heap, @intCast(index), buffer, elements, 4);
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
