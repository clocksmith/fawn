const std = @import("std");
const abi_base = @import("../core/abi/wgpu_handle_types.zig");
const loader = @import("../core/abi/wgpu_loader.zig");
const p1_capability_procs = @import("../core/abi/procs/wgpu_p1_capability_procs.zig");
const dropin_ext_a = @import("wgpu_dropin_ext_a.zig");
const dropin_ext_b = @import("wgpu_dropin_ext_b.zig");
const dropin_ext_c = @import("wgpu_dropin_ext_c.zig");
const dropin_symbol_ownership = @import("dropin_symbol_ownership.zig");
const dropin_proc_manifest = @import("dropin_proc_manifest.zig");
const dropin_runtime_config = @import("dropin_runtime_config.zig");
const dropin_router = @import("dropin_router.zig");
const dropin_diagnostics = @import("dropin_diagnostics.zig");
const dropin_abi_procs = @import("dropin_abi_procs.zig");
const dropin_browser_shared_memory = @import("dropin_browser_shared_memory.zig");
const compute_fast = @import("../native/compute/doe_compute_fast.zig");

const build_options = @import("build_options");
pub const BuildTier = @TypeOf(build_options.build_tier);
pub const TIER = build_options.build_tier;

pub export fn doeWgpuDropinBuildInfoSchemaVersion() callconv(.c) u32 {
    return 1;
}

pub export fn doeWgpuDropinLeanVerifiedBuild() callconv(.c) u32 {
    return if (build_options.lean_verified) 1 else 0;
}

comptime {
    _ = dropin_ext_a;
    _ = dropin_ext_b;
    _ = dropin_ext_c;
    _ = dropin_symbol_ownership;
    _ = dropin_router;
    _ = dropin_diagnostics;
    _ = dropin_browser_shared_memory;
    _ = @import("../native/mod.zig");
    // Public render-control exports require these real C recorder peers,
    // independently of which tests or native procedures a tier references.
    const render_recording = @import("../native/render/doe_render_native.zig");
    _ = &render_recording.doeNativeRenderPassRecordViewportState;
    _ = &render_recording.doeNativeRenderPassRecordScissorState;
    _ = &render_recording.doeNativeRenderPassRecordBlendConstantState;
    _ = &render_recording.doeNativeRenderPassRecordStencilReferenceState;
    if (@import("builtin").os.tag == .macos) {
        // Multi-queue management: doeNativeMultiQueueDevice*, doeNativeQueueSubmit, etc.
        _ = @import("../runtime/queue/multi_queue.zig");
    }
}

fn symbolRouteForName(symbol: []const u8) dropin_router.RouteDecision {
    return dropin_runtime_config.symbolRouteForName(symbol);
}

fn symbolNameSlice(name: abi_base.WGPUStringView) ?[]const u8 {
    return dropin_runtime_config.symbolNameSlice(name);
}

fn symbolRouteForView(name: abi_base.WGPUStringView) dropin_router.RouteDecision {
    return dropin_runtime_config.symbolRouteForView(name);
}

pub const DropinErrorCode = enum(u32) {
    ok = 0,
    library_open_failed = 1,
    symbol_missing = 2,
    invalid_symbol_name = 3,
    symbol_name_too_long = 4,
};

var g_state_lock: std.Thread.Mutex = .{};
var g_native_lib: ?std.DynLib = null;
var g_library_ready: bool = false;
var g_library_failed: bool = false;
var g_last_error_code = std.atomic.Value(u32).init(@intFromEnum(DropinErrorCode.ok));
const WgpuAnyProc = *const fn () callconv(.c) void;

fn setLastError(code: DropinErrorCode) void {
    g_last_error_code.store(@intFromEnum(code), .monotonic);
}

pub export fn doeWgpuDropinLastErrorCode() callconv(.c) u32 {
    return g_last_error_code.load(.monotonic);
}

pub export fn doeWgpuDropinClearLastError() callconv(.c) void {
    setLastError(.ok);
}

