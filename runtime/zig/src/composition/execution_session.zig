//! Composition-owned lifetime for a selected backend and its runtime ports.
//!
//! The application/runtime layers borrow a PortBundle. Only this composition
//! root constructs and destroys a concrete provider behind narrow ports.

const std = @import("std");
const model_profile = @import("../contracts/model/model_profile.zig");
const backend_contract = @import("../contracts/backend.zig");
const runtime_configuration = @import("../contracts/runtime_configuration.zig");
const backend_policy = @import("../backend/backend_policy.zig");
const execution = @import("../runtime/execution.zig");
const backend_factory = @import("backend_factory.zig");
const evidence_observer = @import("../contracts/evidence_observer.zig");

pub const Options = struct {
    no_pipeline_cache: bool = false,
    /// Mutable provider cache storage. This is never inferred from kernel_root,
    /// which identifies immutable shader inputs.
    pipeline_cache_dir: []const u8 = "",
    observer: ?evidence_observer.EvidenceObserver = null,
};

pub const RetirementReport = struct {
    path: ?[]const u8 = null,
    tag: []const u8,
    first_error: ?anyerror = null,

    fn write(self: RetirementReport, suffix: []const u8, phase: []const u8) void {
        if (self.path == null and self.first_error == null and !std.mem.eql(u8, phase, "unknown")) return;
        const allocator = std.heap.page_allocator;
        const bytes = std.json.Stringify.valueAlloc(allocator, .{
            .schemaVersion = 1,
            .artifactKind = "session-retirement",
            .phase = phase,
            .claimEligible = false,
            .firstError = if (self.first_error) |err| @errorName(err) else @as(?[]const u8, null),
            .runtimeCloseComplete = std.mem.eql(u8, phase, "retired"),
            .gpuCompletionUnknown = !std.mem.eql(u8, phase, "retired"),
        }, .{}) catch |err| {
            std.debug.print("session-retirement encoding failed {s}; firstError={s}\n", .{
                @errorName(err), if (self.first_error) |first| @errorName(first) else "none",
            });
            return;
        };
        defer allocator.free(bytes);
        const base = self.path orelse {
            std.debug.print("session-retirement {s}\n", .{bytes});
            return;
        };
        const path = std.fmt.allocPrint(allocator, "{s}.{s}.retirement.{s}", .{ base, self.tag, suffix }) catch {
            std.debug.print("session-retirement path allocation failed; {s}\n", .{bytes});
            return;
        };
        defer allocator.free(path);
        const temporary = std.fmt.allocPrint(allocator, "{s}.tmp", .{path}) catch {
            std.debug.print("session-retirement temporary allocation failed; {s}\n", .{bytes});
            return;
        };
        defer allocator.free(temporary);
        const file = std.fs.cwd().createFile(temporary, .{}) catch |err| {
            std.debug.print("session-retirement persistence failed {s}: {s}; {s}\n", .{ path, @errorName(err), bytes });
            return;
        };
        file.writeAll(bytes) catch |err| {
            file.close();
            std.debug.print("session-retirement write failed: {s}; {s}\n", .{ @errorName(err), bytes });
            return;
        };
        file.sync() catch |err| {
            file.close();
            std.debug.print("session-retirement sync failed: {s}; {s}\n", .{ @errorName(err), bytes });
            return;
        };
        file.close();
        std.fs.cwd().rename(temporary, path) catch |err|
            std.debug.print("session-retirement rename failed: {s}; {s}\n", .{ @errorName(err), bytes });
    }
};

