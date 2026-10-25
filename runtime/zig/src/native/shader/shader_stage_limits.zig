//! Compute pipeline device-limit checks from the shader's stage IR:
//! maxComputeWorkgroupStorageSize over the workgroup variables the entry point
//! statically uses (each var counts roundUp(16, SizeOf(T)),
//! https://gpuweb.github.io/gpuweb/#dom-gpudevice-createcomputepipeline), and,
//! for an automatic layout, the binding slot limits of the bindings it uses.

const std = @import("std");
const wgsl_analysis = @import("../../compiler/wgsl/pipeline/analysis.zig");
const wgsl_bindings = @import("../../compiler/wgsl/pipeline/binding_reflection.zig");
const wgsl_ir = @import("../../compiler/wgsl/ir/ir.zig");
const layout_utils = @import("../../compiler/wgsl/ir/layout_utils.zig");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");
const native_types = @import("../support/doe_native_object_types.zig");
const native_shared = @import("../support/doe_native_shared_types.zig");
const shader_binding_reflection = @import("shader_binding_reflection.zig");
const binding_slot_limits = @import("../resource/doe_binding_slot_limits.zig");
const shader_binding_types = @import("shader_binding_types.zig");
const compute_bind_groups = @import("../compute/doe_compute_bind_groups.zig");

/// Marks the globals `function_id` and its user callees reference.
fn markUsedGlobals(module_ir: *const wgsl_ir.Module, function_id: usize, visited: []bool, globals: []bool) void {
    if (visited[function_id]) return;
    visited[function_id] = true;
    for (module_ir.functions.items[function_id].exprs.items) |expr| switch (expr.data) {
        .global_ref => |global_id| globals[global_id] = true,
        .call => |call| if (call.kind == .user) {
            for (module_ir.functions.items, 0..) |candidate, candidate_id| {
                if (std.mem.eql(u8, candidate.name, call.name)) {
                    markUsedGlobals(module_ir, candidate_id, visited, globals);
                    break;
                }
            }
        },
        else => {},
    };
}

/// Bytes of workgroup storage the entry point at `function_id` uses.
pub fn workgroupStorageSize(allocator: std.mem.Allocator, module_ir: *const wgsl_ir.Module, function_id: usize) error{OutOfMemory}!u64 {
    const visited = try allocator.alloc(bool, module_ir.functions.items.len);
    defer allocator.free(visited);
    @memset(visited, false);
    const globals = try allocator.alloc(bool, module_ir.globals.items.len);
    defer allocator.free(globals);
    @memset(globals, false);
    markUsedGlobals(module_ir, function_id, visited, globals);
    var total: u64 = 0;
    for (module_ir.globals.items, globals) |global, used| {
        if (!used or global.addr_space != .workgroup) continue;
        total += std.mem.alignForward(u64, layout_utils.type_size(module_ir, global.ty), 16);
    }
    return total;
}

fn computeEntry(module_ir: *const wgsl_ir.Module, entry: ?[]const u8) ?usize {
    var only: ?usize = null;
    for (module_ir.functions.items, 0..) |function, id| {
        if (function.stage != .compute) continue;
        if (entry) |name| {
            if (std.mem.eql(u8, function.name, name)) return id;
        } else {
            if (only != null) return null;
            only = id;
        }
    }
    return only;
}

/// Whether `source` may declare a workgroup variable (`var<workgroup>`, with
/// optional template whitespace); false skips the stage IR analysis.
fn declaresWorkgroupVariable(source: []const u8) bool {
    var at: usize = 0;
    while (std.mem.indexOfPos(u8, source, at, "workgroup")) |found| {
        at = found + "workgroup".len;
        var next = at;
        while (next < source.len and std.ascii.isWhitespace(source[next])) next += 1;
        if (next < source.len and (source[next] == '>' or source[next] == ',')) return true;
    }
    return false;
}

fn slotFailure(device: *const native_types.DoeDevice, bindings: []const wgsl_bindings.BindingMeta) ?[]const u8 {
    for (bindings) |meta| if (meta.group >= device.shape_limits.bind_groups)
        return "compute pipeline automatic layout uses a bind group index not below maxBindGroups";
    var slots = binding_slot_limits.Counts{};
    for (bindings) |meta| {
        const required = shader_binding_reflection.requiredBinding(meta, @intCast(abi_binding.WGPUShaderStage_Compute));
        slots.addKind(required.stages, required.kind, required.buffer_type);
    }
    const limit = slots.failure(device.stage_limits) orelse return null;
    return binding_slot_limits.message("compute pipeline automatic layout exceeds ", limit);
}

/// The module's cached Vulkan reflection of the compute entry point's
/// bindings, selected as computeEntryBindingOverLimit selects it.
fn cachedEntryBindings(sm: *const native_types.DoeShaderModule, entry: ?[]const u8) ?[]const wgsl_bindings.BindingMeta {
    const entries = sm.vk_entry_point_bindings;
    if (entry) |name| {
        for (entries) |candidate| if (std.mem.eql(u8, candidate.name, name)) return candidate.bindings;
        return null;
    }
    return if (entries.len == 1) entries[0].bindings else null;
}

