//! Delivery and immutable identity projection only. The private C image owns policy.
const std = @import("std");
const build_options = @import("build_options");
const abi = @import("../core/abi/wgpu_handle_types.zig");
const backend = @import("../contracts/backend.zig");
const manifest = @import("dropin_proc_manifest.zig");
const ownership = @import("dropin_symbol_ownership.zig");
const behavior_policy = @import("dropin_behavior_policy.zig");
const router = @import("dropin_router.zig");
const c = @cImport({
    @cInclude("doe_dropin_policy_private.h");
});

fn bytes(value: []const u8) c.DoeDropinBytes {
    return .{ .data = value.ptr, .length = value.len };
}

fn ownerCode(owner: ownership.SymbolOwner) u32 {
    return switch (owner) {
        .dawn_delegate => c.DOE_DROPIN_OWNER_DAWN,
        .doe_metal => c.DOE_DROPIN_OWNER_METAL,
        .doe_vulkan => c.DOE_DROPIN_OWNER_VULKAN,
        .doe_d3d12 => c.DOE_DROPIN_OWNER_D3D12,
        .shared => c.DOE_DROPIN_OWNER_SHARED,
    };
}

fn ownerFromCode(code: u32) ownership.SymbolOwner {
    return switch (code) {
        c.DOE_DROPIN_OWNER_DAWN => .dawn_delegate,
        c.DOE_DROPIN_OWNER_METAL => .doe_metal,
        c.DOE_DROPIN_OWNER_VULKAN => .doe_vulkan,
        c.DOE_DROPIN_OWNER_D3D12 => .doe_d3d12,
        c.DOE_DROPIN_OWNER_SHARED => .shared,
        else => @panic("C drop-in policy returned an invalid owner code"),
    };
}

fn modeFromCode(code: u32) behavior_policy.BehaviorMode {
    return switch (code) {
        c.DOE_DROPIN_MODE_DAWN => .dawn_ownership,
        c.DOE_DROPIN_MODE_MIXED => .mixed_ownership,
        c.DOE_DROPIN_MODE_METAL => .doe_metal_ownership,
        c.DOE_DROPIN_MODE_VULKAN => .doe_vulkan_ownership,
        c.DOE_DROPIN_MODE_D3D12 => .doe_d3d12_ownership,
        else => @panic("C drop-in policy returned an invalid mode code"),
    };
}

const manifest_rows = blk: {
    var rows: [manifest.ACTIVE_IDENTITIES.len]c.DoeDropinOwnership = undefined;
    for (manifest.ACTIVE_IDENTITIES, 0..) |entry, index| {
        rows[index] = .{ .symbol = bytes(entry.symbol), .owner = ownerCode(entry.owner) };
    }
    break :blk rows;
};

const lane_rows = blk: {
    var count: usize = 0;
    for (backend.LANE_SPECS) |lane| count += 1 + lane.aliases.len;
    var rows: [count]c.DoeDropinLaneName = undefined;
    var index: usize = 0;
    for (backend.LANE_SPECS) |lane| {
        rows[index] = .{ .spelling = bytes(lane.name), .canonical = bytes(lane.name) };
        index += 1;
        for (lane.aliases) |alias| {
            rows[index] = .{ .spelling = bytes(alias), .canonical = bytes(lane.name) };
            index += 1;
        }
    }
    break :blk rows;
};

const descriptor = c.DoeDropinPolicyDescriptor{
    .abi_version = c.DOE_DROPIN_POLICY_ABI_VERSION,
    .struct_size = @sizeOf(c.DoeDropinPolicyDescriptor),
    .active_tier = switch (build_options.build_tier) {
        .compute => c.DOE_DROPIN_TIER_COMPUTE,
        .headless => c.DOE_DROPIN_TIER_HEADLESS,
        .full => c.DOE_DROPIN_TIER_FULL,
    },
    .reserved = 0,
    .config = &c.doe_dropin_policy_generated_config,
    .manifest = &manifest_rows,
    .manifest_count = manifest_rows.len,
    .lane_names = &lane_rows,
    .lane_name_count = lane_rows.len,
};

fn requireOk(status: c.DoeDropinStatus) void {
    if (status != c.DOE_DROPIN_OK) {
        const message = c.doe_dropin_policy_status_message(status);
        std.debug.panic("{s}", .{std.mem.span(message)});
    }
}

fn bind() void {
    requireOk(c.doe_dropin_policy_bind(&descriptor));
}

pub const Behavior = struct {
    mode: behavior_policy.BehaviorMode,
    strict_no_fallback: bool,
};

pub fn activeBehaviorConfig() Behavior {
    bind();
    var value: c.DoeDropinBehavior = undefined;
    requireOk(c.doe_dropin_policy_behavior(&value));
    return .{ .mode = modeFromCode(value.mode), .strict_no_fallback = value.strict_no_fallback != 0 };
}

pub fn symbolOwnerForName(name: []const u8) ownership.SymbolOwner {
    bind();
    var owner: u32 = undefined;
    requireOk(c.doe_dropin_policy_owner(bytes(name), &owner));
    return ownerFromCode(owner);
}

fn deliverRoute(route: c.DoeDropinRoute) router.RouteDecision {
    return .{ .owner = ownerFromCode(route.effective_owner), .fallback_used = route.fallback_used != 0 };
}

pub fn symbolRouteForName(name: []const u8) router.RouteDecision {
    bind();
    var route: c.DoeDropinRoute = undefined;
    requireOk(c.doe_dropin_policy_route_name(bytes(name), &route));
    return deliverRoute(route);
}

pub fn symbolRouteForView(name: abi.WGPUStringView) router.RouteDecision {
    bind();
    var route: c.DoeDropinRoute = undefined;
    requireOk(c.doe_dropin_policy_route_view(@ptrCast(name.data), name.length, &route));
    return deliverRoute(route);
}

pub fn testDescriptor() *const c.DoeDropinPolicyDescriptor {
    if (!@import("builtin").is_test) @compileError("test-only immutable policy observation");
    return &descriptor;
}