pub export fn doeNativeComputePrewarmDispatchBindings(
    q_raw: ?*anyopaque,
    dispatch_count: usize,
    pipe_ptrs: [*]?*anyopaque,
    bg_ptrs: [*]?*anyopaque,
    bg_counts: [*]const u32,
) callconv(.c) u32 {
    return compute_fast.prewarmDispatchBindingsForQueue(
        q_raw,
        dispatch_count,
        pipe_ptrs,
        bg_ptrs,
        bg_counts,
    );
}

pub export fn doeNativeComputeDispatchFlushOneBindGroup(
    q_raw: ?*anyopaque,
    pipe_raw: ?*anyopaque,
    bg0_raw: ?*anyopaque,
    dx: u32,
    dy: u32,
    dz: u32,
    copy_src: ?*anyopaque,
    copy_src_off: u64,
    copy_dst: ?*anyopaque,
    copy_dst_off: u64,
    copy_size: u64,
) callconv(.c) void {
    var bg_ptrs = [_]?*anyopaque{bg0_raw};
    compute_fast.computeDispatchFlushDirect(
        q_raw,
        pipe_raw,
        &bg_ptrs,
        bg_ptrs.len,
        dx,
        dy,
        dz,
        copy_src,
        copy_src_off,
        copy_dst,
        copy_dst_off,
        copy_size,
        null,
    );
}

fn ensureNativeLibraryLocked() bool {
    if (g_library_ready) return !g_library_failed;
    g_library_ready = true;
    g_native_lib = loader.openDropinTargetLibrary() catch {
        g_library_failed = true;
        setLastError(.library_open_failed);
        return false;
    };
    g_library_failed = false;
    return true;
}

fn ensureNativeLibrary() bool {
    g_state_lock.lock();
    defer g_state_lock.unlock();
    return ensureNativeLibraryLocked();
}

fn abortMissingRequiredSymbol(symbol_name: []const u8) noreturn {
    setLastError(.symbol_missing);
    std.debug.panic("missing required WebGPU symbol: {s}", .{symbol_name});
}

pub export fn doeWgpuDropinAbortMissingRequiredSymbol(name: abi_base.WGPUStringView) callconv(.c) noreturn {
    const symbol_name = symbolNameSlice(name) orelse {
        setLastError(.invalid_symbol_name);
        std.debug.panic("missing required WebGPU symbol: <invalid>", .{});
    };
    abortMissingRequiredSymbol(symbol_name);
}

fn routeAndRecordForName(
    symbol_name: []const u8,
    route: dropin_router.RouteDecision,
    resolved: bool,
) void {
    const diagnostics_owner = if (comptime build_options.experimental_c_dropin_policy)
        dropin_runtime_config.symbolOwnerForName(symbol_name)
    else
        dropin_proc_manifest.manifestOwnerForSymbol(symbol_name) orelse
            dropin_runtime_config.symbolOwnerForName(symbol_name);
    dropin_diagnostics.record(
        symbol_name,
        dropin_symbol_ownership.symbol_owner_name(diagnostics_owner),
        resolved,
        route.fallback_used,
    );
}

fn nativeFromSymbol(comptime FnType: type, comptime symbol_name: [:0]const u8) ?FnType {
    // Direct lookup without re-acquiring g_state_lock (caller already holds it).
    if (g_native_lib == null) return null;
    var lib = g_native_lib.?;
    return lib.lookup(FnType, symbol_name);
}

/// Resolve a standard wgpu* symbol to the corresponding doeNative* implementation.
/// Returns null for symbols not yet implemented natively.
fn resolveDoeNativeProc(comptime FnType: type, comptime symbol_name: [:0]const u8) ?FnType {
    return dropin_proc_manifest.resolveDoeNativeProc(FnType, symbol_name);
}

