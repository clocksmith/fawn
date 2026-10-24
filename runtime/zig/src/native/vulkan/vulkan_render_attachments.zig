const std = @import("std");
const contract = @import("../../contracts/command_recording.zig");
const abi = @import("../../core/abi/wgpu_pipeline_descriptor_types.zig");
const texture_abi = @import("../../core/abi/wgpu_texture_base_types.zig");
const objects = @import("../support/doe_native_object_types.zig");
const helpers = @import("../support/doe_native_object_helpers.zig");

const Error = contract.RenderAttachmentError;
const model_render_types = @import("../../contracts/model/model_render_types.zig");
const vertex_formats = @import("../../contracts/vertex_format.zig");
const values = @import("../../contracts/model/model_texture_value_types.zig");
const feature_abi = @import("../../core/abi/wgpu_feature_base_types.zig");
const Extent = struct { width: u32, height: u32 };

fn tier1Enabled(device: ?*const objects.DoeDevice) bool {
    const owner = device orelse return false;
    for (owner.enabled_features) |feature| {
        if (feature == feature_abi.WGPUFeatureName_TextureFormatsTier1) return true;
    }
    return false;
}

fn validateView(device: *objects.DoeDevice, raw: ?*anyopaque, depth: bool) Error!*objects.DoeTextureView {
    const view = helpers.cast(objects.DoeTextureView, raw) orelse return error.RenderAttachmentUnavailable;
    const texture = view.tex;
    if (texture.isUnavailable()) return error.RenderAttachmentUnavailable;
    if (texture.device_ref != device) return error.RenderAttachmentDeviceMismatch;
    if ((view.usage & texture_abi.WGPUTextureUsage_RenderAttachment) == 0) return error.RenderAttachmentUsageMissing;
    // A renderable view is 2d, or 2d-array with one layer (the default view of
    // one layer of a layered texture); the layer count is checked below.
    if (view.dimension != texture_abi.WGPUTextureViewDimension_2D and
        view.dimension != texture_abi.WGPUTextureViewDimension_2DArray) return error.RenderAttachmentUnsupported;
    if (view.mip_level_count != 1 or view.array_layer_count != 1 or
        view.aspect != texture_abi.WGPUTextureAspect_All or
        view.base_mip_level >= texture.mip_level_count or view.base_mip_level >= @bitSizeOf(u32))
        return error.RenderAttachmentViewInvalid;
    if (texture_abi.isDepthStencilFormat(view.format) != depth) return error.RenderAttachmentFormatMismatch;
    return view;
}

fn extent(view: *const objects.DoeTextureView) Extent {
    return .{
        .width = @max(1, view.tex.width >> @intCast(view.base_mip_level)),
        .height = @max(1, view.tex.height >> @intCast(view.base_mip_level)),
    };
}

fn validatePair(color: *const objects.DoeTextureView, depth: *const objects.DoeTextureView) Error!void {
    if (!std.meta.eql(extent(color), extent(depth))) return error.RenderAttachmentExtentMismatch;
    if (color.tex.sample_count != depth.tex.sample_count) return error.RenderAttachmentSampleMismatch;
}

/// Formats with the "resolve" capability in the WebGPU texture format table
/// (https://gpuweb.github.io/gpuweb/#plain-color-formats); integer and 32-bit
/// float formats cannot resolve. texture-formats-tier1 adds the 8-bit snorm
/// formats (https://gpuweb.github.io/gpuweb/#texture-formats-tier1); its 16-bit
/// unorm/snorm formats render and multisample but do not resolve.
fn resolvable(format: u32, tier1: bool) bool {
    return switch (format) {
        values.WGPUTextureFormat_R8Snorm,
        values.WGPUTextureFormat_RG8Snorm,
        values.WGPUTextureFormat_RGBA8Snorm,
        => tier1,
        texture_abi.WGPUTextureFormat_R8Unorm,
        texture_abi.WGPUTextureFormat_RG8Unorm,
        texture_abi.WGPUTextureFormat_RGBA8Unorm,
        texture_abi.WGPUTextureFormat_RGBA8UnormSrgb,
        texture_abi.WGPUTextureFormat_BGRA8Unorm,
        texture_abi.WGPUTextureFormat_BGRA8UnormSrgb,
        texture_abi.WGPUTextureFormat_R16Float,
        texture_abi.WGPUTextureFormat_RG16Float,
        texture_abi.WGPUTextureFormat_RGBA16Float,
        texture_abi.WGPUTextureFormat_RGB10A2Unorm,
        texture_abi.WGPUTextureFormat_RG11B10Ufloat,
        => true,
        else => false,
    };
}

