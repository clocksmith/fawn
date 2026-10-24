const std = @import("std");
const abi_core = @import("../../core/abi/wgpu_core_base_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const native_shared = @import("../support/doe_native_shared_types.zig");
const wgsl_analysis = @import("../../compiler/wgsl/pipeline/analysis.zig");
const runtime_compile = @import("../../compiler/wgsl/runtime/runtime_graphics_translation.zig");
const shared = @import("vulkan_render_shared.zig");
const error_scope = @import("../../runtime/diagnostics/error_scope.zig");
const wgsl_bindings = @import("../../compiler/wgsl/pipeline/binding_reflection.zig");
const wgsl_ir = @import("../../compiler/wgsl/ir/ir.zig");
const shader_binding_reflection = @import("../shader/shader_binding_reflection.zig");
const compute_bind_groups = @import("../compute/doe_compute_bind_groups.zig");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");
const texture_contract = @import("../../contracts/texture.zig");
const vertex_format = @import("../../contracts/vertex_format.zig");
const ir_query = @import("../../compiler/wgsl/ir/ir_query.zig");
const abi_texture_dims = @import("../../core/abi/wgpu_texture_base_types.zig");
const texture_contract_values = @import("../../contracts/model/model_texture_value_types.zig");
const binding_slot_limits = @import("../resource/doe_binding_slot_limits.zig");

/// webgpu.h WGPUPrimitiveTopology_PointList.
const PRIMITIVE_TOPOLOGY_POINT_LIST: u32 = 0x00000001;

pub fn vulkan_create_render_pipeline(
    dev: *shared.DoeDevice,
    pip: *shared.DoeRenderPipeline,
    desc: *const anyopaque,
) bool {
    const RenderStringView = extern struct {
        data: ?[*]const u8,
        length: usize,
    };
    const RenderBlendComponent = extern struct {
        operation: u32,
        srcFactor: u32,
        dstFactor: u32,
    };
    const RenderBlendState = extern struct {
        color: RenderBlendComponent,
        alpha: RenderBlendComponent,
    };
    const RenderColorTargetState = extern struct {
        nextInChain: ?*anyopaque,
        format: u32,
        blend: ?*const RenderBlendState,
        writeMask: u64,
    };
    const RenderFragmentState = extern struct {
        nextInChain: ?*anyopaque,
        module: ?*anyopaque,
        entryPoint: RenderStringView,
        constantCount: usize,
        constants: ?*anyopaque,
        targetCount: usize,
        targets: ?[*]const RenderColorTargetState,
    };
    const RenderPrimitiveState = extern struct {
        nextInChain: ?*anyopaque,
        topology: u32,
        stripIndexFormat: u32,
        frontFace: u32,
        cullMode: u32,
        unclippedDepth: u32,
    };
    const RenderVertexAttribute = extern struct {
        nextInChain: ?*anyopaque,
        format: u32,
        offset: u64,
        shaderLocation: u32,
    };
    const RenderVertexBufferLayout = extern struct {
        nextInChain: ?*anyopaque,
        stepMode: u32,
        arrayStride: u64,
        attributeCount: usize,
        attributes: ?[*]const RenderVertexAttribute,
    };
    const RenderStencilFaceState = extern struct {
        compare: u32,
        failOp: u32,
        depthFailOp: u32,
        passOp: u32,
    };
    const RenderDepthStencilDesc = extern struct {
        nextInChain: ?*anyopaque,
        format: u32,
        depthWriteEnabled: u32,
        depthCompare: u32,
        stencilFront: RenderStencilFaceState,
        stencilBack: RenderStencilFaceState,
        stencilReadMask: u32,
        stencilWriteMask: u32,
        depthBias: i32,
        depthBiasSlopeScale: f32,
        depthBiasClamp: f32,
    };
    const LocalDesc = extern struct {
        nextInChain: ?*anyopaque,
        label_data: ?[*]const u8,
        label_length: usize,
        layout: ?*anyopaque,
        vertex_nextInChain: ?*anyopaque,
        vertex_module: ?*anyopaque,
        vertex_ep_data: ?[*]const u8,
        vertex_ep_length: usize,
        vertex_constantCount: usize,
        vertex_constants: ?*anyopaque,
        vertex_bufferCount: usize,
        vertex_buffers: ?*anyopaque,
        primitive: RenderPrimitiveState,
        depthStencil: ?*anyopaque,
        multisample_nextInChain: ?*anyopaque,
        multisample_count: u32,
        multisample_mask: u32,
        multisample_alphaToCoverageEnabled: u32,
        fragment: ?*const RenderFragmentState,
    };
    const d = @as(*const LocalDesc, @ptrCast(@alignCast(desc)));
    pip.topology = d.primitive.topology;
    pip.front_face = d.primitive.frontFace;
    pip.cull_mode = d.primitive.cullMode;
    pip.unclipped_depth = d.primitive.unclippedDepth != 0;
    pip.sample_count = if (d.multisample_count == 0) 1 else d.multisample_count;
    pip.sample_mask = d.multisample_mask;
    pip.alpha_to_coverage = d.multisample_alphaToCoverageEnabled != 0;
    pip.vertex_buffer_count = 0;
    pip.vertex_attribute_count = 0;
    if (d.vertex_bufferCount > 0 and d.vertex_buffers != null) {
        const buffer_count = @min(d.vertex_bufferCount, native_shared.MAX_VERTEX_BUFFERS);
        const buffers = @as([*]const RenderVertexBufferLayout, @ptrCast(@alignCast(d.vertex_buffers)));
        var buffer_index: usize = 0;
        while (buffer_index < buffer_count) : (buffer_index += 1) {
            const layout = buffers[buffer_index];
            pip.vertex_buffer_strides[buffer_index] = layout.arrayStride;
            pip.vertex_step_modes[buffer_index] = layout.stepMode;
            pip.vertex_buffer_count += 1;
            if (layout.attributes) |attrs| {
                const available = native_shared.MAX_VERTEX_ATTRIBUTES - pip.vertex_attribute_count;
                const attr_count = @min(layout.attributeCount, available);
                var attr_index: usize = 0;
                while (attr_index < attr_count) : (attr_index += 1) {
                    const dst = pip.vertex_attribute_count;
                    const attr = attrs[attr_index];
                    pip.vertex_attribute_formats[dst] = attr.format;
                    pip.vertex_attribute_offsets[dst] = attr.offset;
                    pip.vertex_attribute_locations[dst] = attr.shaderLocation;
                    pip.vertex_attribute_buffer_slots[dst] = @intCast(buffer_index);
                    pip.vertex_attribute_count += 1;
                }
            }
        }
    }

    if (d.depthStencil) |ds_raw| {
        const ds = @as(*const RenderDepthStencilDesc, @ptrCast(@alignCast(ds_raw)));
        pip.depth_stencil_format = ds.format;
        pip.depth_compare = ds.depthCompare;
        pip.depth_write_enabled = ds.depthWriteEnabled != 0;
        pip.stencil_front_compare = ds.stencilFront.compare;
        pip.stencil_front_fail_op = ds.stencilFront.failOp;
        pip.stencil_front_depth_fail_op = ds.stencilFront.depthFailOp;
        pip.stencil_front_pass_op = ds.stencilFront.passOp;
        pip.stencil_back_compare = ds.stencilBack.compare;
        pip.stencil_back_fail_op = ds.stencilBack.failOp;
        pip.stencil_back_depth_fail_op = ds.stencilBack.depthFailOp;
        pip.stencil_back_pass_op = ds.stencilBack.passOp;
        pip.stencil_read_mask = ds.stencilReadMask;
        pip.stencil_write_mask = ds.stencilWriteMask;
    }

    pip.has_fragment_stage = d.fragment != null;
    if (d.fragment) |frag| {
        pip.color_target_count = frag.targetCount;
        if (frag.targets) |targets| for (targets[0..@min(frag.targetCount, pip.color_targets.len)], 0..) |target, index| {
            const slot = &pip.color_targets[index];
            slot.* = .{ .format = target.format, .write_mask = @intCast(target.writeMask) };
            if (target.blend) |blend| {
                slot.blend_enabled = true;
                slot.color_operation = blend.color.operation;
                slot.color_src_factor = blend.color.srcFactor;
                slot.color_dst_factor = blend.color.dstFactor;
                slot.alpha_operation = blend.alpha.operation;
                slot.alpha_src_factor = blend.alpha.srcFactor;
                slot.alpha_dst_factor = blend.alpha.dstFactor;
            }
        };
        if (frag.targetCount > 0 and frag.targets != null) {
            const target0 = frag.targets.?[0];
            pip.color_target_format = target0.format;
            pip.color_write_mask = @intCast(target0.writeMask);
            if (target0.blend) |blend| {
                pip.blend_enabled = true;
                pip.color_operation = blend.color.operation;
                pip.color_src_factor = blend.color.srcFactor;
                pip.color_dst_factor = blend.color.dstFactor;
                pip.alpha_operation = blend.alpha.operation;
                pip.alpha_src_factor = blend.alpha.srcFactor;
                pip.alpha_dst_factor = blend.alpha.dstFactor;
            }
        }
    }

    const vertex = native_helpers.cast(shared.DoeShaderModule, d.vertex_module);
    const fragment = if (d.fragment) |frag| native_helpers.cast(shared.DoeShaderModule, frag.module) else null;
    if (d.fragment != null and fragment == null) {
        dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, "render pipeline requires a valid fragment shader");
        return false;
    }
    const vertex_entry = entryPoint(d.vertex_ep_data, d.vertex_ep_length);
    const fragment_entry = if (d.fragment) |frag| entryPoint(frag.entryPoint.data, frag.entryPoint.length) else null;
    var interface = StageInterface{};
    switch (admitStageEntryPoints(pip, vertex, vertex_entry, fragment, fragment_entry, &interface)) {
        .admitted => if (pip.required_binding_kinds_ready) if (fragmentInterfaceFailure(pip, &interface) orelse vertexInterfaceFailure(pip, &interface)) |message| {
            dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, message);
            return false;
        },
        .missing => {
            dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, "render pipeline entry point does not name a vertex or fragment entry point of its shader module");
            return false;
        },
        .binding_over_limit => {
            dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, "render pipeline entry point uses a binding number not below maxBindingsPerBindGroup (" ++ std.fmt.comptimePrint("{d}", .{native_shared.MAX_BINDINGS_PER_BIND_GROUP}) ++ ")");
            return false;
        },
    }
    if (pip.required_binding_kinds_ready) if (interface.interStageFailure(dev.max_inter_stage_shader_variables, pip.topology == PRIMITIVE_TOPOLOGY_POINT_LIST, d.fragment != null)) |message| {
        dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, message);
        return false;
    };
    // An automatic layout holds exactly the bindings the entry points use.
    if (pip.layout == null and pip.required_binding_kinds_ready) {
        var slots = binding_slot_limits.Counts{};
        slots.addRequired(pip.required_bindings.slice());
        if (slots.failure(dev.stage_limits)) |limit| {
            dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, binding_slot_limits.message("render pipeline automatic layout exceeds ", limit));
            return false;
        }
    }
    if (pip.layout) |layout| if (pip.required_binding_kinds_ready and !compute_bind_groups.pipelineLayoutAdmitsBindings(layout, pip.required_bindings.slice())) {
        dev.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, "render pipeline layout lacks a binding the entry points use, or has it with another resource or buffer type or without the stage's visibility");
        return false;
    };
    retainShaderCode(native_helpers.alloc, pip, vertex, fragment, vertex_entry, fragment_entry) catch |err| {
        dev.error_scopes.deliver(error_scope.zig_error_to_type(err), if (err == error.OutOfMemory) "render pipeline could not retain shader code" else "render pipeline requires compiled graphics shader code");
        return false;
    };

    pip.mtl_pso = null;
    return true;
}