const OwnedBackend = struct {
    allocator: std.mem.Allocator,
    provider: backend_factory.OwnedProvider,
    policy_hash: []u8,

    fn init(
        allocator: std.mem.Allocator,
        profile: model_profile.DeviceProfile,
        kernel_root: ?[]const u8,
        pipeline_cache: runtime_configuration.PipelineCacheConfiguration,
        lane: backend_policy.BackendLane,
    ) !OwnedBackend {
        const loaded_policy = try backend_policy.load_policy_for_lane(
            allocator,
            backend_policy.DEFAULT_RUNTIME_POLICY_PATH,
            lane,
        );
        errdefer allocator.free(loaded_policy.owned_policy_hash);
        const selection = backend_contract.select(profile, loaded_policy.policy);
        const provider = try backend_factory.initProvider(
            allocator,
            loaded_policy.policy,
            selection.backend_id,
            profile,
            .{
                .kernel_root = kernel_root,
                .pipeline_cache = pipeline_cache,
            },
            selection.reason,
            selection.fallback_used,
        );
        return .{
            .allocator = allocator,
            .provider = provider,
            .policy_hash = loaded_policy.owned_policy_hash,
        };
    }

    fn deinit(self: *OwnedBackend) bool {
        if (!self.provider.deinit()) return false;
        self.allocator.free(self.policy_hash);
        self.policy_hash = &.{};
        return true;
    }

    fn retireForExit(self: *OwnedBackend, report: RetirementReport) void {
        report.write("pending.json", "retiring");
        if (!self.deinit()) {
            // Durable unknown precedes the indefinite destruction-only drain.
            // No command is executed or resubmitted by this policy.
            report.write("unknown.meta.json", "unknown");
            std.debug.print("session retirement unknown; allocator and resources retained; firstError={s}\n", .{if (report.first_error) |err| @errorName(err) else "none"});
            while (!self.deinit()) std.Thread.sleep(std.time.ns_per_ms);
        }
        report.write("final.json", "retired");
    }
};

pub const ExecutionSession = struct {
    allocator: std.mem.Allocator,
    owned_backend: ?*OwnedBackend,
    context: execution.ExecutionContext,

    pub fn init(
        allocator: std.mem.Allocator,
        mode: execution.BackendMode,
        profile: model_profile.DeviceProfile,
        kernel_root: ?[]const u8,
        lane: backend_policy.BackendLane,
        options: Options,
    ) !ExecutionSession {
        if (mode == .trace) {
            var context = execution.ExecutionContext.initTrace(lane);
            context.setEvidenceObserver(options.observer);
            return .{
                .allocator = allocator,
                .owned_backend = null,
                .context = context,
            };
        }

        const owned_backend = try allocator.create(OwnedBackend);
        errdefer allocator.destroy(owned_backend);
        owned_backend.* = try OwnedBackend.init(
            allocator,
            profile,
            kernel_root,
            .{
                .enabled = !options.no_pipeline_cache,
                .directory = options.pipeline_cache_dir,
            },
            lane,
        );
        errdefer |err| owned_backend.retireForExit(.{
            .tag = "initialization",
            .first_error = err,
        });
        var context = execution.ExecutionContext.initNative(lane, owned_backend.provider.ports);
        context.setEvidenceObserver(options.observer);
        return .{
            .allocator = allocator,
            .owned_backend = owned_backend,
            .context = context,
        };
    }

    pub fn deinit(self: *ExecutionSession) bool {
        if (self.owned_backend) |owned_backend| {
            if (!owned_backend.deinit()) return false;
            self.allocator.destroy(owned_backend);
            self.owned_backend = null;
        }
        self.context.deinit();
        return true;
    }

    /// CLI cleanup cannot unwind the caller's allocator while callbacks or
    /// submitted resources remain live. The first failure is never overwritten.
    pub fn retireForExit(self: *ExecutionSession, report: RetirementReport) void {
        if (self.owned_backend) |owned_backend| {
            owned_backend.retireForExit(report);
            self.allocator.destroy(owned_backend);
            self.owned_backend = null;
        }
        self.context.deinit();
    }

    pub fn contextPtr(self: *ExecutionSession) *execution.ExecutionContext {
        return &self.context;
    }
};

pub fn needsLibraryLifetimeGuard(lane: backend_policy.BackendLane) bool {
    return switch (backend_policy.default_policy_for_lane(lane).default_backend) {
        .dawn_delegate, .webkit_delegate => true,
        else => false,
    };
}

test "library lifetime guard applies only to delegate lanes" {
    try std.testing.expect(needsLibraryLifetimeGuard(.vulkan_dawn_release));
    try std.testing.expect(needsLibraryLifetimeGuard(.metal_webkit_release));
    try std.testing.expect(!needsLibraryLifetimeGuard(.vulkan_doe_comparable));
}

