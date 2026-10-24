const std = @import("std");
const c = @import("vk_constants.zig");
const vk_compute_sync = @import("vk_compute_sync.zig");
const vk_pipeline = @import("vk_pipeline.zig");
const resources = @import("vk_resources.zig");
const shader_source = @import("vk_shader_source.zig");
const translation = @import("../../compiler/wgsl/pipeline/translate_spirv.zig");
const binding_types = @import("../../contracts/model/model_binding_value_types.zig");
const compute_types = @import("../../contracts/model/model_compute_types.zig");

const VK_NULL_U64 = c.VK_NULL_U64;
const ARGS_BYTES: u64 = 3 * @sizeOf(u32);

pub fn record_prepared_replay(
    self: anytype,
    indirect_buffer: c.VkBuffer,
    indirect_offset: u64,
    indirect_resource_handle: u64,
) !void {
    if (!self.has_pipeline) return error.Unsupported;
    if (indirect_buffer == VK_NULL_U64) return error.InvalidArgument;
    const command_buffer = try self.begin_prepared_dispatch_replay();
    vk_compute_sync.make_prior_transfer_writes_visible_for_indirect_dispatch(self, command_buffer);
    vk_compute_sync.make_prior_compute_writes_visible_for_indirect_read(self, command_buffer, indirect_resource_handle, indirect_buffer);
    vk_compute_sync.make_replay_compute_writes_visible(self, command_buffer);
    vk_pipeline.bind_compute_pipeline_if_needed(self, command_buffer);
    vk_pipeline.bind_descriptor_sets_if_needed(self, command_buffer);
    c.vkCmdDispatchIndirect(command_buffer, indirect_buffer, indirect_offset);
    vk_compute_sync.remember_current_compute_writes(self);
}

/// WebGPU dispatchWorkgroupsIndirect: a workgroup count above
/// maxComputeWorkgroupsPerDimension makes the dispatch do nothing
/// (https://gpuweb.github.io/gpuweb/#dom-gpucomputepassencoder-dispatchworkgroupsindirect).
/// The arguments are GPU data, so a one-invocation guard copies them to scratch
/// and zeroes all three counts when any exceeds the limit; the dispatch then
/// reads the scratch copy and never consumes unvalidated arguments.
pub fn guard_source(allocator: std.mem.Allocator, limit: u32) ![]u8 {
    return std.fmt.allocPrint(allocator,
        \\const LIMIT = {d}u;
        \\@group(0) @binding(0) var<storage, read_write> args: array<u32, 3>;
        \\@compute @workgroup_size(1) fn main() {{
        \\    if (args[0] > LIMIT || args[1] > LIMIT || args[2] > LIMIT) {{
        \\        args[0] = 0u;
        \\        args[1] = 0u;
        \\        args[2] = 0u;
        \\    }}
        \\}}
        \\
    , .{limit});
}

fn guard_spirv(rt: anytype, limit: u32) ![]const u32 {
    var key_buffer: [64]u8 = undefined;
    const key = try std.fmt.bufPrint(&key_buffer, "doe:vulkan-dispatch-indirect-guard-v1:{d}", .{limit});
    if (rt.kernel_spirv_cache.get(key)) |words| return words;
    const source = try guard_source(rt.allocator, limit);
    defer rt.allocator.free(source);
    const bytes = try rt.allocator.alloc(u8, translation.MAX_OUTPUT);
    defer rt.allocator.free(bytes);
    const length = try translation.translateToSpirv(rt.allocator, source, bytes);
    const words = try shader_source.words_from_spirv_bytes(rt.allocator, bytes[0..length]);
    errdefer rt.allocator.free(words);
    const owned_key = try rt.allocator.dupe(u8, key);
    errdefer rt.allocator.free(owned_key);
    try rt.kernel_spirv_cache.put(rt.allocator, owned_key, words);
    return words;
}

fn guard_binding(handle: u64) compute_types.KernelBinding {
    return .{ .binding = 0, .resource_kind = .buffer, .resource_handle = handle, .buffer_size = ARGS_BYTES, .buffer_type = binding_types.WGPUBufferBindingType_Storage };
}

pub const GuardedArgs = struct {
    buffer: c.VkBuffer,
    resource_handle: u64,
};

/// Records the copy and the guard into the replay command buffer. It leaves the
/// guard pipeline bound; the caller prepares the application dispatch next.
pub fn record_guard(rt: anytype, source: c.VkBuffer, source_offset: u64, limit: u32) !GuardedArgs {
    if (rt.dispatch_indirect_guard_handle == 0) {
        const handle = rt.next_buffer_resource_handle;
        rt.next_buffer_resource_handle = try std.math.add(u64, handle, 1);
        _ = try resources.ensure_compute_buffer_for_binding(rt, guard_binding(handle), false);
        rt.dispatch_indirect_guard_handle = handle;
    }
    const handle = rt.dispatch_indirect_guard_handle;
    const bindings = [_]compute_types.KernelBinding{guard_binding(handle)};
    try rt.set_compute_shader_spirv(try guard_spirv(rt, limit), "main", &bindings, false);
    const scratch = rt.compute_buffers.get(handle) orelse return error.InvalidState;
    const command_buffer = try rt.begin_prepared_dispatch_replay();
    // Earlier writes to the arguments and earlier indirect reads of the scratch
    // complete before the copy reads and overwrites them.
    const before_copy = c.VkMemoryBarrier{
        .sType = c.VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = null,
        .srcAccessMask = c.VK_ACCESS_SHADER_WRITE_BIT | c.VK_ACCESS_TRANSFER_WRITE_BIT | c.VK_ACCESS_INDIRECT_COMMAND_READ_BIT | c.VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = c.VK_ACCESS_TRANSFER_READ_BIT | c.VK_ACCESS_TRANSFER_WRITE_BIT,
    };
    c.vkCmdPipelineBarrier(command_buffer, c.VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, c.VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, @ptrCast(&before_copy), 0, null, 0, null);
    const region = c.VkBufferCopy{ .srcOffset = source_offset, .dstOffset = 0, .size = ARGS_BYTES };
    c.vkCmdCopyBuffer(command_buffer, source, scratch.buffer, 1, @ptrCast(&region));
    rt.has_pending_transfer_writes = true;
    try rt.record_prepared_dispatch_replay_on(command_buffer, 1, 1, 1);
    return .{ .buffer = scratch.buffer, .resource_handle = handle };
}
