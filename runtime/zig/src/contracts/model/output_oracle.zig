const std = @import("std");
const model_compute_types = @import("model_compute_types.zig");
const Allocator = std.mem.Allocator;
const ParseError = error{InvalidCommandPayload};

pub const Raw = struct {
    schema_version: ?u32 = null,
    schemaVersion: ?u32 = null,
    scope: ?[]const u8 = null,
    reference_class: ?[]const u8 = null,
    referenceClass: ?[]const u8 = null,
    kind: ?[]const u8 = null,
    initialization: ?[]const u8 = null,
    binding_group: ?u32 = null,
    bindingGroup: ?u32 = null,
    binding: ?u32 = null,
    dispatch_count: ?u32 = null,
    dispatchCount: ?u32 = null,
    expected_sha256: ?[]const u8 = null,
    expectedSha256: ?[]const u8 = null,
    reference_sha256: ?[]const u8 = null,
    referenceSha256: ?[]const u8 = null,
    reference_id: ?[]const u8 = null,
    referenceId: ?[]const u8 = null,
    reference_path: ?[]const u8 = null,
    referencePath: ?[]const u8 = null,
    absolute_tolerance: ?f32 = null,
    absoluteTolerance: ?f32 = null,
    relative_tolerance: ?f32 = null,
    relativeTolerance: ?f32 = null,
};

pub fn parse(allocator: Allocator, raw: Raw) !model_compute_types.KernelDispatchOutputOracle {
    const schema_version = raw.schema_version orelse raw.schemaVersion orelse return ParseError.InvalidCommandPayload;
    const scope: model_compute_types.KernelDispatchOutputOracleScope = switch (schema_version) {
        1 => if (raw.scope == null)
            .isolated_dispatch
        else
            return ParseError.InvalidCommandPayload,
        2, 3 => if (std.mem.eql(u8, raw.scope orelse return ParseError.InvalidCommandPayload, "command_graph"))
            .command_graph
        else
            return ParseError.InvalidCommandPayload,
        else => return ParseError.InvalidCommandPayload,
    };
    const raw_reference_class = raw.reference_class orelse raw.referenceClass;
    const reference_class: model_compute_types.KernelDispatchOutputOracleReferenceClass = switch (schema_version) {
        1 => if (raw_reference_class == null)
            .independent
        else
            return ParseError.InvalidCommandPayload,
        2, 3 => if (std.mem.eql(u8, raw_reference_class orelse return ParseError.InvalidCommandPayload, "independent_v1"))
            .independent
        else if (std.mem.eql(u8, raw_reference_class.?, "cross_runtime_consensus_v1"))
            .cross_runtime_consensus
        else
            return ParseError.InvalidCommandPayload,
        else => return ParseError.InvalidCommandPayload,
    };
    const kind = raw.kind orelse return ParseError.InvalidCommandPayload;
    const initialization = raw.initialization orelse return ParseError.InvalidCommandPayload;
    const binding_group = raw.binding_group orelse raw.bindingGroup orelse return ParseError.InvalidCommandPayload;
    const binding = raw.binding orelse return ParseError.InvalidCommandPayload;
    const dispatch_count = raw.dispatch_count orelse raw.dispatchCount orelse return ParseError.InvalidCommandPayload;
    const expected_sha256 = if (schema_version == 3)
        raw.reference_sha256 orelse raw.referenceSha256 orelse return ParseError.InvalidCommandPayload
    else
        raw.expected_sha256 orelse raw.expectedSha256 orelse return ParseError.InvalidCommandPayload;
    const reference_id = raw.reference_id orelse raw.referenceId orelse return ParseError.InvalidCommandPayload;
    const reference_path = raw.reference_path orelse raw.referencePath;
    const absolute_tolerance = raw.absolute_tolerance orelse raw.absoluteTolerance orelse 0;
    const relative_tolerance = raw.relative_tolerance orelse raw.relativeTolerance orelse 0;
    if (dispatch_count == 0 or expected_sha256.len != 64) return ParseError.InvalidCommandPayload;
    if (!std.mem.eql(u8, initialization, "zero_fill_v1")) {
        return ParseError.InvalidCommandPayload;
    }
    if (schema_version <= 2 and !std.mem.eql(u8, kind, "sha256_exact_v1")) {
        return ParseError.InvalidCommandPayload;
    }
    if (schema_version == 3 and
        (!std.mem.eql(u8, kind, "float32_reference_tolerance_v1") or
            reference_path == null or reference_path.?.len == 0 or
            !std.math.isFinite(absolute_tolerance) or absolute_tolerance < 0 or
            !std.math.isFinite(relative_tolerance) or relative_tolerance < 0 or
            (absolute_tolerance == 0 and relative_tolerance == 0)))
    {
        return ParseError.InvalidCommandPayload;
    }
    const owned_kind = try allocator.dupe(u8, kind);
    errdefer allocator.free(owned_kind);
    const owned_initialization = try allocator.dupe(u8, initialization);
    errdefer allocator.free(owned_initialization);
    const owned_expected = try allocator.dupe(u8, expected_sha256);
    errdefer allocator.free(owned_expected);
    const owned_reference = try allocator.dupe(u8, reference_id);
    errdefer allocator.free(owned_reference);
    const owned_reference_path = if (reference_path) |path| try allocator.dupe(u8, path) else null;
    errdefer if (owned_reference_path) |path| allocator.free(path);
    return .{
        .schema_version = schema_version,
        .scope = scope,
        .reference_class = reference_class,
        .kind = owned_kind,
        .initialization = owned_initialization,
        .binding_group = binding_group,
        .binding = binding,
        .dispatch_count = dispatch_count,
        .expected_sha256 = owned_expected,
        .reference_id = owned_reference,
        .reference_path = owned_reference_path,
        .absolute_tolerance = absolute_tolerance,
        .relative_tolerance = relative_tolerance,
    };
}
