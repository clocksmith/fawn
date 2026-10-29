//! Evidence for bytes read from the actual plan execution, never a replay.
const std = @import("std");
const types = @import("dawn_plan_types.zig");
const output_oracle = @import("../runtime/output_oracle.zig");

const FrozenOracle = struct {
    kind: []const u8,
    referenceClass: []const u8,
    expectedSha256: []const u8,
    referenceId: []const u8,
    referencePath: ?[]const u8,
    absoluteTolerance: f32,
    relativeTolerance: f32,
};

const Observation = struct {
    commandSeq: usize,
    kernel: []const u8,
    entryPoint: []const u8,
    bindingGroup: u32,
    binding: u32,
    resourceHandle: u64,
    byteLength: u64,
    dispatchCount: u32,
    oracle: FrozenOracle,
    actualSha256: []const u8,
    matched: bool,
    comparedValueCount: u64,
    mismatchCount: u64,
};

pub const Proof = struct {
    allocator: std.mem.Allocator,
    plan_file_sha256: [64]u8,
    observations: std.ArrayList(Observation) = .empty,

    pub fn init(allocator: std.mem.Allocator, plan_bytes: []const u8) Proof {
        var digest: [32]u8 = undefined;
        std.crypto.hash.sha2.Sha256.hash(plan_bytes, &digest, .{});
        return .{ .allocator = allocator, .plan_file_sha256 = std.fmt.bytesToHex(digest, .lower) };
    }

    pub fn deinit(self: *Proof) void {
        for (self.observations.items) |value| self.allocator.free(value.actualSha256);
        self.observations.deinit(self.allocator);
    }

    pub fn observe(self: *Proof, seq: usize, command: types.KernelDispatchCommand, evidence: output_oracle.Evidence) !void {
        const oracle = command.output_oracle orelse return;
        if (!evidence.has_actual_sha256) return;
        const binding = for (command.bindings) |candidate| {
            if (candidate.group == oracle.binding_group and candidate.binding == oracle.binding)
                break candidate;
        } else return error.OutputOracleBindingMissing;
        const actual = try self.allocator.dupe(u8, &evidence.actual_sha256);
        errdefer self.allocator.free(actual);
        try self.observations.append(self.allocator, .{
            .commandSeq = seq,
            .kernel = command.kernel,
            .entryPoint = command.entry_point,
            .bindingGroup = oracle.binding_group,
            .binding = oracle.binding,
            .resourceHandle = binding.resource_handle,
            .byteLength = binding.buffer_size,
            .dispatchCount = 1,
            .oracle = .{
                .kind = oracle.kind,
                .referenceClass = evidence.reference_class.?,
                .expectedSha256 = oracle.expected_sha256,
                .referenceId = oracle.reference_id,
                .referencePath = oracle.reference_path,
                .absoluteTolerance = oracle.absolute_tolerance,
                .relativeTolerance = oracle.relative_tolerance,
            },
            .actualSha256 = actual,
            .matched = evidence.matched_count == 1 and evidence.failed_count == 0,
            .comparedValueCount = evidence.compared_value_count,
            .mismatchCount = evidence.mismatch_count,
        });
    }

    pub fn serialize(self: *const Proof, plan_hash: []const u8) ![]u8 {
        return try std.json.Stringify.valueAlloc(self.allocator, .{
            .schema = "doe.plan-output-proof/v1",
            .executionScope = "same-execution",
            .planSha256 = plan_hash,
            .planFileSha256 = @as([]const u8, &self.plan_file_sha256),
            .observations = self.observations.items,
        }, .{});
    }
};