/// A resolve target is a single-sample view of the multisampled color
/// attachment's resolvable format and extent.
fn validateResolve(color: *const objects.DoeTextureView, resolve: *const objects.DoeTextureView) Error!void {
    if (color.tex.sample_count <= 1 or resolve.tex.sample_count > 1) return error.RenderAttachmentSampleMismatch;
    if (color.format != resolve.format or !resolvable(resolve.format, tier1Enabled(resolve.tex.device_ref)))
        return error.RenderAttachmentFormatMismatch;
    if (!std.meta.eql(extent(color), extent(resolve))) return error.RenderAttachmentExtentMismatch;
}

/// maxColorAttachmentBytesPerSample every Vulkan device reports
/// (vk_device_caps.zig: the WebGPU minimum, not raisable).
const MAX_COLOR_ATTACHMENT_BYTES_PER_SAMPLE: u32 = 32;

const TargetCost = struct { bytes: u32, alignment: u32 };

/// Render target pixel byte cost and component alignment
/// (https://gpuweb.github.io/gpuweb/#plain-color-formats and the
/// texture-formats-tier1 table); null for a format that is not a color target.
fn targetCost(format: u32) ?TargetCost {
    return switch (format) {
        texture_abi.WGPUTextureFormat_R8Unorm,
        texture_abi.WGPUTextureFormat_R8Uint,
        texture_abi.WGPUTextureFormat_R8Sint,
        values.WGPUTextureFormat_R8Snorm,
        => .{ .bytes = 1, .alignment = 1 },
        texture_abi.WGPUTextureFormat_RG8Unorm,
        texture_abi.WGPUTextureFormat_RG8Uint,
        texture_abi.WGPUTextureFormat_RG8Sint,
        values.WGPUTextureFormat_RG8Snorm,
        => .{ .bytes = 2, .alignment = 1 },
        texture_abi.WGPUTextureFormat_RGBA8Uint,
        texture_abi.WGPUTextureFormat_RGBA8Sint,
        values.WGPUTextureFormat_RGBA8Snorm,
        => .{ .bytes = 4, .alignment = 1 },
        texture_abi.WGPUTextureFormat_RGBA8Unorm,
        texture_abi.WGPUTextureFormat_RGBA8UnormSrgb,
        texture_abi.WGPUTextureFormat_BGRA8Unorm,
        texture_abi.WGPUTextureFormat_BGRA8UnormSrgb,
        => .{ .bytes = 8, .alignment = 1 },
        texture_abi.WGPUTextureFormat_R16Uint,
        texture_abi.WGPUTextureFormat_R16Sint,
        texture_abi.WGPUTextureFormat_R16Float,
        values.WGPUTextureFormat_R16Unorm,
        values.WGPUTextureFormat_R16Snorm,
        => .{ .bytes = 2, .alignment = 2 },
        texture_abi.WGPUTextureFormat_RG16Uint,
        texture_abi.WGPUTextureFormat_RG16Sint,
        texture_abi.WGPUTextureFormat_RG16Float,
        values.WGPUTextureFormat_RG16Unorm,
        values.WGPUTextureFormat_RG16Snorm,
        => .{ .bytes = 4, .alignment = 2 },
        texture_abi.WGPUTextureFormat_RGBA16Uint,
        texture_abi.WGPUTextureFormat_RGBA16Sint,
        texture_abi.WGPUTextureFormat_RGBA16Float,
        values.WGPUTextureFormat_RGBA16Unorm,
        values.WGPUTextureFormat_RGBA16Snorm,
        => .{ .bytes = 8, .alignment = 2 },
        texture_abi.WGPUTextureFormat_R32Uint,
        texture_abi.WGPUTextureFormat_R32Sint,
        texture_abi.WGPUTextureFormat_R32Float,
        => .{ .bytes = 4, .alignment = 4 },
        texture_abi.WGPUTextureFormat_RG32Uint,
        texture_abi.WGPUTextureFormat_RG32Sint,
        texture_abi.WGPUTextureFormat_RG32Float,
        texture_abi.WGPUTextureFormat_RGB10A2Uint,
        texture_abi.WGPUTextureFormat_RGB10A2Unorm,
        texture_abi.WGPUTextureFormat_RG11B10Ufloat,
        => .{ .bytes = 8, .alignment = 4 },
        texture_abi.WGPUTextureFormat_RGBA32Uint,
        texture_abi.WGPUTextureFormat_RGBA32Sint,
        texture_abi.WGPUTextureFormat_RGBA32Float,
        => .{ .bytes = 16, .alignment = 4 },
        else => null,
    };
}