pub fn loadRequiredProc(comptime FnType: type, comptime symbol_name: [:0]const u8) FnType {
    const Cache = struct {
        const symbol_key = symbol_name;
        var initialized = std.atomic.Value(u8).init(0);
        var proc: ?FnType = null;
    };
    _ = Cache.symbol_key;

    if (Cache.initialized.load(.acquire) != 0) {
        return Cache.proc.?;
    }

    g_state_lock.lock();
    defer g_state_lock.unlock();

    if (Cache.initialized.load(.monotonic) != 0) {
        return Cache.proc.?;
    }
    const route = symbolRouteForName(symbol_name);

    // For doe_metal or shared owner, try linked native implementations first (no sidecar needed).
    if (route.owner == .doe_metal or route.owner == .shared) {
        if (resolveDoeNativeProc(FnType, symbol_name)) |proc| {
            routeAndRecordForName(symbol_name, route, true);
            Cache.proc = proc;
            Cache.initialized.store(1, .release);
            setLastError(.ok);
            return proc;
        }
    }

    if (!ensureNativeLibraryLocked()) {
        routeAndRecordForName(symbol_name, route, false);
        abortMissingRequiredSymbol(symbol_name);
    }

    const resolved = switch (route.owner) {
        .dawn_delegate, .shared => nativeFromSymbol(FnType, symbol_name),
        .doe_metal, .doe_vulkan, .doe_d3d12 => if (route.fallback_used) nativeFromSymbol(FnType, symbol_name) else null,
    };

    routeAndRecordForName(symbol_name, route, resolved != null);
    if (resolved) |proc| {
        Cache.proc = proc;
        Cache.initialized.store(1, .release);
        setLastError(.ok);
        return proc;
    }
    abortMissingRequiredSymbol(symbol_name);
}

fn loadOptionalProc(comptime FnType: type, comptime symbol_name: [:0]const u8) ?FnType {
    const Cache = struct {
        const symbol_key = symbol_name;
        var initialized = std.atomic.Value(u8).init(0);
        var proc: ?FnType = null;
    };
    _ = Cache.symbol_key;

    if (Cache.initialized.load(.acquire) != 0) {
        return Cache.proc;
    }

    g_state_lock.lock();
    defer g_state_lock.unlock();

    if (Cache.initialized.load(.monotonic) != 0) {
        return Cache.proc;
    }

    if (!ensureNativeLibraryLocked()) return null;
    var lib = g_native_lib.?;
    Cache.proc = lib.lookup(FnType, symbol_name);
    Cache.initialized.store(1, .release);
    return Cache.proc;
}

fn fnPtr(value: anytype) p1_capability_procs.WGPUProc {
    return @as(p1_capability_procs.WGPUProc, @ptrCast(value));
}

fn symbolViewEq(name: abi_base.WGPUStringView, comptime expected: []const u8) bool {
    const data = name.data orelse return false;
    if (name.length == abi_base.WGPU_STRLEN) {
        const z = @as([*:0]const u8, @ptrCast(data));
        return std.mem.eql(u8, std.mem.span(z), expected);
    }
    if (name.length != expected.len) return false;
    return std.mem.eql(u8, data[0..name.length], expected);
}

fn toZeroTerminatedSymbolName(
    name: abi_base.WGPUStringView,
    buffer: *[256]u8,
) ?[:0]const u8 {
    const data = name.data orelse {
        setLastError(.invalid_symbol_name);
        return null;
    };

    if (name.length == abi_base.WGPU_STRLEN) {
        const z = @as([*:0]const u8, @ptrCast(data));
        return std.mem.span(z);
    }

    if (name.length == 0) {
        setLastError(.invalid_symbol_name);
        return null;
    }

    if (name.length >= buffer.len) {
        setLastError(.symbol_name_too_long);
        return null;
    }

    @memcpy(buffer[0..name.length], data[0..name.length]);
    buffer[name.length] = 0;
    return buffer[0..name.length :0];
}