/// The scalar kind and component count of a stage interface value, from the
/// shader's IR (bool and composite values are `other`).
const InterfaceValue = struct {
    location: u32,
    kind: Kind,
    components: u32,

    const Kind = enum { float, sint, uint, other };

    fn of(module_ir: *const wgsl_ir.Module, location: u32, ty: wgsl_ir.TypeId) InterfaceValue {
        return switch (module_ir.types.get(ty)) {
            .scalar => |scalar| .{ .location = location, .kind = scalarKind(scalar), .components = 1 },
            .vector => |vector| .{ .location = location, .kind = switch (module_ir.types.get(vector.elem)) {
                .scalar => |scalar| scalarKind(scalar),
                else => .other,
            }, .components = vector.len },
            else => .{ .location = location, .kind = .other, .components = 0 },
        };
    }

    fn scalarKind(scalar: wgsl_ir.ScalarType) Kind {
        return switch (scalar) {
            .f32, .f16, .abstract_float => .float,
            .i32, .abstract_int => .sint,
            .u32 => .uint,
            .void, .bool => .other,
        };
    }
};

/// The vertex inputs and fragment outputs with @location, for the
/// createRenderPipeline rules that compare them with the descriptor.
const StageInterface = struct {
    const CAPACITY = 32;
    vertex_inputs: [CAPACITY]InterfaceValue = undefined,
    vertex_count: usize = 0,
    fragment_outputs: [CAPACITY]InterfaceValue = undefined,
    fragment_count: usize = 0,
    /// Inter-stage variables: user-defined vertex outputs (with the largest
    /// location and the clip_distances array size) and fragment inputs (with
    /// the inter-stage builtins the fragment stage reads).
    vertex_outputs: u32 = 0,
    vertex_output_location_end: u32 = 0,
    clip_distances: u32 = 0,
    fragment_inputs: u32 = 0,
    fragment_input_builtins: u32 = 0,

    fn recordVertexOutputs(self: *StageInterface, module_ir: *const wgsl_ir.Module, ty: wgsl_ir.TypeId, io: ?wgsl_ir.IoAttr) void {
        switch (module_ir.types.get(ty)) {
            .struct_ => |id| for (module_ir.structs.items[id].fields.items) |field| self.recordVertexOutputs(module_ir, field.ty, field.io),
            else => if (io) |attr| {
                if (attr.location) |location| {
                    self.vertex_outputs += 1;
                    self.vertex_output_location_end = @max(self.vertex_output_location_end, location + 1);
                } else if (attr.builtin == .clip_distances) switch (module_ir.types.get(ty)) {
                    .array => |array| self.clip_distances = array.len orelse 0,
                    else => {},
                };
            },
        }
    }

    fn recordFragmentInputs(self: *StageInterface, module_ir: *const wgsl_ir.Module, ty: wgsl_ir.TypeId, io: ?wgsl_ir.IoAttr) void {
        switch (module_ir.types.get(ty)) {
            .struct_ => |id| for (module_ir.structs.items[id].fields.items) |field| self.recordFragmentInputs(module_ir, field.ty, field.io),
            else => if (io) |attr| {
                if (attr.location != null) {
                    self.fragment_inputs += 1;
                } else switch (attr.builtin) {
                    // https://gpuweb.github.io/gpuweb/#inter-stage-builtins
                    .front_facing, .sample_index, .sample_mask, .primitive_index, .subgroup_invocation_id, .subgroup_size => self.fragment_input_builtins += 1,
                    else => {},
                }
            },
        }
    }

    /// "validating inter-stage interfaces" limits
    /// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-inter-stage-interfaces).
    fn interStageFailure(self: *const StageInterface, limit: u32, point_list: bool, has_fragment: bool) ?[]const u8 {
        const clip_slots = (self.clip_distances + 3) / 4;
        const output_budget = limit -| (@as(u32, @intFromBool(point_list)) + clip_slots);
        if (self.vertex_outputs > output_budget or self.vertex_output_location_end > limit -| clip_slots)
            return "render pipeline vertex outputs exceed maxInterStageShaderVariables";
        if (has_fragment and self.fragment_inputs > limit -| self.fragment_input_builtins)
            return "render pipeline fragment inputs exceed maxInterStageShaderVariables";
        return null;
    }

    fn recordVertexInput(self: *StageInterface, module_ir: *const wgsl_ir.Module, ty: wgsl_ir.TypeId, io: ?wgsl_ir.IoAttr) void {
        switch (module_ir.types.get(ty)) {
            .struct_ => |id| for (module_ir.structs.items[id].fields.items) |field| self.recordVertexInput(module_ir, field.ty, field.io),
            else => if (io) |attr| if (attr.location) |location| if (self.vertex_count < CAPACITY) {
                self.vertex_inputs[self.vertex_count] = InterfaceValue.of(module_ir, location, ty);
                self.vertex_count += 1;
            },
        }
    }

    fn recordFragmentOutputs(self: *StageInterface, module_ir: *const wgsl_ir.Module, ty: wgsl_ir.TypeId, io: ?wgsl_ir.IoAttr) void {
        switch (module_ir.types.get(ty)) {
            .struct_ => |id| for (module_ir.structs.items[id].fields.items) |field| self.recordFragmentOutputs(module_ir, field.ty, field.io),
            else => if (io) |attr| if (attr.location) |location| if ((attr.blend_src orelse 0) == 0 and self.fragment_count < CAPACITY) {
                self.fragment_outputs[self.fragment_count] = InterfaceValue.of(module_ir, location, ty);
                self.fragment_count += 1;
            },
        }
    }

    fn find(values: []const InterfaceValue, location: u32) ?InterfaceValue {
        for (values) |value| if (value.location == location) return value;
        return null;
    }
};