/// https://gpuweb.github.io/gpuweb/#abstract-opdef-calculating-color-attachment-bytes-per-sample
fn colorBytesPerSample(formats: []const u32) Error!u32 {
    var total: u32 = 0;
    for (formats) |format| {
        if (format == 0) continue;
        const cost = targetCost(format) orelse return error.RenderAttachmentFormatMismatch;
        total = std.mem.alignForward(u32, total, cost.alignment) + cost.bytes;
    }
    return total;
}

/// Every attachment of a pass shares the first one's extent and sample count.
fn admitAttachment(reference: *?*const objects.DoeTextureView, view: *const objects.DoeTextureView) Error!void {
    if (reference.*) |first| try validatePair(first, view) else reference.* = view;
}

/// Admit native attachment semantics before retaining views or recording work:
/// up to MAX_COLOR_ATTACHMENTS color slots (null slots allowed) and an optional
/// depth/stencil attachment, with at least one attachment in all.
pub fn validateDescriptor(device: *objects.DoeDevice, descriptor: ?*const abi.WGPURenderPassDescriptor) Error!void {
    const desc = descriptor orelse return error.RenderAttachmentUnavailable;
    if (desc.colorAttachmentCount > model_render_types.MAX_COLOR_ATTACHMENTS) return error.RenderAttachmentUnsupported;
    var reference: ?*const objects.DoeTextureView = null;
    var formats = [_]u32{0} ** model_render_types.MAX_COLOR_ATTACHMENTS;
    if (desc.colorAttachmentCount > 0) {
        const colors = desc.colorAttachments orelse return error.RenderAttachmentUnavailable;
        for (colors[0..desc.colorAttachmentCount], 0..) |attachment, index| {
            if (attachment.view == null) {
                if (attachment.resolveTarget != null) return error.RenderAttachmentUnavailable;
                continue;
            }
            const color = try validateView(device, attachment.view, false);
            try admitAttachment(&reference, color);
            if (attachment.resolveTarget) |raw| try validateResolve(color, try validateView(device, raw, false));
            formats[index] = color.format;
        }
    }
    if (try colorBytesPerSample(formats[0..desc.colorAttachmentCount]) > MAX_COLOR_ATTACHMENT_BYTES_PER_SAMPLE)
        return error.RenderAttachmentColorBytesExceeded;
    if (desc.depthStencilAttachment) |raw| {
        const attachment: *const abi.WGPURenderPassDepthStencilAttachment = @ptrCast(@alignCast(raw));
        try admitAttachment(&reference, try validateView(device, attachment.view, true));
    }
    if (reference == null) return error.RenderAttachmentUnavailable;
}