// Public ABI identity projects the explicitly owned procedure modules,
// including browser procedures intentionally available only by lookup.
fn resolveLocalProc(name: abi_base.WGPUStringView) p1_capability_procs.WGPUProc {
    @setEvalBranchQuota(20_000);
    if (symbolViewEq(name, "wgpuGetProcAddress")) return fnPtr(&wgpuGetProcAddress);
    inline for (.{ dropin_abi_procs, dropin_ext_a.exports, dropin_ext_b, dropin_ext_c, dropin_browser_shared_memory }) |module| {
        inline for (comptime std.meta.declarations(module)) |decl| {
            if (comptime std.mem.startsWith(u8, decl.name, "wgpu") and @typeInfo(@TypeOf(@field(module, decl.name))) == .@"fn") {
                if (symbolViewEq(name, decl.name)) return fnPtr(&@field(module, decl.name));
            }
        }
    }
    return null;
}

fn resolveNativeProc(name: abi_base.WGPUStringView) p1_capability_procs.WGPUProc {
    if (!ensureNativeLibrary()) return null;

    if (loadOptionalProc(p1_capability_procs.FnGetProcAddress, "wgpuGetProcAddress")) |native_get_proc_address| {
        const resolved = native_get_proc_address(name);
        if (resolved != null) return resolved;
    }

    var buffer: [256]u8 = undefined;
    const zname = toZeroTerminatedSymbolName(name, &buffer) orelse return null;

    g_state_lock.lock();
    defer g_state_lock.unlock();

    if (!ensureNativeLibraryLocked()) return null;
    var lib = g_native_lib.?;
    const resolved = lib.lookup(WgpuAnyProc, zname) orelse return null;
    return @as(p1_capability_procs.WGPUProc, resolved);
}

fn resolveImplementationProc(name: abi_base.WGPUStringView) callconv(.c) p1_capability_procs.WGPUProc {
    const route = symbolRouteForView(name);
    const symbol_name = symbolNameSlice(name) orelse {
        setLastError(.invalid_symbol_name);
        return null;
    };

    if (std.mem.indexOfScalar(u8, symbol_name, 0) != null) {
        setLastError(.invalid_symbol_name);
        return null;
    }

    // Linked native pointers come only from the typed procedure manifest.
    if (route.owner == .doe_vulkan or route.owner == .doe_d3d12) {
        if (!route.fallback_used) {
            routeAndRecordForName(symbol_name, route, false);
            setLastError(.symbol_missing);
            return null;
        }
    }

    if (dropin_proc_manifest.resolveNativeProcByName(symbol_name)) |proc| {
        routeAndRecordForName(symbol_name, route, route.owner == .dawn_delegate or route.owner == .shared or route.fallback_used);
        setLastError(.ok);
        return proc;
    }

    if (resolveNativeProc(name)) |proc| {
        routeAndRecordForName(symbol_name, route, true);
        setLastError(.ok);
        return proc;
    }

    routeAndRecordForName(symbol_name, route, false);
    setLastError(.symbol_missing);
    return null;
}
// Wrapper-only C seam avoids an import cycle with public ABI modules.
// ELF/Mach-O hide it. COFF requires default visibility for the internal
// bridge export recorded in tools/bridge_manifests/dropin_bridge_manifest.json.
comptime {
    @export(&resolveImplementationProc, .{ .name = "doeWgpuDropinGetNativeProc", .visibility = if (@import("builtin").os.tag == .windows) .default else .hidden });
}

pub export fn wgpuGetProcAddress(name: abi_base.WGPUStringView) callconv(.c) p1_capability_procs.WGPUProc {
    const route = symbolRouteForView(name);
    const symbol_name = symbolNameSlice(name) orelse {
        setLastError(.invalid_symbol_name);
        return null;
    };

    if (std.mem.indexOfScalar(u8, symbol_name, 0) != null) {
        setLastError(.invalid_symbol_name);
        return null;
    }

    if (route.owner == .doe_vulkan or route.owner == .doe_d3d12) {
        if (!route.fallback_used) {
            routeAndRecordForName(symbol_name, route, false);
            setLastError(.symbol_missing);
            return null;
        }
    }

    if (resolveLocalProc(name)) |proc| {
        routeAndRecordForName(symbol_name, route, route.owner == .dawn_delegate or route.owner == .shared or route.fallback_used);
        setLastError(.ok);
        return proc;
    }

    routeAndRecordForName(symbol_name, route, false);
    setLastError(.symbol_missing);
    return null;
}

