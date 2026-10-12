const std = @import("std");
const doe = @import("doe");
const wgsl = doe.compiler.wgsl();
const runtime_compile = doe.compiler.wgsl_runtime.compile();

const MAX_SOURCE_BYTES: usize = 2 * 1024 * 1024;
const MAX_OVERRIDES: usize = 64;

const CompilerMode = enum {
    default,
    vulkan_compute_runtime,
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    var shader_path: ?[]const u8 = null;
    var out_path: ?[]const u8 = null;
    var mode: CompilerMode = .default;
    var overrides: [MAX_OVERRIDES]wgsl.ir.OverrideEntry = undefined;
    var override_count: usize = 0;

    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        if (std.mem.eql(u8, args[i], "--shader-path") and i + 1 < args.len) {
            i += 1;
            shader_path = args[i];
        } else if (std.mem.eql(u8, args[i], "--out") and i + 1 < args.len) {
            i += 1;
            out_path = args[i];
        } else if (std.mem.eql(u8, args[i], "--mode") and i + 1 < args.len) {
            i += 1;
            if (std.mem.eql(u8, args[i], "default")) {
                mode = .default;
            } else if (std.mem.eql(u8, args[i], "vulkan-compute-runtime")) {
                mode = .vulkan_compute_runtime;
            } else {
                try printUsage();
                return error.InvalidArgument;
            }
        } else if (std.mem.eql(u8, args[i], "--overrides") and i + 1 < args.len) {
            i += 1;
            var values = std.mem.splitScalar(u8, args[i], ',');
            while (values.next()) |value| {
                if (override_count == MAX_OVERRIDES) return error.InvalidArgument;
                const equals = std.mem.indexOfScalar(u8, value, '=') orelse return error.InvalidArgument;
                if (equals == 0 or equals + 1 == value.len) return error.InvalidArgument;
                const key = value[0..equals];
                for (overrides[0..override_count]) |entry| {
                    if (std.mem.eql(u8, entry.key, key)) return error.InvalidArgument;
                }
                const number = std.fmt.parseFloat(f64, value[equals + 1 ..]) catch return error.InvalidArgument;
                if (!std.math.isFinite(number)) return error.InvalidArgument;
                overrides[override_count] = .{ .key = key, .value = number };
                override_count += 1;
            }
        } else if (std.mem.eql(u8, args[i], "--help")) {
            try printUsage();
            return;
        } else {
            try printUsage();
            return error.InvalidArgument;
        }
    }
    if (override_count > 0 and mode != .vulkan_compute_runtime) return error.InvalidArgument;

    const shader_path_value = shader_path orelse {
        try printUsage();
        return error.InvalidArgument;
    };

    const source = try std.fs.cwd().readFileAlloc(allocator, shader_path_value, MAX_SOURCE_BYTES);
    defer allocator.free(source);

    const out_buf = try allocator.alloc(u8, wgsl.MAX_SPIRV_OUTPUT);
    defer allocator.free(out_buf);

    var diagnostic = wgsl.analysis.Diagnostic{};
    const spirv_len = translate(allocator, source, out_buf, mode, overrides[0..override_count], &diagnostic) catch |err| {
        const stderr = std.fs.File.stderr().deprecatedWriter();
        const detail = if (mode == .vulkan_compute_runtime) diagnostic.lastErrorMessage() else wgsl.lastErrorMessage();
        if (detail.len > 0) {
            try stderr.print("{s}: {s}\n", .{ @errorName(err), detail });
        } else {
            try stderr.print("{s}\n", .{@errorName(err)});
        }
        return err;
    };

    if (out_path) |path| {
        const file = try std.fs.cwd().createFile(path, .{ .truncate = true });
        defer file.close();
        try file.writeAll(out_buf[0..spirv_len]);
        return;
    }

    try std.fs.File.stdout().writeAll(out_buf[0..spirv_len]);
}

fn translate(
    allocator: std.mem.Allocator,
    source: []const u8,
    out_buf: []u8,
    mode: CompilerMode,
    overrides: []const wgsl.ir.OverrideEntry,
    diagnostic: *wgsl.analysis.Diagnostic,
) wgsl.TranslateError!usize {
    return switch (mode) {
        .default => wgsl.translateToSpirv(allocator, source, out_buf),
        .vulkan_compute_runtime => blk: {
            var result = try runtime_compile.compute.translateToSpirvForVulkanComputeRuntimeWithOverridesWithDiagnostic(allocator, source, out_buf, overrides.ptr, overrides.len, diagnostic);
            defer result.info.deinit(allocator);
            break :blk result.len;
        },
    };
}

fn printUsage() !void {
    try std.fs.File.stderr().deprecatedWriter().writeAll(
        \\doe-emit-spirv --shader-path <path> [--out <path>] [--mode default|vulkan-compute-runtime] [--overrides NAME=VALUE,...]
        \\
        \\Translate one WGSL shader to SPIR-V using Doe's WGSL compiler.
        \\If --out is omitted, writes SPIR-V bytes to stdout.
        \\Overrides require vulkan-compute-runtime mode; boolean overrides use 0/1.
        \\
    );
}
