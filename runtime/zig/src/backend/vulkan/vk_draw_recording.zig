const std = @import("std");
const c = @import("vk_constants.zig");
const resources = @import("vk_resources.zig");
const model_render_types = @import("../../contracts/model/model_render_types.zig");
const model_gpu_types = @import("../../contracts/model/model_texture_value_types.zig");
const vk_formats = @import("vk_formats.zig");
const VK_NULL_U64 = c.VK_NULL_U64;
const VK_QUERY_CONTROL_NONE: u32 = 0;

pub const IndexBinding = struct {
    buffer: c.VkBuffer,
    offset: u64,
    format: u32,
    count: u32,
};

pub fn color_clear_value(format: model_gpu_types.WGPUTextureFormat, color: [4]f32) error{InvalidArgument}!c.VkClearValue {
    for (color) |component| if (!std.math.isFinite(component)) return error.InvalidArgument;
    return switch (vk_formats.color_component_kind(format)) {
        .float => .{ .color = .{ .float32 = color } },
        .uint => blk: {
            var values: [4]u32 = undefined;
            for (color, &values) |component, *value| {
                value.* = @intFromFloat(std.math.clamp(@as(f64, component), 0, std.math.maxInt(u32)));
            }
            break :blk .{ .color = .{ .uint32 = values } };
        },
        .sint => blk: {
            var values: [4]i32 = undefined;
            for (color, &values) |component, *value| {
                value.* = @intFromFloat(std.math.clamp(@as(f64, component), std.math.minInt(i32), std.math.maxInt(i32)));
            }
            break :blk .{ .color = .{ .int32 = values } };
        },
    };
}

/// An indirect draw's arguments after WebGPU validation: drawn directly with
/// these values, or skipped (the render pass still loads, clears and stores).
pub const IndirectDraw = union(enum) {
    skip,
    draw: IndirectArgs,
};

/// VkDrawIndirectCommand (4 words) or VkDrawIndexedIndirectCommand (5 words).
pub const IndirectArgs = struct {
    count: u32,
    instance_count: u32,
    first: u32,
    base_vertex: i32 = 0,
    first_instance: u32,

    pub fn decode(words: []const u32, indexed: bool) IndirectArgs {
        if (indexed) return .{ .count = words[0], .instance_count = words[1], .first = words[2], .base_vertex = @bitCast(words[3]), .first_instance = words[4] };
        return .{ .count = words[0], .instance_count = words[1], .first = words[2], .first_instance = words[3] };
    }
};

/// https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-draw applied to
/// GPU-supplied arguments: each vertex, instance and index range must fit the
/// bound buffers, or the draw does nothing. Sums are formed in u64.
pub fn admitIndirect(args: IndirectArgs, limits: model_render_types.IndirectDrawLimits, indexed: bool) IndirectDraw {
    if (args.count == 0 or args.instance_count == 0) return .skip;
    if (args.first_instance != 0 and !limits.first_instance_allowed) return .skip;
    const count_limit = if (indexed) limits.index else limits.vertex;
    if (@as(u64, args.first) + args.count > count_limit) return .skip;
    if (@as(u64, args.first_instance) + args.instance_count > limits.instance) return .skip;
    return .{ .draw = args };
}