test "procedure identity separates public create wrapper from native implementation" {
    const N = @import("../native/mod.zig");
    const symbol: [:0]const u8 = "wgpuDeviceCreateBuffer";
    const view = abi_base.WGPUStringView{
        .data = symbol.ptr,
        .length = abi_base.WGPU_STRLEN,
    };

    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuDeviceCreateBuffer), resolveLocalProc(view));
    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuDeviceCreateBuffer), wgpuGetProcAddress(view));
    try std.testing.expectEqual(fnPtr(&N.doeNativeDeviceCreateBuffer), resolveImplementationProc(view));
}

test "procedure identity separates public compute wrapper from native implementation" {
    const N = @import("../native/mod.zig");
    const symbol: [:0]const u8 = "wgpuCommandEncoderBeginComputePass";
    const view = abi_base.WGPUStringView{
        .data = symbol.ptr,
        .length = abi_base.WGPU_STRLEN,
    };

    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuCommandEncoderBeginComputePass), resolveLocalProc(view));
    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuCommandEncoderBeginComputePass), wgpuGetProcAddress(view));
    try std.testing.expectEqual(fnPtr(&N.doeNativeCommandEncoderBeginComputePass), resolveImplementationProc(view));
}

test "procedure identity keeps shader compilation info local" {
    const symbol: [:0]const u8 = "wgpuShaderModuleGetCompilationInfo";
    const view = abi_base.WGPUStringView{
        .data = symbol.ptr,
        .length = abi_base.WGPU_STRLEN,
    };

    try std.testing.expectEqual(fnPtr(&dropin_ext_b.wgpuShaderModuleGetCompilationInfo), resolveLocalProc(view));
    try std.testing.expectEqual(fnPtr(&dropin_ext_b.wgpuShaderModuleGetCompilationInfo), wgpuGetProcAddress(view));
}

test "procedure identity keeps texture validation wrapper public" {
    const symbol: [:0]const u8 = "wgpuDeviceValidateTextureDescriptor";
    const view = abi_base.WGPUStringView{
        .data = symbol.ptr,
        .length = abi_base.WGPU_STRLEN,
    };

    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuDeviceValidateTextureDescriptor), resolveLocalProc(view));
    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuDeviceValidateTextureDescriptor), wgpuGetProcAddress(view));
}

test "procedure identity preserves browser procedure-only functions" {
    const symbols = .{
        .{ "wgpuQueueCopyExternalImageToTexture", &dropin_browser_shared_memory.wgpuQueueCopyExternalImageToTexture },
        .{ "wgpuDeviceImportSharedTextureMemory", &dropin_browser_shared_memory.wgpuDeviceImportSharedTextureMemory },
        .{ "wgpuSharedTextureMemoryBeginAccess", &dropin_browser_shared_memory.wgpuSharedTextureMemoryBeginAccess },
        .{ "wgpuSharedTextureMemoryEndAccess", &dropin_browser_shared_memory.wgpuSharedTextureMemoryEndAccess },
        .{ "wgpuDeviceImportSharedBufferMemory", &dropin_browser_shared_memory.wgpuDeviceImportSharedBufferMemory },
        .{ "wgpuSharedBufferMemoryBeginAccess", &dropin_browser_shared_memory.wgpuSharedBufferMemoryBeginAccess },
        .{ "wgpuSharedFenceExportInfo", &dropin_browser_shared_memory.wgpuSharedFenceExportInfo },
        .{ "wgpuDeviceCreateErrorTexture", &dropin_browser_shared_memory.wgpuDeviceCreateErrorTexture },
        .{ "wgpuDeviceCreateErrorBuffer", &dropin_browser_shared_memory.wgpuDeviceCreateErrorBuffer },
    };

    inline for (symbols) |entry| {
        const symbol: [:0]const u8 = entry[0];
        const view = abi_base.WGPUStringView{
            .data = symbol.ptr,
            .length = abi_base.WGPU_STRLEN,
        };
        try std.testing.expectEqual(fnPtr(entry[1]), resolveLocalProc(view));
        try std.testing.expectEqual(fnPtr(entry[1]), wgpuGetProcAddress(view));
    }
}