/// webgpu.h blend factors that read the source alpha.
fn readsSourceAlpha(factor: u32) bool {
    return switch (factor) {
        0x00000005, 0x00000006, 0x0000000B, 0x00000010, 0x00000011 => true, // src-alpha, one-minus-src-alpha, src-alpha-saturated, src1-alpha, one-minus-src1-alpha
        else => false,
    };
}

fn vertexFormatKind(code: u32) ?InterfaceValue.Kind {
    const format = vertex_format.fromCode(code) catch return null;
    return switch (format) {
        .uint8, .uint8x2, .uint8x4, .uint16, .uint16x2, .uint16x4, .uint32, .uint32x2, .uint32x3, .uint32x4 => .uint,
        .sint8, .sint8x2, .sint8x4, .sint16, .sint16x2, .sint16x4, .sint32, .sint32x2, .sint32x3, .sint32x4 => .sint,
        .unorm8,
        .unorm8x2,
        .unorm8x4,
        .snorm8,
        .snorm8x2,
        .snorm8x4,
        .unorm16,
        .unorm16x2,
        .unorm16x4,
        .snorm16,
        .snorm16x2,
        .snorm16x4,
        .float16,
        .float16x2,
        .float16x4,
        .float32,
        .float32x2,
        .float32x3,
        .float32x4,
        .unorm10_10_10_2,
        .unorm8x4_bgra,
        => .float,
    };
}