/// Borrows native handles and a read-only buffer registry only for recording.
/// Its caller owns allocation, image transitions, submission and retirement.
pub const DrawRecording = struct {
    command_buffer: c.VkCommandBuffer,
    buffers: *const std.AutoHashMapUnmanaged(u64, resources.ComputeBuffer),
    render_pass: c.VkRenderPass,
    framebuffer: c.VkFramebuffer,
    pipeline: c.VkPipeline,
    pipeline_layout: c.VkPipelineLayout,
    /// Descriptor sets for bind groups 0..len, bound at set 0.
    descriptor_sets: []const c.VkDescriptorSet,
    /// One clear value per render pass attachment, in attachment order.
    clear_values: []const c.VkClearValue,

    fn resolve_buffer(self: *const DrawRecording, raw: ?*anyopaque) ?c.VkBuffer {
        const handle = raw orelse return null;
        const buffer = self.buffers.get(@intFromPtr(handle)) orelse return null;
        return buffer.buffer;
    }

    /// `indirect` is the validated argument set for an indirect draw (ignored otherwise).
    pub fn record(self: *const DrawRecording, cmd: model_render_types.RenderDrawCommand, index: ?IndexBinding, draw_count: u32, target_width: u32, target_height: u32, indirect: IndirectDraw) error{InvalidArgument}!void {
        var render_pass_begin = c.VkRenderPassBeginInfo{
            .sType = c.VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = null,
            .renderPass = self.render_pass,
            .framebuffer = self.framebuffer,
            .renderArea = .{
                .offset = .{ .x = 0, .y = 0 },
                .extent = .{ .width = target_width, .height = target_height },
            },
            .clearValueCount = @intCast(self.clear_values.len),
            .pClearValues = self.clear_values.ptr,
        };
        if (cmd.occlusion_query_pool != 0) {
            if (cmd.occlusion_query_index) |query_index|
                c.vkCmdResetQueryPool(self.command_buffer, @intCast(cmd.occlusion_query_pool), query_index, 1);
        }
        c.vkCmdBeginRenderPass(self.command_buffer, &render_pass_begin, c.VK_SUBPASS_CONTENTS_INLINE);
        if (cmd.occlusion_query_pool != 0) {
            if (cmd.occlusion_query_index) |query_index| {
                const query_pool: c.VkQueryPool = @intCast(cmd.occlusion_query_pool);
                c.vkCmdBeginQuery(self.command_buffer, query_pool, query_index, VK_QUERY_CONTROL_NONE);
            }
        }

        if (cmd.vertex_bindings) |bs| {
            for (bs) |binding| {
                const vk_buffer = self.resolve_buffer(binding.handle) orelse continue;
                const buffers_arr = [1]c.VkBuffer{vk_buffer};
                const offsets_arr = [1]u64{binding.offset};
                c.vkCmdBindVertexBuffers(self.command_buffer, binding.slot, 1, &buffers_arr, &offsets_arr);
            }
        }
        const vp_width = cmd.viewport_width orelse @as(f32, @floatFromInt(target_width));
        const vp_height = cmd.viewport_height orelse @as(f32, @floatFromInt(target_height));
        const empty_viewport = vp_width == 0 or vp_height == 0;
        var viewport = webgpu_viewport(cmd.viewport_x, cmd.viewport_y, vp_width, vp_height, cmd.viewport_min_depth, cmd.viewport_max_depth);
        c.vkCmdSetViewport(self.command_buffer, 0, 1, @ptrCast(&viewport));
        // An empty WebGPU viewport rasterizes nothing: Vulkan needs a positive
        // width, so the stand-in viewport is paired with an empty scissor.
        const sc_width = if (empty_viewport) 0 else cmd.scissor_width orelse target_width;
        const sc_height = if (empty_viewport) 0 else cmd.scissor_height orelse target_height;
        var scissor = c.VkRect2D{
            .offset = .{
                .x = @intCast(cmd.scissor_x),
                .y = @intCast(cmd.scissor_y),
            },
            .extent = .{ .width = sc_width, .height = sc_height },
        };
        c.vkCmdSetScissor(self.command_buffer, 0, 1, @ptrCast(&scissor));

        if (cmd.depth_bias != 0 or cmd.depth_bias_slope_scale != 0 or cmd.depth_bias_clamp != 0) {
            c.vkCmdSetDepthBias(
                self.command_buffer,
                @floatFromInt(cmd.depth_bias),
                cmd.depth_bias_clamp,
                cmd.depth_bias_slope_scale,
            );
        }

        if (cmd.depth_stencil_format != model_gpu_types.WGPUTextureFormat_Undefined) {
            c.vkCmdSetStencilReference(
                self.command_buffer,
                c.VK_STENCIL_FACE_FRONT_AND_BACK,
                cmd.stencil_reference,
            );
        }

        c.vkCmdBindPipeline(self.command_buffer, c.VK_PIPELINE_BIND_POINT_GRAPHICS, self.pipeline);

        if (self.descriptor_sets.len > 0) {
            c.vkCmdBindDescriptorSets(
                self.command_buffer,
                c.VK_PIPELINE_BIND_POINT_GRAPHICS,
                self.pipeline_layout,
                0,
                @intCast(self.descriptor_sets.len),
                self.descriptor_sets.ptr,
                0,
                null,
            );
        }

        // Bound slots need not be contiguous (an unused slot is a hole), so
        // each bound slot binds at its own index.
        var slot: u32 = 0;
        while (slot < cmd.vertex_buffer_count and slot < model_render_types.MAX_VERTEX_BUFFERS) : (slot += 1) {
            const handle = cmd.vertex_buffer_handles[slot];
            if (handle == 0) continue;
            const compute_buffer = self.buffers.get(handle) orelse return error.InvalidArgument;
            const buffers = [1]c.VkBuffer{compute_buffer.buffer};
            const offsets = [1]u64{cmd.vertex_buffer_offsets[slot]};
            c.vkCmdBindVertexBuffers(self.command_buffer, slot, 1, &buffers, &offsets);
        }

        if (index) |binding| c.vkCmdBindIndexBuffer(self.command_buffer, binding.buffer, binding.offset, binding.format);
        if (cmd.indirect_buffer_handle != 0) {
            // The arguments were read and validated before recording; the GPU
            // never consumes unvalidated indirect arguments.
            switch (indirect) {
                .skip => {},
                .draw => |args| if (index != null)
                    c.vkCmdDrawIndexed(self.command_buffer, args.count, args.instance_count, args.first, args.base_vertex, args.first_instance)
                else
                    c.vkCmdDraw(self.command_buffer, args.count, args.instance_count, args.first, args.first_instance),
            }
        } else {
            for (0..draw_count) |_| {
                if (index) |binding| {
                    c.vkCmdDrawIndexed(self.command_buffer, binding.count, cmd.instance_count, cmd.first_index, cmd.base_vertex, cmd.first_instance);
                } else {
                    c.vkCmdDraw(self.command_buffer, cmd.vertex_count, cmd.instance_count, cmd.first_vertex, cmd.first_instance);
                }
            }
        }
        if (cmd.occlusion_query_pool != 0) {
            if (cmd.occlusion_query_index) |query_index| {
                const query_pool: c.VkQueryPool = @intCast(cmd.occlusion_query_pool);
                c.vkCmdEndQuery(self.command_buffer, query_pool, query_index);
            }
        }

        c.vkCmdEndRenderPass(self.command_buffer);
    }
};

/// WebGPU framebuffer coordinates put y = 0 at the top with clip-space +y up;
/// Vulkan maps clip-space +y down. A negative-height viewport (core since
/// Vulkan 1.1) anchored at the bottom edge gives WebGPU orientation without
/// touching shaders. An empty WebGPU viewport becomes a 1x1 stand-in.
pub fn webgpu_viewport(x: f32, y: f32, width: f32, height: f32, min_depth: f32, max_depth: f32) c.VkViewport {
    if (width == 0 or height == 0) return .{ .x = 0, .y = 0, .width = 1, .height = 1, .minDepth = min_depth, .maxDepth = max_depth };
    return .{ .x = x, .y = y + height, .width = width, .height = -height, .minDepth = min_depth, .maxDepth = max_depth };
}

test "WebGPU viewports flip to a negative Vulkan height anchored at the bottom edge" {
    const viewport = webgpu_viewport(2, 4, 16, 8, 0, 1);
    try std.testing.expectEqual(@as(f32, 12), viewport.y);
    try std.testing.expectEqual(@as(f32, -8), viewport.height);
    try std.testing.expectEqual(@as(f32, 1), webgpu_viewport(2, 4, 0, 8, 0, 1).width);
}