const CloseProbe = struct {
    remaining_unknown: u32 = 1,
    closed: bool = false,
    checkpoint: ?[]const u8 = null,
    saw_checkpoint: bool = false,

    fn close(context: *anyopaque) bool {
        const self: *CloseProbe = @ptrCast(@alignCast(context));
        if (self.checkpoint) |path| self.saw_checkpoint = checkpointPresent(path);
        if (self.remaining_unknown > 0) {
            self.remaining_unknown -= 1;
            return false;
        }
        self.closed = true;
        return true;
    }

    fn checkpointPresent(path: []const u8) bool {
        const a = std.heap.page_allocator;
        const bytes = std.fs.cwd().readFileAlloc(a, path, 8192) catch return false;
        defer a.free(bytes);
        const parsed = std.json.parseFromSlice(std.json.Value, a, bytes, .{}) catch return false;
        defer parsed.deinit();
        const first = parsed.value.object.get("firstError") orelse return false;
        return first == .string and std.mem.eql(u8, first.string, "OriginalCaptureFailure");
    }

    fn session(self: *CloseProbe) !ExecutionSession {
        const a = std.testing.allocator;
        const owned = try a.create(OwnedBackend);
        owned.* = .{
            .allocator = a,
            .policy_hash = try a.dupe(u8, "retained-policy"),
            .provider = .{ .context = self, .destroy_context = close, .ports = undefined },
        };
        return .{
            .allocator = a,
            .owned_backend = owned,
            .context = execution.ExecutionContext.initNative(.vulkan_doe_comparable, undefined),
        };
    }
};

test "Dawn capture lease: composition preserves allocation policy and context on unknown" {
    var probe = CloseProbe{};
    var session = try probe.session();
    const retained = session.owned_backend.?;
    try std.testing.expect(!session.deinit());
    try std.testing.expect(session.owned_backend.? == retained);
    try std.testing.expect(retained.provider.context.? == @as(*anyopaque, &probe));
    try std.testing.expectEqualStrings("retained-policy", retained.policy_hash);
    try std.testing.expect(session.context.ports != null);
    try std.testing.expect(!probe.closed);
    try std.testing.expect(session.deinit());
    try std.testing.expect(probe.closed);
    try std.testing.expect(session.owned_backend == null);
    try std.testing.expect(session.context.ports == null);
    try std.testing.expect(session.deinit());
}

test "Dawn capture lease: failure checkpoint precedes retirement and survives late settlement" {
    var tmp = std.testing.tmpDir(.{});
    defer tmp.cleanup();
    const a = std.testing.allocator;
    const dir = try tmp.dir.realpathAlloc(a, ".");
    defer a.free(dir);
    const base = try std.fs.path.join(a, &.{ dir, "capture" });
    defer a.free(base);
    const pending = try std.fmt.allocPrint(a, "{s}.test.retirement.pending.json", .{base});
    defer a.free(pending);
    var probe = CloseProbe{ .checkpoint = pending };
    var session = try probe.session();
    session.retireForExit(.{ .path = base, .tag = "test", .first_error = error.OriginalCaptureFailure });
    try std.testing.expect(probe.saw_checkpoint);
    try std.testing.expect(probe.closed);
    try std.testing.expect(session.owned_backend == null);
    for ([_][]const u8{ "unknown.meta.json", "final.json" }, [_]bool{ false, true }) |suffix, closed| {
        const path = try std.fmt.allocPrint(a, "{s}.test.retirement.{s}", .{ base, suffix });
        defer a.free(path);
        const bytes = try std.fs.cwd().readFileAlloc(a, path, 8192);
        defer a.free(bytes);
        const parsed = try std.json.parseFromSlice(std.json.Value, a, bytes, .{});
        defer parsed.deinit();
        try std.testing.expectEqualStrings("OriginalCaptureFailure", parsed.value.object.get("firstError").?.string);
        try std.testing.expectEqual(closed, parsed.value.object.get("runtimeCloseComplete").?.bool);
        try std.testing.expectEqual(!closed, parsed.value.object.get("gpuCompletionUnknown").?.bool);
    }
}