/// Fragment outputs against color targets
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpufragmentstate):
/// a color target with an output at its location needs an output of the
/// format's scalar type with at least the format's components (four when its
/// color blend reads the source alpha); without one its writeMask is 0.
fn fragmentInterfaceFailure(pip: *const shared.DoeRenderPipeline, interface: *const StageInterface) ?[]const u8 {
    if (!pip.has_fragment_stage) return null;
    for (pip.color_targets[0..@min(pip.color_target_count, pip.color_targets.len)], 0..) |target, index| {
        if (target.format == 0) continue;
        const output = StageInterface.find(interface.fragment_outputs[0..interface.fragment_count], @intCast(index)) orelse {
            if (target.write_mask != 0) return "a color target without a fragment output at its location must have writeMask 0";
            continue;
        };
        const kind: InterfaceValue.Kind = switch (texture_contract.inferSampleType(target.format)) {
            abi_binding.WGPUTextureSampleType_Uint => .uint,
            abi_binding.WGPUTextureSampleType_Sint => .sint,
            else => .float,
        };
        if (output.kind != kind) return "a fragment output's scalar type does not match its color target format";
        const components = texture_contract.colorComponentCount(target.format) orelse continue;
        if (output.components < components) return "a fragment output has fewer components than its color target format";
        if (target.blend_enabled and (readsSourceAlpha(target.color_src_factor) or readsSourceAlpha(target.color_dst_factor)) and output.components != 4)
            return "a color blend that reads the source alpha needs a four-component fragment output";
    }
    return null;
}

