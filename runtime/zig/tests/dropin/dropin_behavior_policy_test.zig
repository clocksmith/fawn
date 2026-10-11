const std = @import("std");
const policy = @import("../../src/dropin/dropin_behavior_policy.zig");

test "packed WGSL feature enumeration agrees with feature queries" {
    const exports = @import("../../src/dropin/wgpu_dropin_ext_a_exports.zig");
    const capabilities = @import("../../src/core/abi/procs/wgpu_p1_capability_procs.zig");
    const feature = capabilities.WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct;
    var supported = capabilities.initSupportedWGSLLanguageFeatures();
    exports.wgpuInstanceGetWGSLLanguageFeatures(null, &supported);
    try std.testing.expect(capabilities.hasWGSLLanguageFeatureInList(supported, feature));
    try std.testing.expectEqual(@as(u32, 1), exports.wgpuInstanceHasWGSLLanguageFeature(null, feature));
    try std.testing.expectEqual(@as(u32, 0), exports.wgpuInstanceHasWGSLLanguageFeature(null, 0x7fffffff));
}

test "behavior mode parser accepts strict mode" {
    try std.testing.expect(policy.parse_behavior_mode("doe_metal_ownership") != null);
}

test "behavior mode parser accepts strict vulkan mode" {
    try std.testing.expect(policy.parse_behavior_mode("doe_vulkan_ownership") != null);
}

test "behavior mode parser accepts strict d3d12 mode" {
    try std.testing.expect(policy.parse_behavior_mode("doe_d3d12_ownership") != null);
}

test "behavior mode parser rejects unknown mode" {
    try std.testing.expect(policy.parse_behavior_mode("no_such_mode") == null);
}