/// Whether the explicit `layout` admits the IR types of every binding the
/// entry point uses ("validating shader binding"); true when a binding's
/// type has no IR description.
fn explicitLayoutAdmits(allocator: std.mem.Allocator, module_ir: *const wgsl_ir.Module, function_id: usize, layout: *const native_types.DoePipelineLayout) bool {
    var metadata: [native_shared.MAX_SHADER_BINDINGS]wgsl_bindings.BindingMeta = undefined;
    const count = wgsl_bindings.extractEntryPointBindings(allocator, module_ir, module_ir.functions.items[function_id].name, &metadata) catch return true;
    var filtered = [_]bool{false} ** shader_binding_types.MAX_FILTERED_GLOBALS;
    shader_binding_types.markFilteredTextures(module_ir, @intCast(function_id), &filtered);
    var bindings = native_shared.RequiredBindings{};
    for (metadata[0..count]) |meta| {
        var use = shader_binding_reflection.requiredBinding(meta, @intCast(abi_binding.WGPUShaderStage_Compute));
        if (!shader_binding_types.describeDefaultLayout(module_ir, meta, &filtered, &use)) return true;
        bindings.record(use) catch return true;
    }
    return compute_bind_groups.pipelineLayoutAdmitsBindings(layout, bindings.slice());
}

/// The device limit the compute entry point exceeds, or the explicit layout
/// binding type it does not match, as a validation message, or null. An
/// automatic layout's bindings come from the module's cached Vulkan
/// reflection when it has one; the stage IR is analyzed for an explicit
/// layout, for workgroup variables or for uncached bindings. A module without
/// retained WGSL, or one the stage IR cannot analyze, is left to the
/// backend's own checks.
pub fn computeLimitFailure(
    allocator: std.mem.Allocator,
    device: *const native_types.DoeDevice,
    sm: *const native_types.DoeShaderModule,
    entry: ?[]const u8,
    overrides: []const wgsl_ir.OverrideEntry,
    layout: ?*const native_types.DoePipelineLayout,
) ?[]const u8 {
    const automatic_layout = layout == null;
    const cached = if (automatic_layout) cachedEntryBindings(sm, entry) else null;
    if (cached) |bindings| if (slotFailure(device, bindings)) |message| return message;
    const source = sm.wgsl_source orelse return null;
    const needs_bindings = automatic_layout and cached == null;
    if (!needs_bindings and layout == null and !declaresWorkgroupVariable(source)) return null;
    var module_ir = wgsl_analysis.analyzeToIrWithConfigAndOverrides(allocator, source, wgsl_analysis.default_translation_robustness_config(), overrides) catch return null;
    defer module_ir.deinit();
    const function_id = computeEntry(&module_ir, entry) orelse return null;
    const storage = workgroupStorageSize(allocator, &module_ir, function_id) catch return null;
    if (storage > device.max_compute_workgroup_storage_size)
        return "compute entry point workgroup storage exceeds maxComputeWorkgroupStorageSize";
    if (layout) |explicit| {
        if (!explicitLayoutAdmits(allocator, &module_ir, function_id, explicit))
            return "compute pipeline layout's binding types do not match the entry point's bindings";
        return null;
    }
    if (!needs_bindings) return null;
    var metadata: [native_shared.MAX_SHADER_BINDINGS]wgsl_bindings.BindingMeta = undefined;
    const count = wgsl_bindings.extractEntryPointBindings(allocator, &module_ir, module_ir.functions.items[function_id].name, &metadata) catch return null;
    return slotFailure(device, metadata[0..count]);
}

fn storageFor(source: []const u8, limit: u32) !?[]const u8 {
    var device = native_types.DoeDevice{};
    device.max_compute_workgroup_storage_size = limit;
    const sm = native_types.DoeShaderModule{ .wgsl_source = source };
    return computeLimitFailure(std.testing.allocator, &device, &sm, null, &.{}, null);
}

test "workgroup storage counts each used variable rounded up to 16 bytes" {
    const source =
        \\var<workgroup> a: array<vec3<f32>, 4>;
        \\var<workgroup> b: f32;
        \\var<workgroup> unused: array<f32, 4096>;
        \\fn helper() { b = 1.0; }
        \\@compute @workgroup_size(1) fn main() { a[0] = vec3<f32>(0.0); helper(); }
    ;
    // a: 4 * 16 bytes; b: 4 bytes rounded to 16; unused is not counted.
    try std.testing.expectEqual(@as(?[]const u8, null), try storageFor(source, 80));
    try std.testing.expect((try storageFor(source, 79)) != null);
}

test "an automatic compute layout counts the entry point's bindings per stage" {
    const source =
        \\@group(0) @binding(0) var<uniform> u0: vec4<f32>;
        \\@group(0) @binding(1) var<uniform> u1: vec4<f32>;
        \\@group(0) @binding(2) var<storage, read_write> o: array<f32>;
        \\@compute @workgroup_size(1) fn main() { o[0] = u0.x + u1.x; }
    ;
    var device = native_types.DoeDevice{};
    const sm = native_types.DoeShaderModule{ .wgsl_source = source };
    try std.testing.expectEqual(@as(?[]const u8, null), computeLimitFailure(std.testing.allocator, &device, &sm, "main", &.{}, null));
    device.stage_limits.uniform_buffers = 1;
    try std.testing.expectEqualStrings("compute pipeline automatic layout exceeds maxUniformBuffersPerShaderStage", computeLimitFailure(std.testing.allocator, &device, &sm, "main", &.{}, null).?);
    // An explicit layout is checked against the binding types instead; this
    // empty one lacks the bindings.
    var empty = native_types.DoePipelineLayout{};
    try std.testing.expect(computeLimitFailure(std.testing.allocator, &device, &sm, "main", &.{}, &empty) != null);
}

test "only a workgroup template argument marks a workgroup variable" {
    try std.testing.expect(!declaresWorkgroupVariable("@compute @workgroup_size(64) fn main() {}"));
    try std.testing.expect(declaresWorkgroupVariable("var< workgroup > a: u32;"));
    try std.testing.expect(declaresWorkgroupVariable("var<workgroup>a: u32;"));
}