/// Vertex attributes against shader inputs
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpuvertexstate):
/// an attribute the shader reads has a format of the input's scalar type.
fn vertexInterfaceFailure(pip: *const shared.DoeRenderPipeline, interface: *const StageInterface) ?[]const u8 {
    for (0..pip.vertex_attribute_count) |index| {
        const input = StageInterface.find(interface.vertex_inputs[0..interface.vertex_count], pip.vertex_attribute_locations[index]) orelse continue;
        const kind = vertexFormatKind(pip.vertex_attribute_formats[index]) orelse continue;
        if (input.kind != kind) return "a vertex attribute's format does not match the scalar type of its shader input";
    }
    return null;
}

const MAX_FILTERED_GLOBALS = 256;

/// Builtins that sample a texture through a (non-comparison) sampler.
fn filteringBuiltin(name: []const u8) bool {
    const names = [_][]const u8{ "textureSample", "textureSampleBias", "textureSampleGrad", "textureSampleLevel", "textureGather", "textureSampleBaseClampToEdge" };
    for (names) |candidate| if (std.mem.eql(u8, candidate, name)) return true;
    return false;
}

/// Marks the texture globals that the entry point (and the functions it
/// calls) samples through a sampler; the default layout makes those f32
/// textures "float" and the others "unfilterable-float".
fn markFilteredTextures(module_ir: *const wgsl_ir.Module, entry: wgsl_ir.FunctionId, filtered: *[MAX_FILTERED_GLOBALS]bool) void {
    var visited = [_]bool{false} ** 256;
    var pending: [256]wgsl_ir.FunctionId = undefined;
    var pending_len: usize = 0;
    if (entry >= visited.len) return;
    visited[entry] = true;
    pending[0] = entry;
    pending_len = 1;
    while (pending_len > 0) {
        pending_len -= 1;
        const function = &module_ir.functions.items[pending[pending_len]];
        for (function.exprs.items) |expr| switch (expr.data) {
            .call => |call| {
                const args = function.expr_args.items[call.args.start..][0..call.args.len];
                if (call.kind == .builtin and filteringBuiltin(call.name)) {
                    for (args) |arg| if (ir_query.findGlobalBase(function, arg)) |global| {
                        if (global < filtered.len) filtered[global] = true;
                    };
                } else if (call.kind == .user) {
                    for (module_ir.functions.items, 0..) |candidate, index| {
                        if (!std.mem.eql(u8, candidate.name, call.name)) continue;
                        if (index < visited.len and !visited[index] and pending_len < pending.len) {
                            visited[index] = true;
                            pending[pending_len] = @intCast(index);
                            pending_len += 1;
                        }
                    }
                }
            },
            else => {},
        };
    }
}

fn scalarSampleType(module_ir: *const wgsl_ir.Module, ty: wgsl_ir.TypeId, filtered: bool) u32 {
    return switch (module_ir.types.get(ty)) {
        .scalar => |scalar| switch (scalar) {
            .i32 => abi_binding.WGPUTextureSampleType_Sint,
            .u32 => abi_binding.WGPUTextureSampleType_Uint,
            else => if (filtered) abi_binding.WGPUTextureSampleType_Float else abi_binding.WGPUTextureSampleType_UnfilterableFloat,
        },
        else => abi_binding.WGPUTextureSampleType_UnfilterableFloat,
    };
}

fn storageFormat(format: wgsl_ir.TextureFormat) u32 {
    return switch (format) {
        .rgba8unorm => texture_contract_values.WGPUTextureFormat_RGBA8Unorm,
        .rgba8snorm => texture_contract_values.WGPUTextureFormat_RGBA8Snorm,
        .rgba8uint => texture_contract_values.WGPUTextureFormat_RGBA8Uint,
        .rgba8sint => texture_contract_values.WGPUTextureFormat_RGBA8Sint,
        .rgba16uint => texture_contract_values.WGPUTextureFormat_RGBA16Uint,
        .rgba16sint => texture_contract_values.WGPUTextureFormat_RGBA16Sint,
        .rgba16float => texture_contract_values.WGPUTextureFormat_RGBA16Float,
        .r32uint => texture_contract_values.WGPUTextureFormat_R32Uint,
        .r32sint => texture_contract_values.WGPUTextureFormat_R32Sint,
        .r32float => texture_contract_values.WGPUTextureFormat_R32Float,
        .rg32uint => texture_contract_values.WGPUTextureFormat_RG32Uint,
        .rg32sint => texture_contract_values.WGPUTextureFormat_RG32Sint,
        .rg32float => texture_contract_values.WGPUTextureFormat_RG32Float,
        .rgba32uint => texture_contract_values.WGPUTextureFormat_RGBA32Uint,
        .rgba32sint => texture_contract_values.WGPUTextureFormat_RGBA32Sint,
        .rgba32float => texture_contract_values.WGPUTextureFormat_RGBA32Float,
    };
}