/// Pass views have encoder-owned leases. Recheck availability at each draw;
/// queue-wide lease validation separately rejects destruction after recording.
/// A draw's pipeline matches the pass slot by slot (null slots included), in
/// depth/stencil format and in sample count.
pub fn validateDraw(pass: *const objects.DoeRenderPass, clear_only: bool) Error!void {
    var reference: ?*const objects.DoeTextureView = null;
    const count = @min(pass.color_attachment_count, model_render_types.MAX_COLOR_ATTACHMENTS);
    for (pass.color_attachments[0..count]) |slot| {
        if (slot.view_handle == 0) continue;
        const color = try validateView(pass.enc.dev, @ptrFromInt(slot.view_handle), false);
        try admitAttachment(&reference, color);
        if (slot.resolve_view_handle != 0)
            try validateResolve(color, try validateView(pass.enc.dev, @ptrFromInt(slot.resolve_view_handle), false));
    }
    const depth = if (pass.depth_target_view_handle != 0)
        try validateView(pass.enc.dev, @ptrFromInt(pass.depth_target_view_handle), true)
    else
        null;
    if (depth) |view| try admitAttachment(&reference, view);
    const first = reference orelse return error.RenderAttachmentUnavailable;
    if (clear_only) return;
    const pipeline = pass.pipeline orelse return error.RenderAttachmentUnavailable;
    if (pipeline.device_ref != pass.enc.dev) return error.RenderAttachmentDeviceMismatch;
    const pipeline_count = @min(pipeline.color_target_count, model_render_types.MAX_COLOR_ATTACHMENTS);
    for (0..@max(count, pipeline_count)) |index| {
        const pass_format = if (index < count) pass.color_attachments[index].format else 0;
        const pipeline_format = if (index < pipeline_count) pipeline.color_targets[index].format else 0;
        if (pass_format != pipeline_format) return error.RenderAttachmentFormatMismatch;
    }
    if (pipeline.depth_stencil_format != (if (depth) |view| view.format else @as(u32, 0)))
        return error.RenderAttachmentFormatMismatch;
    if (pipeline.sample_count != first.tex.sample_count) return error.RenderAttachmentSampleMismatch;
}

/// The draw parameters WebGPU validates against bound buffer ranges;
/// indirect draws carry none (their bounds are not CPU-validated).
pub const Draw = union(enum) {
    direct: struct { vertex_count: u32, instance_count: u32, first_vertex: u32, first_instance: u32 },
    indexed: struct { index_count: u32, instance_count: u32, first_index: u32, first_instance: u32 },
    indirect,
};

fn boundSize(pass: *const objects.DoeRenderPass, slot: usize, buffer: *const objects.DoeBuffer) u64 {
    const size = pass.vertex_buffer_sizes[slot];
    return if (size == std.math.maxInt(u64)) buffer.size -| pass.vertex_buffer_offsets[slot] else size;
}

/// https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-draw: every
/// pipeline vertex slot has a bound buffer; a direct draw reads
/// (strideCount - 1) * arrayStride + lastStride bytes of each vertex-step
/// buffer (vertex count) and each instance-step buffer (instance count);
/// an indexed draw checks instance-step buffers and its index range.
pub fn validateDrawBounds(pass: *const objects.DoeRenderPass, draw: Draw) contract.DrawError!void {
    const pipeline = pass.pipeline orelse return;
    const count = @min(pipeline.vertex_layout_count, model_render_types.MAX_VERTEX_BUFFERS);
    for (pipeline.vertex_layouts[0..count], 0..) |layout, slot| {
        if (layout.step_mode == model_render_types.WGPUVertexStepMode_Undefined and layout.attribute_count == 0) continue;
        const buffer = pass.vertex_buffers[slot] orelse return error.DrawVertexBufferMissing;
        const instance = layout.step_mode == model_render_types.WGPUVertexStepMode_Instance;
        const stride_count: u64 = switch (draw) {
            .direct => |d| if (instance) @as(u64, d.first_instance) + d.instance_count else @as(u64, d.first_vertex) + d.vertex_count,
            .indexed => |d| if (instance) @as(u64, d.first_instance) + d.instance_count else continue,
            .indirect => continue,
        };
        if (stride_count == 0) continue;
        var last_stride: u64 = 0;
        for (layout.attributes[0..@min(layout.attribute_count, model_render_types.MAX_VERTEX_ATTRIBUTES)]) |attribute| {
            const format = vertex_formats.fromCode(attribute.format) catch continue;
            last_stride = @max(last_stride, attribute.offset +| vertex_formats.byteSize(format));
        }
        const required = (stride_count - 1) *| layout.array_stride +| last_stride;
        if (required > boundSize(pass, slot, buffer)) return error.DrawVertexBufferOutOfBounds;
    }
    switch (draw) {
        .indexed => |d| {
            if (pass.index_buffer == null) return error.DrawIndexBufferMissing;
            const index_bytes: u64 = if (pass.index_format == model_render_types.WGPUIndexFormat_Uint32) 4 else 2;
            const size = if (pass.index_buffer_size == std.math.maxInt(u64)) pass.index_buffer.?.size -| pass.index_offset else pass.index_buffer_size;
            if (@as(u64, d.first_index) + d.index_count > size / index_bytes) return error.DrawIndexBufferOutOfBounds;
        },
        else => {},
    }
}

