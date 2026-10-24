const std = @import("std");
const builtin = @import("builtin");
const log = std.log.scoped(.doe_vulkan_feature_cache);
const has_vulkan = (builtin.os.tag == .linux);
const backend_capabilities = @import("../../backend/dropin_capabilities.zig");
const vk_feature_caps = if (has_vulkan) backend_capabilities.vk_feature_caps else struct {
    pub const VulkanFeatureCaps = struct {};
};
const vk_device_caps = if (has_vulkan) backend_capabilities.vk_device_caps else struct {
    pub const VulkanDeviceCaps = struct {};
};

var gpa = std.heap.GeneralPurposeAllocator(.{}){};
const alloc = gpa.allocator();

/// Adapters and devices are created and released on any thread (one per
/// N-API worker environment); every access to the process-wide maps below
/// holds this lock. Unsynchronized puts corrupted the maps under concurrent
/// requestAdapter (Debug: pointer_stability assertion; release: silent).
var mutex: std.Thread.Mutex = .{};

var adapter_caps: std.AutoHashMapUnmanaged(usize, vk_feature_caps.VulkanFeatureCaps) = .{};
var device_caps: std.AutoHashMapUnmanaged(usize, vk_feature_caps.VulkanFeatureCaps) = .{};

// Hardware-queried limits and feature booleans, cached per adapter/device handle.
var adapter_device_caps: std.AutoHashMapUnmanaged(usize, vk_device_caps.VulkanDeviceCaps) = .{};
var device_device_caps: std.AutoHashMapUnmanaged(usize, vk_device_caps.VulkanDeviceCaps) = .{};

fn cache_key(handle: ?*anyopaque) ?usize {
    return if (handle) |ptr| @intFromPtr(ptr) else null;
}

pub fn set_adapter(handle: ?*anyopaque, caps: vk_feature_caps.VulkanFeatureCaps) void {
    const key = cache_key(handle) orelse return;
    mutex.lock();
    defer mutex.unlock();
    adapter_caps.put(alloc, key, caps) catch |err| {
        log.warn("set_adapter: {s}", .{@errorName(err)});
    };
}

pub fn get_adapter(handle: ?*anyopaque) ?vk_feature_caps.VulkanFeatureCaps {
    const key = cache_key(handle) orelse return null;
    mutex.lock();
    defer mutex.unlock();
    return adapter_caps.get(key);
}

pub fn remove_adapter(handle: ?*anyopaque) void {
    const key = cache_key(handle) orelse return;
    mutex.lock();
    defer mutex.unlock();
    _ = adapter_caps.remove(key);
    _ = adapter_device_caps.remove(key);
}

pub fn set_device(handle: ?*anyopaque, caps: vk_feature_caps.VulkanFeatureCaps) void {
    const key = cache_key(handle) orelse return;
    mutex.lock();
    defer mutex.unlock();
    device_caps.put(alloc, key, caps) catch |err| {
        log.warn("set_device: {s}", .{@errorName(err)});
    };
}

pub fn get_device(handle: ?*anyopaque) ?vk_feature_caps.VulkanFeatureCaps {
    const key = cache_key(handle) orelse return null;
    mutex.lock();
    defer mutex.unlock();
    return device_caps.get(key);
}

pub fn remove_device(handle: ?*anyopaque) void {
    const key = cache_key(handle) orelse return;
    mutex.lock();
    defer mutex.unlock();
    _ = device_caps.remove(key);
    _ = device_device_caps.remove(key);
}

// --- Vulkan device caps (limits + hardware features) ---

pub fn set_adapter_device_caps(handle: ?*anyopaque, caps: vk_device_caps.VulkanDeviceCaps) void {
    const key = cache_key(handle) orelse return;
    mutex.lock();
    defer mutex.unlock();
    adapter_device_caps.put(alloc, key, caps) catch |err| {
        log.warn("set_adapter_device_caps: {s}", .{@errorName(err)});
    };
}

pub fn get_adapter_device_caps(handle: ?*anyopaque) ?vk_device_caps.VulkanDeviceCaps {
    const key = cache_key(handle) orelse return null;
    mutex.lock();
    defer mutex.unlock();
    return adapter_device_caps.get(key);
}

pub fn set_device_device_caps(handle: ?*anyopaque, caps: vk_device_caps.VulkanDeviceCaps) void {
    const key = cache_key(handle) orelse return;
    mutex.lock();
    defer mutex.unlock();
    device_device_caps.put(alloc, key, caps) catch |err| {
        log.warn("set_device_device_caps: {s}", .{@errorName(err)});
    };
}

pub fn get_device_device_caps(handle: ?*anyopaque) ?vk_device_caps.VulkanDeviceCaps {
    const key = cache_key(handle) orelse return null;
    mutex.lock();
    defer mutex.unlock();
    return device_device_caps.get(key);
}

test "feature cache tolerates adapters and devices created and released on many threads" {
    if (comptime !has_vulkan) return error.SkipZigTest;
    const Worker = struct {
        fn run(base: usize) void {
            for (0..2000) |i| {
                // Distinct non-null keys per thread; every map is exercised.
                const handle: *anyopaque = @ptrFromInt(0x1000 * (base * 4096 + i + 1));
                set_adapter(handle, .{});
                set_adapter_device_caps(handle, std.mem.zeroes(vk_device_caps.VulkanDeviceCaps));
                set_device(handle, .{});
                set_device_device_caps(handle, std.mem.zeroes(vk_device_caps.VulkanDeviceCaps));
                std.debug.assert(get_adapter(handle) != null and get_device_device_caps(handle) != null);
                remove_device(handle);
                remove_adapter(handle);
                std.debug.assert(get_device(handle) == null and get_adapter_device_caps(handle) == null);
            }
        }
    };
    var threads: [8]std.Thread = undefined;
    for (&threads, 0..) |*thread, index| thread.* = try std.Thread.spawn(.{}, Worker.run, .{index + 1});
    for (threads) |thread| thread.join();
    mutex.lock();
    defer mutex.unlock();
    try std.testing.expectEqual(@as(u32, 0), adapter_caps.count() + device_caps.count() +
        adapter_device_caps.count() + device_device_caps.count());
}