/// The default bind group layout members for a binding, from its global's
/// IR type (https://gpuweb.github.io/gpuweb/#default-pipeline-layout).
/// Returns false when the binding has no IR global to describe it.
fn describeDefaultLayout(module_ir: *const wgsl_ir.Module, meta: wgsl_bindings.BindingMeta, filtered: *const [MAX_FILTERED_GLOBALS]bool, use: *native_shared.RequiredBinding) bool {
    use.min_binding_size = meta.min_binding_size;
    const index = for (module_ir.globals.items, 0..) |global, i| {
        const point = global.binding orelse continue;
        if (point.group == meta.group and point.binding == meta.binding) break i;
    } else return false;
    const is_filtered = index < filtered.len and filtered[index];
    const T = abi_texture_dims;
    switch (module_ir.types.get(module_ir.globals.items[index].ty)) {
        .sampler => use.sampler_type = abi_binding.WGPUSamplerBindingType_Filtering,
        .sampler_comparison => use.sampler_type = abi_binding.WGPUSamplerBindingType_Comparison,
        .texture_1d => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_1D;
        },
        .texture_2d => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_2D;
        },
        .texture_2d_array => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_2DArray;
        },
        .texture_cube => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_Cube;
        },
        .texture_3d => |sample| {
            use.sample_type = scalarSampleType(module_ir, sample, is_filtered);
            use.view_dimension = T.WGPUTextureViewDimension_3D;
        },
        .texture_multisampled_2d => |sample| {
            // Multisampled textures are never filtered.
            use.sample_type = scalarSampleType(module_ir, sample, false);
            use.view_dimension = T.WGPUTextureViewDimension_2D;
            use.multisampled = true;
        },
        .texture_depth_2d => {
            use.sample_type = abi_binding.WGPUTextureSampleType_Depth;
            use.view_dimension = T.WGPUTextureViewDimension_2D;
        },
        .texture_depth_cube => {
            use.sample_type = abi_binding.WGPUTextureSampleType_Depth;
            use.view_dimension = T.WGPUTextureViewDimension_Cube;
        },
        .storage_texture_2d => |storage| {
            use.storage_format = storageFormat(storage.format);
            use.storage_access = switch (storage.access) {
                .read => abi_binding.WGPUStorageTextureAccess_ReadOnly,
                .write => abi_binding.WGPUStorageTextureAccess_WriteOnly,
                .read_write => abi_binding.WGPUStorageTextureAccess_ReadWrite,
            };
            use.view_dimension = T.WGPUTextureViewDimension_2D;
        },
        else => {},
    }
    return true;
}

const StageEntryPoint = enum { checked, unchecked, missing, binding_over_limit };
const StageAdmission = enum { admitted, missing, binding_over_limit };

/// Each stage's entry point must name a function of that stage in its module;
/// an omitted name needs exactly one such function
/// (https://gpuweb.github.io/gpuweb/#abstract-opdef-validating-gpuprogrammablestage).
/// Also records the layout resource kind of every binding the entry points
/// use, and rejects a used binding number at or above maxBindingsPerBindGroup.
/// A module without WGSL source is not checked, and draws then skip the
/// automatic-layout check.
fn admitStageEntryPoints(
    pipeline: *shared.DoeRenderPipeline,
    vertex: ?*const shared.DoeShaderModule,
    vertex_entry: ?[]const u8,
    fragment: ?*const shared.DoeShaderModule,
    fragment_entry: ?[]const u8,
    interface: *StageInterface,
) StageAdmission {
    pipeline.required_bindings = .{};
    pipeline.required_binding_kinds_ready = false;
    var checked = true;
    switch (stageEntryPoint(pipeline, vertex, vertex_entry, .vertex, interface)) {
        .missing => return .missing,
        .binding_over_limit => return .binding_over_limit,
        .unchecked => checked = false,
        .checked => {},
    }
    if (fragment != null) switch (stageEntryPoint(pipeline, fragment, fragment_entry, .fragment, interface)) {
        .missing => return .missing,
        .binding_over_limit => return .binding_over_limit,
        .unchecked => checked = false,
        .checked => {},
    };
    pipeline.required_binding_kinds_ready = checked;
    return .admitted;
}

fn stageEntryPoint(pipeline: *shared.DoeRenderPipeline, module: ?*const shared.DoeShaderModule, entry: ?[]const u8, stage: wgsl_ir.ShaderStage, interface: *StageInterface) StageEntryPoint {
    const wgsl = (module orelse return .unchecked).wgsl_source orelse return .unchecked;
    var module_ir = wgsl_analysis.analyzeToIr(native_helpers.alloc, wgsl) catch return .unchecked;
    defer module_ir.deinit();
    const name = entry orelse blk: {
        var only: ?[]const u8 = null;
        for (module_ir.functions.items) |function| {
            if (function.stage != stage) continue;
            if (only != null) return .missing;
            only = function.name;
        }
        break :blk only orelse return .missing;
    };
    const function = for (module_ir.functions.items) |candidate| {
        if (std.mem.eql(u8, candidate.name, name)) break candidate;
    } else return .missing;
    if (function.stage != stage) return .missing;
    switch (stage) {
        .vertex => {
            for (function.params.items) |param| interface.recordVertexInput(&module_ir, param.ty, param.io);
            interface.recordVertexOutputs(&module_ir, function.return_type, function.return_io);
        },
        .fragment => {
            interface.recordFragmentOutputs(&module_ir, function.return_type, function.return_io);
            for (function.params.items) |param| interface.recordFragmentInputs(&module_ir, param.ty, param.io);
        },
        .compute => {},
    }
    var metadata: [native_shared.MAX_SHADER_BINDINGS]wgsl_bindings.BindingMeta = undefined;
    const count = wgsl_bindings.extractEntryPointBindings(native_helpers.alloc, &module_ir, name, &metadata) catch return .unchecked;
    if (shader_binding_reflection.bindingOverLimit(metadata[0..count]) != null) return .binding_over_limit;
    const stage_bit: u8 = switch (stage) {
        .vertex => @intCast(abi_binding.WGPUShaderStage_Vertex),
        .fragment => @intCast(abi_binding.WGPUShaderStage_Fragment),
        .compute => @intCast(abi_binding.WGPUShaderStage_Compute),
    };
    const function_id = for (module_ir.functions.items, 0..) |candidate, index| {
        if (std.mem.eql(u8, candidate.name, name)) break index;
    } else return .missing;
    var filtered = [_]bool{false} ** MAX_FILTERED_GLOBALS;
    markFilteredTextures(&module_ir, @intCast(function_id), &filtered);
    for (metadata[0..count]) |meta| {
        var use = shader_binding_reflection.requiredBinding(meta, stage_bit);
        // A binding the IR cannot describe leaves the default layout to the caller.
        if (!describeDefaultLayout(&module_ir, meta, &filtered, &use)) return .unchecked;
        pipeline.required_bindings.record(use) catch return .unchecked;
    }
    return .checked;
}