test "draws read within bound vertex and index buffer ranges" {
    var device = objects.DoeDevice{};
    var encoder = objects.DoeCommandEncoder{ .dev = &device };
    var pipeline = objects.DoeRenderPipeline{ .device_ref = &device, .vertex_layout_count = 2 };
    pipeline.vertex_layouts[1] = .{ .array_stride = 8, .step_mode = model_render_types.WGPUVertexStepMode_Vertex, .attribute_count = 1 };
    pipeline.vertex_layouts[1].attributes[0] = .{ .format = @intFromEnum(vertex_formats.Format.float32), .offset = 4 };
    pipeline.vertex_layouts[0] = .{ .step_mode = model_render_types.WGPUVertexStepMode_Undefined };
    var buffer = objects.DoeBuffer{ .size = 64 };
    var pass = objects.DoeRenderPass{ .enc = &encoder, .pipeline = &pipeline };
    const draw = Draw{ .direct = .{ .vertex_count = 3, .instance_count = 1, .first_vertex = 0, .first_instance = 0 } };
    try std.testing.expectError(error.DrawVertexBufferMissing, validateDrawBounds(&pass, draw));
    pass.vertex_buffers[1] = &buffer;
    pass.vertex_buffer_sizes[1] = 2 * 8 + 8;
    try validateDrawBounds(&pass, draw);
    pass.vertex_buffer_sizes[1] = 2 * 8 + 7;
    try std.testing.expectError(error.DrawVertexBufferOutOfBounds, validateDrawBounds(&pass, draw));
    // Indexed draws do not bound vertex-step buffers, but do bound indices.
    const indexed = Draw{ .indexed = .{ .index_count = 4, .instance_count = 1, .first_index = 1, .first_instance = 0 } };
    try std.testing.expectError(error.DrawIndexBufferMissing, validateDrawBounds(&pass, indexed));
    pass.index_buffer = &buffer;
    pass.index_format = model_render_types.WGPUIndexFormat_Uint16;
    pass.index_buffer_size = 10;
    try validateDrawBounds(&pass, indexed);
    pass.index_buffer_size = 9;
    try std.testing.expectError(error.DrawIndexBufferOutOfBounds, validateDrawBounds(&pass, indexed));
}