comptime {
    _ = @import("dropin_abi_procs.zig");
}

test "procedure identity preserves standard exports and wrapper dispatch without recursion" {
    // Focused filters still link the real peers of the render-control C seam.
    comptime {
        const render = @import("../native/render/doe_render_native.zig");
        _ = &render.doeNativeRenderPassRecordViewportState;
        _ = &render.doeNativeRenderPassRecordScissorState;
        _ = &render.doeNativeRenderPassRecordBlendConstantState;
        _ = &render.doeNativeRenderPassRecordStencilReferenceState;
    }
    const entries = .{
        .{ "wgpuGetProcAddress", &wgpuGetProcAddress },
        .{ "wgpuRenderPassEncoderDrawIndexed", &dropin_abi_procs.wgpuRenderPassEncoderDrawIndexed },
        .{ "wgpuRenderPassEncoderSetImmediates", &dropin_ext_b.wgpuRenderPassEncoderSetImmediates },
        .{ "wgpuExternalTextureSetLabel", &dropin_ext_c.wgpuExternalTextureSetLabel },
    };
    inline for (entries) |entry| {
        const name: [:0]const u8 = entry[0];
        try std.testing.expectEqual(fnPtr(entry[1]), wgpuGetProcAddress(.{ .data = name.ptr, .length = name.len }));
        try std.testing.expectEqual(fnPtr(entry[1]), wgpuGetProcAddress(.{ .data = name.ptr, .length = abi_base.WGPU_STRLEN }));
    }
    // These null-safe calls traverse the actual wrapper-only resolver seam.
    dropin_abi_procs.wgpuRenderPassEncoderDrawIndexed(null, 1, 1, 0, 0, 0);
    dropin_abi_procs.wgpuDeviceValidateTextureDescriptor(null, null);
}

test "procedure identity rejects unknown private and malformed names" {
    try std.testing.expect(wgpuGetProcAddress(.{ .data = null, .length = 0 }) == null);
    const name = "wgpuRenderPassEncoderDrawIndexed";
    try std.testing.expect(wgpuGetProcAddress(.{ .data = name.ptr, .length = 0 }) == null);
    try std.testing.expect(wgpuGetProcAddress(.{ .data = name.ptr, .length = name.len - 1 }) == null);
    const embedded = "wgpuRenderPassEncoderDrawIndexed\x00suffix";
    try std.testing.expect(wgpuGetProcAddress(.{ .data = embedded.ptr, .length = embedded.len }) == null);
    try std.testing.expect(resolveImplementationProc(.{ .data = embedded.ptr, .length = embedded.len }) == null);
    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuRenderPassEncoderDrawIndexed), wgpuGetProcAddress(.{ .data = embedded.ptr, .length = abi_base.WGPU_STRLEN }));
    inline for (.{ "wgpuDefinitelyMissing", "doe_vk_context_create", "doeWgpuDropinGetNativeProc", "doeNativeDeviceDisablePersistentCache" }) |unknown| {
        try std.testing.expect(wgpuGetProcAddress(.{ .data = unknown.ptr, .length = unknown.len }) == null);
    }
    const trailing = "wgpuRenderPassEncoderDrawIndexedSuffix";
    try std.testing.expectEqual(fnPtr(&dropin_abi_procs.wgpuRenderPassEncoderDrawIndexed), wgpuGetProcAddress(.{ .data = trailing.ptr, .length = name.len }));
}