fn entryPoint(data: ?[*]const u8, length: usize) ?[]const u8 {
    const bytes = data orelse return null;
    const len = if (length == abi_core.WGPU_STRLEN) std.mem.len(@as([*:0]const u8, @ptrCast(bytes))) else length;
    return if (len == 0) null else bytes[0..len];
}

fn retainShaderCode(
    allocator: std.mem.Allocator,
    pipeline: *shared.DoeRenderPipeline,
    vertex: ?*const shared.DoeShaderModule,
    fragment: ?*const shared.DoeShaderModule,
    vertex_entry: ?[]const u8,
    fragment_entry: ?[]const u8,
) !void {
    const vertex_module = vertex orelse return error.ShaderCompileFailed;
    const vertex_source = vertex_module.vertex_spirv_data orelse return error.ShaderCompileFailed;
    const fragment_source = if (fragment) |module| module.fragment_spirv_data orelse return error.ShaderCompileFailed else null;
    const vs = try allocator.dupe(u32, vertex_source);
    errdefer allocator.free(vs);
    const fs = if (fragment_source) |source| try allocator.dupe(u32, source) else null;
    errdefer if (fs) |source| allocator.free(source);
    const ve = if (vertex_entry) |name| try allocator.dupe(u8, name) else null;
    errdefer if (ve) |name| allocator.free(name);
    const fe = if (fragment_entry) |name| try allocator.dupe(u8, name) else null;
    pipeline.vertex_spirv_data = vs;
    pipeline.fragment_spirv_data = fs;
    pipeline.vertex_entry_point = ve;
    pipeline.fragment_entry_point = fe;
    if (vertex_module.wgsl_source) |source| {
        std.crypto.hash.sha2.Sha256.hash(source, &pipeline.vertex_wgsl_sha256, .{});
        pipeline.vertex_wgsl_sha256_ready = true;
    }
    if (fragment) |module| if (module.wgsl_source) |source| {
        std.crypto.hash.sha2.Sha256.hash(source, &pipeline.fragment_wgsl_sha256, .{});
        pipeline.fragment_wgsl_sha256_ready = true;
    };
}

fn shaderCodeAllocationFailures(allocator: std.mem.Allocator) !void {
    var pipeline = shared.DoeRenderPipeline{};
    const vertex = shared.DoeShaderModule{ .vertex_spirv_data = &.{ 1, 2 }, .wgsl_source = "vertex" };
    const fragment = shared.DoeShaderModule{ .fragment_spirv_data = &.{ 3, 4 }, .wgsl_source = "fragment" };
    retainShaderCode(allocator, &pipeline, &vertex, &fragment, "vertex", "fragment") catch |err| {
        try std.testing.expectEqual(@as(?[]const u32, null), pipeline.vertex_spirv_data);
        try std.testing.expectEqual(@as(?[]const u32, null), pipeline.fragment_spirv_data);
        try std.testing.expectEqual(@as(?[]const u8, null), pipeline.vertex_entry_point);
        try std.testing.expectEqual(@as(?[]const u8, null), pipeline.fragment_entry_point);
        try std.testing.expect(!pipeline.vertex_wgsl_sha256_ready);
        return err;
    };
    defer allocator.free(pipeline.vertex_spirv_data.?);
    defer allocator.free(pipeline.fragment_spirv_data.?);
    defer allocator.free(pipeline.vertex_entry_point.?);
    defer allocator.free(pipeline.fragment_entry_point.?);
    try std.testing.expectEqualSlices(u32, vertex.vertex_spirv_data.?, pipeline.vertex_spirv_data.?);
    try std.testing.expect(pipeline.vertex_spirv_data.?.ptr != vertex.vertex_spirv_data.?.ptr);
    try std.testing.expectEqualStrings("fragment", pipeline.fragment_entry_point.?);
}