test "attachment admission compares selected mip extent and preserves format and sample identities" {
    var device = objects.DoeDevice{};
    var color_texture = objects.DoeTexture{ .device_ref = &device, .width = 16, .height = 16, .mip_level_count = 2 };
    var depth_texture = objects.DoeTexture{ .device_ref = &device, .width = 8, .height = 8 };
    var color = objects.DoeTextureView{
        .tex = &color_texture,
        .format = texture_abi.WGPUTextureFormat_RGBA8Unorm,
        .dimension = texture_abi.WGPUTextureViewDimension_2D,
        .base_mip_level = 1,
        .mip_level_count = 1,
        .array_layer_count = 1,
        .aspect = texture_abi.WGPUTextureAspect_All,
        .usage = texture_abi.WGPUTextureUsage_RenderAttachment,
    };
    var depth = color;
    depth.tex = &depth_texture;
    depth.base_mip_level = 0;
    depth.format = texture_abi.WGPUTextureFormat_Depth32Float;
    try validatePair(&color, &depth);
    depth_texture.width = 16;
    try std.testing.expectError(error.RenderAttachmentExtentMismatch, validatePair(&color, &depth));
    depth_texture.width = 8;
    depth_texture.sample_count = 4;
    try std.testing.expectError(error.RenderAttachmentSampleMismatch, validatePair(&color, &depth));
    depth_texture.sample_count = 1;
    var encoder = objects.DoeCommandEncoder{ .dev = &device };
    var pipeline = objects.DoeRenderPipeline{
        .device_ref = &device,
        .color_target_count = 1,
        .color_target_format = color.format,
        .depth_stencil_format = depth.format,
    };
    pipeline.color_targets[0].format = color.format;
    var pass = objects.DoeRenderPass{
        .enc = &encoder,
        .pipeline = &pipeline,
        .target_view_handle = @intFromPtr(&color),
        .depth_target_view_handle = @intFromPtr(&depth),
        .color_attachment_count = 1,
    };
    pass.color_attachments[0] = .{ .view_handle = @intFromPtr(&color), .format = color.format };
    try validateDraw(&pass, false);
    pipeline.sample_count = 4;
    try std.testing.expectError(error.RenderAttachmentSampleMismatch, validateDraw(&pass, false));
    pipeline.sample_count = 1;
    pipeline.color_targets[0].format = texture_abi.WGPUTextureFormat_BGRA8Unorm;
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateDraw(&pass, false));
    pipeline.color_targets[0].format = color.format;
    pipeline.depth_stencil_format = 0;
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateDraw(&pass, false));
    pipeline.depth_stencil_format = depth.format;
    // A null slot must be null in both the pass and the pipeline.
    pass.color_attachment_count = 3;
    pass.color_attachments[2] = .{ .view_handle = @intFromPtr(&color), .format = color.format };
    pipeline.color_target_count = 3;
    pipeline.color_targets[2].format = color.format;
    try validateDraw(&pass, false);
    pipeline.color_targets[1].format = color.format;
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateDraw(&pass, false));
    pipeline.color_targets[1].format = 0;
    // Depth-only: no color slots, a pipeline without targets.
    pass.color_attachment_count = 0;
    pipeline.color_target_count = 0;
    try validateDraw(&pass, false);
    pass.depth_target_view_handle = 0;
    try std.testing.expectError(error.RenderAttachmentUnavailable, validateDraw(&pass, true));
    pass.depth_target_view_handle = @intFromPtr(&depth);
    pass.color_attachment_count = 1;
    pipeline.color_target_count = 1;
    try validateDraw(&pass, true);
    color_texture.destroyed = true;
    try std.testing.expectError(error.RenderAttachmentUnavailable, validateDraw(&pass, true));
}

test "color attachment bytes per sample follow the WebGPU byte cost and alignment" {
    const r8 = texture_abi.WGPUTextureFormat_R8Unorm;
    const r32f = texture_abi.WGPUTextureFormat_R32Float;
    const rgba8 = texture_abi.WGPUTextureFormat_RGBA8Unorm;
    const rgba32f = texture_abi.WGPUTextureFormat_RGBA32Float;
    // r8 (1) -> align 4 + r32float (4) = 8, + rgba8 (8) = 16, align 4 + rgba32float (16) = 32, + r8 = 33.
    try std.testing.expectEqual(@as(u32, 33), try colorBytesPerSample(&.{ r8, r32f, rgba8, rgba32f, r8 }));
    try std.testing.expectEqual(@as(u32, 30), try colorBytesPerSample(&.{ r32f, rgba8, rgba32f, r8, r8 }));
    try std.testing.expectEqual(@as(u32, 9), try colorBytesPerSample(&.{ r8, 0, rgba8 }));
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, colorBytesPerSample(&.{texture_abi.WGPUTextureFormat_Depth32Float}));
}

