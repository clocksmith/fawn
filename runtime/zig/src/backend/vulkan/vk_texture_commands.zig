// vk_texture_commands.zig — Texture write, read, copy, query, and destroy
// commands for the NativeVulkanRuntime. Sharded from native_runtime.zig.

const std = @import("std");
const model_gpu_types = @import("../../contracts/model/model_texture_value_types.zig");
const model_render_types = @import("../../contracts/model/model_render_types.zig");
const model_texture_types = @import("../../contracts/model/model_texture_types.zig");
const model_resource_types = @import("../../contracts/model/model_resource_types.zig");
const c = @import("vk_constants.zig");
const vk_device = @import("vk_device.zig");
const vk_sync = @import("vk_sync.zig");
const vk_upload = @import("vk_upload.zig");
const vk_resources = @import("vk_resources.zig");
const vk_formats = @import("vk_formats.zig");
const vk_compute_sync = @import("vk_compute_sync.zig");

const copy_contract = @import("../../contracts/texture_copy.zig");
const texture_format_layout = @import("../../contracts/texture_format_layout.zig");
pub const BufferCopy = copy_contract.Copy;

fn copy_texture(texture: vk_resources.TextureResource) copy_contract.Texture {
    return .{
        .width = texture.width,
        .height = texture.height,
        .layers = texture.depth_or_array_layers,
        .mip_levels = texture.mip_levels,
        .samples = texture.sample_count,
        .dimension = texture.dimension,
        .format = texture.format,
    };
}

pub fn buffer_copy_region(buffer_size: u64, texture: vk_resources.TextureResource, copy: BufferCopy, direction: copy_contract.Direction) !c.VkBufferImageCopy {
    const region = try copy_contract.validate(buffer_size, copy_texture(texture), copy, direction, .native);
    const is_3d = texture.dimension == model_gpu_types.WGPUTextureDimension_3D;
    return .{
        .bufferOffset = copy.offset,
        .bufferRowLength = region.row_length,
        .bufferImageHeight = region.image_height,
        .imageSubresource = .{
            .aspectMask = switch (region.aspect) {
                .color => c.VK_IMAGE_ASPECT_COLOR_BIT,
                .depth => vk_formats.VK_IMAGE_ASPECT_DEPTH_BIT,
                .stencil => vk_formats.VK_IMAGE_ASPECT_STENCIL_BIT,
            },
            .mipLevel = copy.mip,
            .baseArrayLayer = if (is_3d) 0 else copy.origin[2],
            .layerCount = if (is_3d) 1 else copy.depth_or_layers,
        },
        .imageOffset = .{ .x = @intCast(copy.origin[0]), .y = @intCast(copy.origin[1]), .z = if (is_3d) @intCast(copy.origin[2]) else 0 },
        .imageExtent = .{ .width = @min(copy.width, @max(texture.width >> @intCast(copy.mip), 1) -| copy.origin[0]), .height = @min(copy.height, @max(texture.height >> @intCast(copy.mip), 1) -| copy.origin[1]), .depth = if (is_3d) copy.depth_or_layers else 1 },
    };
}

pub fn record_buffer_copy(self: anytype, source: vk_resources.ComputeBuffer, texture: *vk_resources.TextureResource, copy: BufferCopy) !bool {
    return record_buffer_image_copy(.buffer_to_texture, self, source, texture, copy);
}

pub fn record_texture_to_buffer(self: anytype, texture: *vk_resources.TextureResource, destination: vk_resources.ComputeBuffer, copy: BufferCopy) !bool {
    return record_buffer_image_copy(.texture_to_buffer, self, destination, texture, copy);
}

const CopyDirection = copy_contract.Direction;