test "render shader retention publishes only after every allocation succeeds" {
    try std.testing.checkAllAllocationFailures(std.testing.allocator, shaderCodeAllocationFailures, .{});
}

test "missing graphics code is a shader failure rather than an empty pipeline" {
    var pipeline = shared.DoeRenderPipeline{};
    const missing = shared.DoeShaderModule{};
    try std.testing.expectError(error.ShaderCompileFailed, retainShaderCode(std.testing.allocator, &pipeline, &missing, null, null, null));
    const vertex = shared.DoeShaderModule{ .vertex_spirv_data = &.{1} };
    try std.testing.expectError(error.ShaderCompileFailed, retainShaderCode(std.testing.allocator, &pipeline, &vertex, &missing, null, null));
    try std.testing.expectEqual(@as(?[]const u32, null), pipeline.vertex_spirv_data);
}

pub fn probe_has_graphics_entry_points(wgsl: []const u8) bool {
    return std.mem.indexOf(u8, wgsl, "@vertex") != null or
        std.mem.indexOf(u8, wgsl, "@fragment") != null;
}

pub fn vulkan_create_graphics_shader_moduleWithDiagnostic(sm: *shared.DoeShaderModule, wgsl: []const u8, diagnostic: *wgsl_analysis.Diagnostic) wgsl_analysis.TranslateError!void {
    const alloc = native_helpers.alloc;
    var result = try runtime_compile.translateToSpirvForGraphicsRuntimeWithDiagnostic(alloc, wgsl, diagnostic);
    defer result.deinit(alloc);
    sm.vertex_spirv_data = result.vertex_spirv;
    sm.fragment_spirv_data = result.fragment_spirv;

    sm.wg_x = 0;
    sm.wg_y = 0;
    sm.wg_z = 0;
    sm.needs_sizes_buf = false;

    result.vertex_spirv = null;
    result.fragment_spirv = null;
}

pub fn vulkan_create_graphics_shader_module(sm: *shared.DoeShaderModule, wgsl: []const u8) wgsl_analysis.TranslateError!void {
    return vulkan_create_graphics_shader_moduleWithDiagnostic(sm, wgsl, wgsl_analysis.compatibilityDiagnostic());
}

test "fragment outputs match their color targets' scalar type and components" {
    var pip = shared.DoeRenderPipeline{};
    pip.has_fragment_stage = true;
    pip.color_target_count = 1;
    pip.color_targets[0] = .{ .format = texture_contract_values.WGPUTextureFormat_RG8Unorm, .write_mask = 0xF };
    var interface = StageInterface{};
    try std.testing.expect(fragmentInterfaceFailure(&pip, &interface) != null); // no output, writeMask 0xF
    pip.color_targets[0].write_mask = 0;
    try std.testing.expectEqual(@as(?[]const u8, null), fragmentInterfaceFailure(&pip, &interface));
    pip.color_targets[0].write_mask = 0xF;
    interface.fragment_outputs[0] = .{ .location = 0, .kind = .float, .components = 1 };
    interface.fragment_count = 1;
    try std.testing.expect(fragmentInterfaceFailure(&pip, &interface) != null); // fewer components
    interface.fragment_outputs[0].components = 2;
    try std.testing.expectEqual(@as(?[]const u8, null), fragmentInterfaceFailure(&pip, &interface));
    interface.fragment_outputs[0].kind = .uint;
    try std.testing.expect(fragmentInterfaceFailure(&pip, &interface) != null); // scalar type
    interface.fragment_outputs[0].kind = .float;
    pip.color_targets[0].blend_enabled = true;
    pip.color_targets[0].color_src_factor = 0x00000005; // src-alpha
    try std.testing.expect(fragmentInterfaceFailure(&pip, &interface) != null);
    interface.fragment_outputs[0].components = 4;
    try std.testing.expectEqual(@as(?[]const u8, null), fragmentInterfaceFailure(&pip, &interface));
}

test "vertex attribute formats match the scalar type of their shader inputs" {
    var pip = shared.DoeRenderPipeline{};
    pip.vertex_attribute_count = 1;
    pip.vertex_attribute_locations[0] = 3;
    pip.vertex_attribute_formats[0] = @intFromEnum(vertex_format.Format.uint8x2);
    var interface = StageInterface{};
    interface.vertex_inputs[0] = .{ .location = 3, .kind = .uint, .components = 2 };
    interface.vertex_count = 1;
    try std.testing.expectEqual(@as(?[]const u8, null), vertexInterfaceFailure(&pip, &interface));
    interface.vertex_inputs[0].kind = .sint;
    try std.testing.expect(vertexInterfaceFailure(&pip, &interface) != null);
    pip.vertex_attribute_formats[0] = @intFromEnum(vertex_format.Format.unorm8x2);
    interface.vertex_inputs[0].kind = .float;
    try std.testing.expectEqual(@as(?[]const u8, null), vertexInterfaceFailure(&pip, &interface));
    interface.vertex_inputs[0].location = 4; // not read by the shader
    pip.vertex_attribute_formats[0] = @intFromEnum(vertex_format.Format.sint32);
    try std.testing.expectEqual(@as(?[]const u8, null), vertexInterfaceFailure(&pip, &interface));
}
