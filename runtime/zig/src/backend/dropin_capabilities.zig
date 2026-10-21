const builtin = @import("builtin");
const std = @import("std");
const vk = @import("vulkan/vk_constants.zig");
const bridge = @import("metal/metal_bridge_decls.zig");

pub const d3d12_device_caps = @import("d3d12/d3d12_device_caps.zig");
pub const vk_feature_caps = if (builtin.os.tag == .linux) @import("vulkan/vk_feature_caps.zig") else struct {};
pub const vk_device_caps = if (builtin.os.tag == .linux) @import("vulkan/vk_device_caps.zig") else struct {};
pub const vk_feature_probe = if (builtin.os.tag == .linux) @import("vulkan/vk_feature_probe.zig") else struct {};
pub const vk_adapter_probe = if (builtin.os.tag == .linux) @import("vulkan/vk_adapter_probe.zig") else struct {};

pub fn metal_device_max_buffer_length(device: ?*anyopaque) u64 {
    return bridge.metal_bridge_device_max_buffer_length(device);
}

pub fn metal_supports_timestamp_query(device: ?*anyopaque) bool {
    return bridge.metal_bridge_supports_timestamp_query(device) != 0;
}

// Vulkan and WebGPU use different device-type values. Virtual/other devices
// have no corresponding WebGPU class and remain explicitly unknown.
pub fn vulkanAdapterType(device_type: u32) u32 {
    const WGPU_ADAPTER_TYPE_DISCRETE_GPU: u32 = 1;
    const WGPU_ADAPTER_TYPE_INTEGRATED_GPU: u32 = 2;
    const WGPU_ADAPTER_TYPE_CPU: u32 = 3;
    const WGPU_ADAPTER_TYPE_UNKNOWN: u32 = 4;
    return switch (device_type) {
        vk.VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU => WGPU_ADAPTER_TYPE_DISCRETE_GPU,
        vk.VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU => WGPU_ADAPTER_TYPE_INTEGRATED_GPU,
        vk.VK_PHYSICAL_DEVICE_TYPE_CPU => WGPU_ADAPTER_TYPE_CPU,
        else => WGPU_ADAPTER_TYPE_UNKNOWN,
    };
}

test "adapter Vulkan classification preserves physical classes and unknown values" {
    // Independent foreign ABI values; do not derive expectations from the map.
    const cases = [_][2]u32{ .{ 0, 4 }, .{ 1, 2 }, .{ 2, 1 }, .{ 3, 4 }, .{ 4, 3 }, .{ 0xffff, 4 } };
    for (cases) |case| try std.testing.expectEqual(case[1], vulkanAdapterType(case[0]));
}