fn record_buffer_image_copy(comptime direction: CopyDirection, self: anytype, buffer: vk_resources.ComputeBuffer, texture: *vk_resources.TextureResource, copy: BufferCopy) !bool {
    const region = try buffer_copy_region(buffer.size, texture.*, copy, direction);
    if (copy.width == 0 or copy.height == 0 or copy.depth_or_layers == 0) return false;
    const command_buffer = try self.begin_prepared_dispatch_replay();
    const barrier = c.VkMemoryBarrier{
        .sType = c.VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = null,
        .srcAccessMask = c.VK_ACCESS_SHADER_WRITE_BIT | c.VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = c.VK_ACCESS_TRANSFER_READ_BIT | c.VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    c.vkCmdPipelineBarrier(command_buffer, c.VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, c.VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, @ptrCast(&barrier), 0, null, 0, null);
    const layout = if (direction == .buffer_to_texture) c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL else c.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const access = if (direction == .buffer_to_texture) c.VK_ACCESS_TRANSFER_WRITE_BIT else c.VK_ACCESS_TRANSFER_READ_BIT;
    vk_resources.transition_texture_layout(command_buffer, texture.*, texture.layout, layout, c.VK_ACCESS_MEMORY_READ_BIT | c.VK_ACCESS_MEMORY_WRITE_BIT, access, c.VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, c.VK_PIPELINE_STAGE_TRANSFER_BIT);
    switch (direction) {
        .buffer_to_texture => c.vkCmdCopyBufferToImage(command_buffer, buffer.buffer, texture.image, layout, 1, @ptrCast(&region)),
        .texture_to_buffer => {
            c.vkCmdCopyImageToBuffer(command_buffer, texture.image, layout, buffer.buffer, 1, @ptrCast(&region));
            self.has_pending_transfer_writes = true;
            if (buffer.mapped != null) vk_compute_sync.make_transfer_writes_visible_for_host_read(command_buffer);
        },
    }
    vk_resources.transition_texture_layout(command_buffer, texture.*, layout, c.VK_IMAGE_LAYOUT_GENERAL, access, c.VK_ACCESS_MEMORY_READ_BIT | c.VK_ACCESS_MEMORY_WRITE_BIT, c.VK_PIPELINE_STAGE_TRANSFER_BIT, c.VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    vk_resources.mark_texture_image_layout(self, texture.image, c.VK_IMAGE_LAYOUT_GENERAL);
    return true;
}

test "buffer image copy validates the last accessed byte and mip-relative layers" {
    var texture = std.mem.zeroes(vk_resources.TextureResource);
    texture.width = 8;
    texture.height = 4;
    texture.depth_or_array_layers = 3;
    texture.mip_levels = 3;
    texture.sample_count = 1;
    texture.dimension = model_gpu_types.WGPUTextureDimension_2D;
    texture.format = model_gpu_types.WGPUTextureFormat_RGBA8Unorm;
    var copy = BufferCopy{ .offset = 16, .bytes_per_row = 256, .rows_per_image = 3, .mip = 1, .width = 4, .height = 2, .depth_or_layers = 2 };
    const minimum_source_size = 16 + 256 * 3 + 256 + 16;
    const region = try buffer_copy_region(minimum_source_size, texture, copy, .texture_to_buffer);
    try std.testing.expectEqual(@as(u32, 4), region.imageExtent.width);
    try std.testing.expectEqual(@as(u32, 2), region.imageSubresource.layerCount);
    try std.testing.expectEqual(@as(u32, 1), region.imageExtent.depth);
    try std.testing.expectError(error.TextureCopyRange, buffer_copy_region(minimum_source_size - 1, texture, copy, .texture_to_buffer));
    copy.width = 5;
    try std.testing.expectError(error.TextureCopyRange, buffer_copy_region(minimum_source_size, texture, copy, .texture_to_buffer));
    copy.width = 4;
    texture.dimension = model_gpu_types.WGPUTextureDimension_3D;
    try std.testing.expectError(error.TextureCopyRange, buffer_copy_region(minimum_source_size, texture, copy, .texture_to_buffer));
    texture.depth_or_array_layers = 4;
    const volume = try buffer_copy_region(minimum_source_size, texture, copy, .texture_to_buffer);
    try std.testing.expectEqual(@as(u32, 1), volume.imageSubresource.layerCount);
    try std.testing.expectEqual(@as(u32, 2), volume.imageExtent.depth);
    copy.offset = std.math.maxInt(u64) - 3;
    try std.testing.expectError(error.TextureCopyRange, buffer_copy_region(std.math.maxInt(u64), texture, copy, .texture_to_buffer));
}

test "compressed buffer copy converts block rows to Vulkan texels" {
    var texture = std.mem.zeroes(vk_resources.TextureResource);
    texture.width = 10;
    texture.height = 12;
    texture.depth_or_array_layers = 1;
    texture.mip_levels = 1;
    texture.sample_count = 1;
    texture.dimension = model_gpu_types.WGPUTextureDimension_2D;
    texture.format = model_gpu_types.WGPUTextureFormat_ASTC5x4Unorm;
    const copy = BufferCopy{ .offset = 0, .bytes_per_row = 256, .rows_per_image = 3, .mip = 0, .width = 10, .height = 12, .depth_or_layers = 1 };
    const region = try buffer_copy_region(256 * 2 + 32, texture, copy, .texture_to_buffer);
    try std.testing.expectEqual(@as(u32, 80), region.bufferRowLength);
    try std.testing.expectEqual(@as(u32, 12), region.bufferImageHeight);
    texture.format = model_gpu_types.WGPUTextureFormat_Depth24PlusStencil8;
    try std.testing.expectError(error.TextureCopyAspect, buffer_copy_region(1024, texture, copy, .texture_to_buffer));
}

/// The whole first layer of the command's mip level, the legacy command shape.
fn whole_mip_copy(texture: anytype) BufferCopy {
    const shift: u5 = @intCast(@min(texture.mip_level, 31));
    return .{
        .offset = 0,
        .bytes_per_row = texture.bytes_per_row,
        .rows_per_image = texture.rows_per_image,
        .mip = texture.mip_level,
        .width = @max(texture.width >> shift, 1),
        .height = @max(texture.height >> shift, 1),
        .depth_or_layers = 1,
        .aspect = texture.aspect,
    };
}

/// Writes `data` into the copy's origin, extent, layers and single aspect.
/// Layout, range and aspect violations return the texture copy contract's
/// errors before any staging allocation or submission.
pub fn texture_write(self: anytype, cmd_arg: model_texture_types.TextureWriteCommand) !void {
    const resource = try vk_resources.ensure_texture_resource(self, cmd_arg.texture);
    if (cmd_arg.data.len == 0 and cmd_arg.copy == null) {
        try vk_resources.ensure_texture_shader_layout(self, resource);
        return;
    }
    const copy = cmd_arg.copy orelse whole_mip_copy(cmd_arg.texture);
    // writeTexture data needs no texel-aligned offset or row pitch; the rows
    // are repacked tightly into staging, which Vulkan's texel-unit
    // bufferRowLength can always describe.
    const source = try copy_contract.validateWrite(cmd_arg.data.len, copy_texture(resource.*), copy);
    if (copy.width == 0 or copy.height == 0 or copy.depth_or_layers == 0) return;
    var tight = copy;
    tight.offset = 0;
    tight.bytes_per_row = source.row_bytes;
    tight.rows_per_image = source.rows;
    const packed_len = @as(u64, source.row_bytes) * source.rows * copy.depth_or_layers;
    const region = try buffer_copy_region(packed_len, resource.*, tight, .buffer_to_texture);
    if (self.has_deferred_submissions or self.pending_uploads.items.len > 0) {
        _ = try self.flush_queue();
    }
    try vk_device.ensure_submission_state(self);
    const staging = try vk_resources.create_host_visible_buffer(self, packed_len, c.VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    defer vk_resources.destroy_host_visible_buffer(self, staging);
    if (staging.mapped) |raw| {
        const destination = @as([*]u8, @ptrCast(raw))[0..@intCast(packed_len)];
        var image: u64 = 0;
        while (image < copy.depth_or_layers) : (image += 1) {
            var row: u64 = 0;
            while (row < source.rows) : (row += 1) {
                const from: usize = @intCast(copy.offset + image * source.pitch * source.image_rows + row * source.pitch);
                const to: usize = @intCast((image * source.rows + row) * source.row_bytes);
                @memcpy(destination[to..][0..source.row_bytes], cmd_arg.data[from..][0..source.row_bytes]);
            }
        }
    }
    try c.check_vk(c.vkResetCommandPool(self.device, self.command_pool, 0));
    var begin_info = c.VkCommandBufferBeginInfo{
        .sType = c.VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = null,
        .flags = c.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = null,
    };
    try c.check_vk(c.vkBeginCommandBuffer(self.primary_command_buffer, &begin_info));
    vk_resources.transition_texture_layout(
        self.primary_command_buffer,
        resource.*,
        resource.layout,
        c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,
        c.VK_ACCESS_TRANSFER_WRITE_BIT,
        c.VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        c.VK_PIPELINE_STAGE_TRANSFER_BIT,
    );
    c.vkCmdCopyBufferToImage(self.primary_command_buffer, staging.buffer, resource.image, c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, @ptrCast(&region));
    vk_resources.transition_texture_layout(
        self.primary_command_buffer,
        resource.*,
        c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        c.VK_IMAGE_LAYOUT_GENERAL,
        c.VK_ACCESS_TRANSFER_WRITE_BIT,
        c.VK_ACCESS_SHADER_READ_BIT | c.VK_ACCESS_SHADER_WRITE_BIT,
        c.VK_PIPELINE_STAGE_TRANSFER_BIT,
        c.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    );
    try c.check_vk(c.vkEndCommandBuffer(self.primary_command_buffer));
    try submit_and_wait_timeline(self);
    vk_resources.mark_texture_image_layout(self, resource.image, c.VK_IMAGE_LAYOUT_GENERAL);
}

test "queue texture writes keep the destination origin, extent and stencil plane" {
    var texture = std.mem.zeroes(vk_resources.TextureResource);
    texture.width = 4;
    texture.height = 4;
    texture.depth_or_array_layers = 3;
    texture.mip_levels = 1;
    texture.sample_count = 1;
    texture.dimension = model_gpu_types.WGPUTextureDimension_2D;
    texture.format = model_gpu_types.WGPUTextureFormat_RGBA8Unorm;
    var copy = BufferCopy{ .offset = 0, .bytes_per_row = 256, .rows_per_image = 1, .mip = 0, .width = 1, .height = 1, .depth_or_layers = 1, .origin = .{ 1, 2, 2 } };
    const region = try buffer_copy_region(4, texture, copy, .buffer_to_texture);
    try std.testing.expectEqual(@as(i32, 1), region.imageOffset.x);
    try std.testing.expectEqual(@as(i32, 2), region.imageOffset.y);
    try std.testing.expectEqual(@as(u32, 2), region.imageSubresource.baseArrayLayer);
    try std.testing.expectEqual(@as(u32, 1), region.imageExtent.width);
    try std.testing.expectEqual(@as(u32, 64), region.bufferRowLength);
    texture.depth_or_array_layers = 1;
    texture.format = model_gpu_types.WGPUTextureFormat_Depth24PlusStencil8;
    copy = .{ .offset = 0, .bytes_per_row = 4, .rows_per_image = 4, .mip = 0, .width = 4, .height = 4, .depth_or_layers = 1 };
    try std.testing.expectError(error.TextureCopyAspect, buffer_copy_region(16, texture, copy, .buffer_to_texture));
    copy.aspect = model_gpu_types.WGPUTextureAspect_DepthOnly;
    try std.testing.expectError(error.TextureCopyUnsupported, buffer_copy_region(16, texture, copy, .buffer_to_texture));
    copy.aspect = model_gpu_types.WGPUTextureAspect_StencilOnly;
    const stencil = try buffer_copy_region(16, texture, copy, .buffer_to_texture);
    try std.testing.expectEqual(@as(u32, vk_formats.VK_IMAGE_ASPECT_STENCIL_BIT), stencil.imageSubresource.aspectMask);
    try std.testing.expectEqual(@as(u32, 4), stencil.bufferRowLength);
    const legacy = model_resource_types.CopyTextureResource{ .handle = 1, .kind = .texture, .width = 8, .height = 2, .mip_level = 1, .bytes_per_row = 16, .aspect = model_gpu_types.WGPUTextureAspect_All };
    const whole = whole_mip_copy(legacy);
    try std.testing.expectEqual(@as(u32, 4), whole.width);
    try std.testing.expectEqual(@as(u32, 1), whole.height);
}

/// Records UNDEFINED -> TRANSFER_DST and zero contents for every subresource:
/// clears for color and depth/stencil, a zeroed buffer copy for block-
/// compressed formats (which cannot be cleared). Returns the new layout.
fn record_zero_initialization(self: anytype, texture: *vk_resources.TextureResource, staging: *?vk_resources.ComputeBuffer) !u32 {
    const command_buffer = self.primary_command_buffer;
    vk_resources.transition_texture_layout(command_buffer, texture.*, c.VK_IMAGE_LAYOUT_UNDEFINED, c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, c.VK_ACCESS_TRANSFER_WRITE_BIT, c.VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, c.VK_PIPELINE_STAGE_TRANSFER_BIT);
    const is_3d = texture.dimension == model_gpu_types.WGPUTextureDimension_3D;
    const layers: u32 = if (is_3d) 1 else @max(texture.depth_or_array_layers, 1);
    const aspect = vk_formats.aspect_mask_for_format(texture.format);
    const range = c.VkImageSubresourceRange{ .aspectMask = aspect, .baseMipLevel = 0, .levelCount = @max(texture.mip_levels, 1), .baseArrayLayer = 0, .layerCount = layers };
    const block = vk_formats.copy_block_extent(texture.format);
    if (vk_formats.is_depth_stencil(texture.format)) {
        const value = c.VkClearDepthStencilValue{ .depth = 0, .stencil = 0 };
        c.vkCmdClearDepthStencilImage(command_buffer, texture.image, c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, @ptrCast(&range));
    } else if (block[0] == 1 and block[1] == 1) {
        const value = std.mem.zeroes(c.VkClearColorValue);
        c.vkCmdClearColorImage(command_buffer, texture.image, c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, @ptrCast(&range));
    } else {
        const block_bytes: u64 = try texture_format_layout.bytes_per_pixel(texture.format);
        const width = std.math.divCeil(u64, texture.width, block[0]) catch unreachable;
        const height = std.math.divCeil(u64, texture.height, block[1]) catch unreachable;
        const depth: u64 = if (is_3d) @max(texture.depth_or_array_layers, 1) else 1;
        const bytes = try std.math.mul(u64, try std.math.mul(u64, try std.math.mul(u64, width * height, block_bytes), depth), layers);
        const buffer = try vk_resources.create_host_visible_buffer(self, bytes, c.VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        staging.* = buffer;
        if (buffer.mapped) |raw| @memset(@as([*]u8, @ptrCast(raw))[0..@intCast(bytes)], 0);
        var mip: u32 = 0;
        while (mip < @max(texture.mip_levels, 1)) : (mip += 1) {
            const shift: u5 = @intCast(@min(mip, 31));
            const region = c.VkBufferImageCopy{
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = .{ .aspectMask = aspect, .mipLevel = mip, .baseArrayLayer = 0, .layerCount = layers },
                .imageOffset = .{ .x = 0, .y = 0, .z = 0 },
                .imageExtent = .{
                    .width = @max(texture.width >> shift, 1),
                    .height = @max(texture.height >> shift, 1),
                    .depth = if (is_3d) @max(texture.depth_or_array_layers >> shift, 1) else 1,
                },
            };
            c.vkCmdCopyBufferToImage(command_buffer, buffer.buffer, texture.image, c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, @ptrCast(&region));
        }
    }
    return c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

pub fn texture_copy(self: anytype, args: struct {
    src_handle: u64,
    src_mip: u32,
    src_x: u32,
    src_y: u32,
    src_z: u32,
    dst_handle: u64,
    dst_mip: u32,
    dst_x: u32,
    dst_y: u32,
    dst_z: u32,
    width: u32,
    height: u32,
    depth_or_layers: u32,
}) !void {
    const src = self.textures.getPtr(args.src_handle) orelse return error.InvalidState;
    const dst = self.textures.getPtr(args.dst_handle) orelse return error.InvalidState;
    if (self.has_deferred_submissions or self.pending_uploads.items.len > 0) {
        _ = try self.flush_queue();
    }
    try vk_device.ensure_submission_state(self);
    try c.check_vk(c.vkResetCommandPool(self.device, self.command_pool, 0));
    var begin_info = c.VkCommandBufferBeginInfo{
        .sType = c.VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = null,
        .flags = c.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = null,
    };
    try c.check_vk(c.vkBeginCommandBuffer(self.primary_command_buffer, &begin_info));
    // A never-written image holds WebGPU's zero contents, which Vulkan's
    // UNDEFINED layout does not guarantee: zero it in this same submission.
    var src_zero: ?vk_resources.ComputeBuffer = null;
    var dst_zero: ?vk_resources.ComputeBuffer = null;
    defer if (src_zero) |staging| vk_resources.destroy_host_visible_buffer(self, staging);
    defer if (dst_zero) |staging| vk_resources.destroy_host_visible_buffer(self, staging);
    const src_prev = if (src.layout == c.VK_IMAGE_LAYOUT_UNDEFINED) try record_zero_initialization(self, src, &src_zero) else src.layout;
    const dst_prev = if (dst.layout == c.VK_IMAGE_LAYOUT_UNDEFINED and src.image != dst.image)
        try record_zero_initialization(self, dst, &dst_zero)
    else if (src.image == dst.image) src_prev else dst.layout;
    // Layouts cover the whole image, so a copy between subresources of one
    // texture keeps it in GENERAL, which Vulkan admits for both sides.
    const same = src.image == dst.image;
    const src_layout: u32 = if (same) c.VK_IMAGE_LAYOUT_GENERAL else c.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const dst_layout: u32 = if (same) c.VK_IMAGE_LAYOUT_GENERAL else c.VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    const src_access: u32 = if (same) c.VK_ACCESS_TRANSFER_READ_BIT | c.VK_ACCESS_TRANSFER_WRITE_BIT else c.VK_ACCESS_TRANSFER_READ_BIT;
    vk_resources.transition_texture_layout(self.primary_command_buffer, src.*, src_prev, src_layout, vk_resources.texture_transition_source(src_prev).src_access_mask, src_access, vk_resources.texture_transition_source(src_prev).src_stage, c.VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (!same) vk_resources.transition_texture_layout(self.primary_command_buffer, dst.*, dst_prev, dst_layout, vk_resources.texture_transition_source(dst_prev).src_access_mask, c.VK_ACCESS_TRANSFER_WRITE_BIT, vk_resources.texture_transition_source(dst_prev).src_stage, c.VK_PIPELINE_STAGE_TRANSFER_BIT);
    const layers = if (args.depth_or_layers > 0) args.depth_or_layers else 1;
    // WebGPU origin.z and depthOrArrayLayers are array layers for 1D/2D
    // textures and depth slices for 3D textures.
    const src_3d = src.dimension == model_gpu_types.WGPUTextureDimension_3D;
    const dst_3d = dst.dimension == model_gpu_types.WGPUTextureDimension_3D;
    var region = c.VkImageCopy{
        .srcSubresource = .{ .aspectMask = vk_formats.aspect_mask_for_format(src.format), .mipLevel = args.src_mip, .baseArrayLayer = if (src_3d) 0 else args.src_z, .layerCount = if (src_3d) 1 else layers },
        .srcOffset = .{ .x = @intCast(args.src_x), .y = @intCast(args.src_y), .z = if (src_3d) @intCast(args.src_z) else 0 },
        .dstSubresource = .{ .aspectMask = vk_formats.aspect_mask_for_format(dst.format), .mipLevel = args.dst_mip, .baseArrayLayer = if (dst_3d) 0 else args.dst_z, .layerCount = if (dst_3d) 1 else layers },
        .dstOffset = .{ .x = @intCast(args.dst_x), .y = @intCast(args.dst_y), .z = if (dst_3d) @intCast(args.dst_z) else 0 },
        .extent = .{ .width = args.width, .height = args.height, .depth = if (src_3d or dst_3d) layers else 1 },
    };
    c.vkCmdCopyImage(self.primary_command_buffer, src.image, src_layout, dst.image, dst_layout, 1, @ptrCast(&region));
    vk_resources.transition_texture_layout(self.primary_command_buffer, src.*, src_layout, c.VK_IMAGE_LAYOUT_GENERAL, src_access, c.VK_ACCESS_SHADER_READ_BIT | c.VK_ACCESS_SHADER_WRITE_BIT, c.VK_PIPELINE_STAGE_TRANSFER_BIT, c.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (!same) vk_resources.transition_texture_layout(self.primary_command_buffer, dst.*, dst_layout, c.VK_IMAGE_LAYOUT_GENERAL, c.VK_ACCESS_TRANSFER_WRITE_BIT, c.VK_ACCESS_SHADER_READ_BIT | c.VK_ACCESS_SHADER_WRITE_BIT, c.VK_PIPELINE_STAGE_TRANSFER_BIT, c.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    try c.check_vk(c.vkEndCommandBuffer(self.primary_command_buffer));
    try submit_and_wait_timeline(self);
    vk_resources.mark_texture_image_layout(self, src.image, c.VK_IMAGE_LAYOUT_GENERAL);
    vk_resources.mark_texture_image_layout(self, dst.image, c.VK_IMAGE_LAYOUT_GENERAL);
}

/// Zeroes every subresource of a texture that was never written, so it reads
/// as WebGPU's zero contents whatever its memory last held: destroy() frees
/// memory at once, and the next allocation may reuse it. Leaves the texture in
/// the layout the zeroing used.
pub fn texture_zero_initialize(self: anytype, handle: u64) !void {
    const texture = self.textures.getPtr(handle) orelse return error.InvalidState;
    if (texture.layout != c.VK_IMAGE_LAYOUT_UNDEFINED) return;
    if (self.has_deferred_submissions or self.pending_uploads.items.len > 0) {
        _ = try self.flush_queue();
    }
    try vk_device.ensure_submission_state(self);
    try c.check_vk(c.vkResetCommandPool(self.device, self.command_pool, 0));
    var begin_info = c.VkCommandBufferBeginInfo{
        .sType = c.VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = null,
        .flags = c.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = null,
    };
    try c.check_vk(c.vkBeginCommandBuffer(self.primary_command_buffer, &begin_info));
    var staging: ?vk_resources.ComputeBuffer = null;
    defer if (staging) |buffer| vk_resources.destroy_host_visible_buffer(self, buffer);
    const layout = try record_zero_initialization(self, texture, &staging);
    try c.check_vk(c.vkEndCommandBuffer(self.primary_command_buffer));
    try submit_and_wait_timeline(self);
    vk_resources.mark_texture_image_layout(self, texture.image, layout);
}

pub fn texture_query(self: anytype, cmd_arg: model_texture_types.TextureQueryCommand) !void {
    const texture = self.textures.get(cmd_arg.handle) orelse return error.InvalidState;
    if (cmd_arg.expected_width) |width| if (texture.width != width) return error.InvalidState;
    if (cmd_arg.expected_height) |height| if (texture.height != height) return error.InvalidState;
    if (cmd_arg.expected_depth_or_array_layers) |layers| if (texture.depth_or_array_layers != layers) return error.InvalidState;
    if (cmd_arg.expected_format) |format| if (texture.format != format) return error.InvalidState;
    if (cmd_arg.expected_dimension) |dimension| if (texture.dimension != dimension) return error.InvalidState;
    if (cmd_arg.expected_view_dimension) |view_dimension| if (texture.view_dimension != view_dimension) return error.InvalidState;
    if (cmd_arg.expected_sample_count) |sample_count| if (texture.sample_count != sample_count) return error.InvalidState;
    if (cmd_arg.expected_usage) |usage| if ((texture.usage & usage) != usage) return error.InvalidState;
}

pub fn texture_destroy(self: anytype, cmd_arg: model_texture_types.TextureDestroyCommand) !void {
    if (self.textures.fetchRemove(cmd_arg.handle)) |entry| {
        vk_resources.release_texture_resource(self, entry.value);
    }
}

pub fn sampler_create(self: anytype, cmd: model_render_types.SamplerCreateCommand) !void {
    _ = try vk_resources.create_sampler(self, cmd);
}

pub fn sampler_destroy(self: anytype, cmd: model_render_types.SamplerDestroyCommand) !void {
    vk_resources.destroy_sampler(self, cmd.handle);
}

const vk_pipeline = @import("vk_pipeline.zig");

pub fn collect_dispatch_gpu_timestamp(self: anytype) !u64 {
    var query_pool: c.VkQueryPool = c.VK_NULL_U64;
    defer if (query_pool != c.VK_NULL_U64) c.vkDestroyQueryPool(self.device, query_pool, null);
    var create_info = c.VkQueryPoolCreateInfo{
        .sType = c.VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext = null,
        .flags = 0,
        .queryType = c.VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2,
        .pipelineStatistics = 0,
    };
    try c.check_vk(c.vkCreateQueryPool(self.device, &create_info, null, &query_pool));
    try c.check_vk(c.vkResetCommandPool(self.device, self.command_pool, 0));
    var begin_info = c.VkCommandBufferBeginInfo{
        .sType = c.VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = null,
        .flags = c.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = null,
    };
    try c.check_vk(c.vkBeginCommandBuffer(self.primary_command_buffer, &begin_info));
    c.vkCmdWriteTimestamp(self.primary_command_buffer, c.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, query_pool, 0);
    c.vkCmdBindPipeline(self.primary_command_buffer, c.VK_PIPELINE_BIND_POINT_COMPUTE, self.pipeline);
    vk_pipeline.bind_descriptor_sets(self, self.primary_command_buffer);
    c.vkCmdDispatch(self.primary_command_buffer, 1, 1, 1);
    c.vkCmdWriteTimestamp(self.primary_command_buffer, c.VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, query_pool, 1);
    try c.check_vk(c.vkEndCommandBuffer(self.primary_command_buffer));
    try submit_and_wait_timeline(self);
    var results: [2]u64 = .{ 0, 0 };
    try c.check_vk(c.vkGetQueryPoolResults(self.device, query_pool, 0, 2, @sizeOf(@TypeOf(results)), &results, @sizeOf(u64), c.VK_QUERY_RESULT_64_BIT | c.VK_QUERY_RESULT_WAIT_BIT));
    if (results[1] <= results[0]) return 0;
    return results[1] - results[0];
}

// Submit current command buffer and wait (timeline or fence).
fn submit_and_wait_timeline(self: anytype) !void {
    var submit_info = c.VkSubmitInfo{
        .sType = c.VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = null,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = null,
        .pWaitDstStageMask = null,
        .commandBufferCount = 1,
        .pCommandBuffers = @ptrCast(&self.primary_command_buffer),
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = null,
    };
    if (self.has_timeline_semaphore) {
        var tsi = try vk_sync.TimelineSubmitHelper.prepare(&self.timeline_semaphore);
        tsi.patch();
        submit_info.pNext = @ptrCast(&tsi.timeline_info);
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = @ptrCast(&tsi.semaphore);
        try tsi.submit(&self.timeline_semaphore, self.queue, &submit_info);
        try self.timeline_semaphore.wait(self.device, tsi.signal_value);
    } else {
        try c.check_vk(c.vkResetFences(self.device, 1, @ptrCast(&self.fence)));
        try c.check_vk(c.vkQueueSubmit(self.queue, 1, @ptrCast(&submit_info), self.fence));
        try c.check_vk(c.vkWaitForFences(self.device, 1, @ptrCast(&self.fence), c.VK_TRUE, vk_upload.WAIT_TIMEOUT_NS));
    }
}