test "attachment admission rejects unavailable foreign nonrenderable and broad views" {
    var device = objects.DoeDevice{};
    var other = objects.DoeDevice{};
    var texture = objects.DoeTexture{ .device_ref = &device, .width = 8, .height = 8 };
    var view = objects.DoeTextureView{
        .tex = &texture,
        .format = texture_abi.WGPUTextureFormat_RGBA8Unorm,
        .dimension = texture_abi.WGPUTextureViewDimension_2D,
        .mip_level_count = 1,
        .array_layer_count = 1,
        .aspect = texture_abi.WGPUTextureAspect_All,
        .usage = texture_abi.WGPUTextureUsage_RenderAttachment,
    };
    try std.testing.expectEqual(&view, try validateView(&device, &view, false));
    try std.testing.expectError(error.RenderAttachmentUnavailable, validateView(&device, null, false));
    try std.testing.expectError(error.RenderAttachmentDeviceMismatch, validateView(&other, &view, false));
    view.usage = texture_abi.WGPUTextureUsage_TextureBinding;
    try std.testing.expectError(error.RenderAttachmentUsageMissing, validateView(&device, &view, false));
    view.usage = texture_abi.WGPUTextureUsage_RenderAttachment;
    view.mip_level_count = 2;
    try std.testing.expectError(error.RenderAttachmentViewInvalid, validateView(&device, &view, false));
    view.mip_level_count = 1;
    view.array_layer_count = 2;
    try std.testing.expectError(error.RenderAttachmentViewInvalid, validateView(&device, &view, false));
    // One layer of a 2d-array view renders; more layers do not.
    view.dimension = texture_abi.WGPUTextureViewDimension_2DArray;
    try std.testing.expectError(error.RenderAttachmentViewInvalid, validateView(&device, &view, false));
    view.array_layer_count = 1;
    try std.testing.expectEqual(&view, try validateView(&device, &view, false));
    view.dimension = texture_abi.WGPUTextureViewDimension_Cube;
    try std.testing.expectError(error.RenderAttachmentUnsupported, validateView(&device, &view, false));
    view.dimension = texture_abi.WGPUTextureViewDimension_2D;
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateView(&device, &view, true));
    texture.error_object = true;
    try std.testing.expectError(error.RenderAttachmentUnavailable, validateView(&device, &view, false));
}

test "resolve targets are single-sample views of the multisampled color format and extent" {
    var device = objects.DoeDevice{};
    var msaa = objects.DoeTexture{ .device_ref = &device, .width = 8, .height = 8, .sample_count = 4 };
    var single = objects.DoeTexture{ .device_ref = &device, .width = 8, .height = 8 };
    const base = objects.DoeTextureView{
        .tex = &msaa,
        .format = texture_abi.WGPUTextureFormat_RGBA8Unorm,
        .dimension = texture_abi.WGPUTextureViewDimension_2D,
        .mip_level_count = 1,
        .array_layer_count = 1,
        .aspect = texture_abi.WGPUTextureAspect_All,
        .usage = texture_abi.WGPUTextureUsage_RenderAttachment,
    };
    var color = base;
    var resolve = base;
    resolve.tex = &single;
    try validateResolve(&color, &resolve);
    try std.testing.expectError(error.RenderAttachmentSampleMismatch, validateResolve(&resolve, &resolve));
    resolve.format = texture_abi.WGPUTextureFormat_BGRA8Unorm;
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateResolve(&color, &resolve));
    resolve.format = base.format;
    // Integer and 32-bit float formats have no resolve capability.
    for ([_]u32{ texture_abi.WGPUTextureFormat_RGBA8Uint, texture_abi.WGPUTextureFormat_R32Float }) |format| {
        color.format = format;
        resolve.format = format;
        try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateResolve(&color, &resolve));
    }
    // texture-formats-tier1 makes the 8-bit snorm formats resolvable, not the
    // 16-bit unorm/snorm ones.
    color.format = texture_abi.WGPUTextureFormat_RGBA8Snorm;
    resolve.format = color.format;
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateResolve(&color, &resolve));
    var tier1 = [_]feature_abi.WGPUFeatureName{feature_abi.WGPUFeatureName_TextureFormatsTier1};
    device.enabled_features = &tier1;
    try validateResolve(&color, &resolve);
    color.format = values.WGPUTextureFormat_RG16Unorm;
    resolve.format = color.format;
    try std.testing.expectError(error.RenderAttachmentFormatMismatch, validateResolve(&color, &resolve));
    device.enabled_features = &.{};
    color.format = base.format;
    resolve.format = base.format;
    single.width = 4;
    try std.testing.expectError(error.RenderAttachmentExtentMismatch, validateResolve(&color, &resolve));
}
