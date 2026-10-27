//! Instance feature advertisement and requirement admission use the same selected owner.
const std = @import("std");
const build_options = @import("build_options");
const capabilities = @import("../../core/abi/procs/wgpu_p1_capability_procs.zig");
const c_instance = if (build_options.experimental_c_instance) @import("doe_instance_c_adapter.zig") else struct {};

const upstream = @import("../../core/abi/generated/webgpu_upstream.zig").c;
const InstanceDescriptor = upstream.WGPUInstanceDescriptor;

pub fn features() capabilities.SupportedInstanceFeatures {
    return if (build_options.experimental_c_instance) c_instance.features() else capabilities.initSupportedInstanceFeatures();
}

pub fn limits() capabilities.InstanceLimits {
    return if (build_options.experimental_c_instance) c_instance.limits() else capabilities.initInstanceLimits();
}

pub fn hasFeature(feature: capabilities.WGPUInstanceFeatureName) u32 {
    return @intFromBool(capabilities.hasInstanceFeatureInList(features(), feature));
}

/// Borrow the public C descriptor only during admission, before instance allocation.
/// Unsupported required features/limits cannot be silently accepted by either owner.
pub fn requirementsSupported(raw: ?*anyopaque) bool {
    const pointer = raw orelse return true;
    const descriptor: *const InstanceDescriptor = @ptrCast(@alignCast(pointer));
    return requirementsSupportedByOwner(descriptor, features(), limits());
}

fn requirementsSupportedByOwner(
    descriptor: *const InstanceDescriptor,
    supported: capabilities.SupportedInstanceFeatures,
    supported_limits: capabilities.InstanceLimits,
) bool {
    // Neither selected instance owner accepts a descriptor extension chain.
    if (descriptor.nextInChain != null) return false;
    if (descriptor.requiredFeatureCount != 0) {
        _ = std.math.mul(usize, descriptor.requiredFeatureCount, @sizeOf(capabilities.WGPUInstanceFeatureName)) catch return false;
        if (supported.featureCount == 0) return false;
        const names = descriptor.requiredFeatures;
        if (names == null) return false;
        for (names[0..descriptor.requiredFeatureCount]) |name| {
            if (!capabilities.hasInstanceFeatureInList(supported, name)) return false;
        }
    }
    if (descriptor.requiredLimits != null) {
        const required = descriptor.requiredLimits[0];
        if (required.nextInChain != null or required.timedWaitAnyMaxCount > supported_limits.timedWaitAnyMaxCount)
            return false;
    }
    return true;
}

test "instance requirements reject unsupported features, malformed lists and unavailable limits" {
    var descriptor = std.mem.zeroes(InstanceDescriptor);
    try std.testing.expect(requirementsSupported(null));
    try std.testing.expect(requirementsSupported(&descriptor));
    var unsupported = [_]capabilities.WGPUInstanceFeatureName{0x7fff_ffff};
    descriptor.requiredFeatureCount = unsupported.len;
    descriptor.requiredFeatures = &unsupported;
    try std.testing.expect(!requirementsSupported(&descriptor));
    descriptor.requiredFeatures = null;
    try std.testing.expect(!requirementsSupported(&descriptor));
    descriptor.requiredFeatureCount = std.math.maxInt(usize);
    try std.testing.expect(!requirementsSupported(&descriptor));
    descriptor = std.mem.zeroes(InstanceDescriptor);
    var requested = std.mem.zeroes(upstream.WGPUInstanceLimits);
    requested.timedWaitAnyMaxCount = limits().timedWaitAnyMaxCount + 1;
    descriptor.requiredLimits = &requested;
    try std.testing.expect(!requirementsSupported(&descriptor));
    requested.timedWaitAnyMaxCount = 0;
    try std.testing.expect(requirementsSupported(&descriptor));
    var chain = std.mem.zeroes(upstream.WGPUChainedStruct);
    requested.nextInChain = &chain;
    try std.testing.expect(!requirementsSupported(&descriptor));
    if (!build_options.experimental_c_instance) {
        try std.testing.expectEqual(@as(usize, 0), features().featureCount);
        try std.testing.expectEqual(@as(usize, 0), limits().timedWaitAnyMaxCount);
    }
}

test "instance descriptor chains are unsupported by the selected instance owner" {
    var chain = std.mem.zeroes(upstream.WGPUChainedStruct);
    var descriptor = std.mem.zeroes(InstanceDescriptor);
    descriptor.nextInChain = &chain;
    try std.testing.expect(!requirementsSupported(&descriptor));
}

test "instance admission preserves repeated supported features and exact owner limits" {
    const available = [_]capabilities.WGPUInstanceFeatureName{
        upstream.WGPUInstanceFeatureName_TimedWaitAny,
        upstream.WGPUInstanceFeatureName_MultipleDevicesPerAdapter,
    };
    const supported: capabilities.SupportedInstanceFeatures = .{ .featureCount = available.len, .features = &available };
    const supported_limits: capabilities.InstanceLimits = .{ .nextInChain = null, .timedWaitAnyMaxCount = 64 };
    const repeated = [_]capabilities.WGPUInstanceFeatureName{available[0]} ** 3;
    var descriptor = std.mem.zeroes(InstanceDescriptor);
    descriptor.requiredFeatureCount = repeated.len;
    descriptor.requiredFeatures = &repeated;
    var required = std.mem.zeroes(upstream.WGPUInstanceLimits);
    required.timedWaitAnyMaxCount = 64;
    descriptor.requiredLimits = &required;
    try std.testing.expect(requirementsSupportedByOwner(&descriptor, supported, supported_limits));
    required.timedWaitAnyMaxCount = 65;
    try std.testing.expect(!requirementsSupportedByOwner(&descriptor, supported, supported_limits));
    required.timedWaitAnyMaxCount = 64;
    descriptor.requiredFeatureCount = std.math.maxInt(usize);
    try std.testing.expect(!requirementsSupportedByOwner(&descriptor, supported, supported_limits));
}
