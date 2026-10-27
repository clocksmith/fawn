//! Disk-free native DXC compilation through the official dxcapi.h COM ABI.
//! Loads only the compiler adjacent to this provider image; no executable,
//! include handler, output filename, environment cache, or generated file path.
const std = @import("std");
const builtin = @import("builtin");
const windows = std.os.windows;
const api_call: std.builtin.CallingConvention = if (builtin.os.tag == .windows) .winapi else .c;
const Guid = extern struct { a: u32, b: u16, c: u16, d: [8]u8 };
const compiler_class = Guid{ .a = 0x73e22d93, .b = 0xe6ce, .c = 0x47f3, .d = .{ 0xb5, 0xbf, 0xf0, 0x66, 0x4f, 0x39, 0xc1, 0xb0 } };
const compiler_iid = Guid{ .a = 0x228b4687, .b = 0x5a6a, .c = 0x4730, .d = .{ 0x90, 0x0c, 0x97, 0x02, 0xb2, 0x20, 0x3f, 0x54 } };
const result_iid = Guid{ .a = 0x58346cda, .b = 0xdde7, .c = 0x4497, .d = .{ 0x94, 0x61, 0x6f, 0x87, 0xaf, 0x5e, 0x06, 0x59 } };
const Buffer = extern struct { ptr: *const anyopaque, size: usize, encoding: u32 = 65001 };
const UnknownVTable = extern struct {
    query: *const fn (*anyopaque, *const Guid, *?*anyopaque) callconv(api_call) i32,
    retain: *const fn (*anyopaque) callconv(api_call) u32,
    release: *const fn (*anyopaque) callconv(api_call) u32,
};
const Compiler = extern struct { vtable: *const CompilerVTablePrefix };
const CompilerVTablePrefix = extern struct {
    unknown: UnknownVTable,
    compile: *const fn (*Compiler, *const Buffer, [*]const [*:0]const u16, u32, ?*anyopaque, *const Guid, *?*Result) callconv(api_call) i32,
};
const Result = extern struct { vtable: *const ResultVTablePrefix };
const ResultVTablePrefix = extern struct {
    unknown: UnknownVTable,
    status: *const fn (*Result, *i32) callconv(api_call) i32,
    object: *const fn (*Result, *?*Blob) callconv(api_call) i32,
    errors: *const fn (*Result, *?*Blob) callconv(api_call) i32,
};
const Blob = extern struct { vtable: *const BlobVTable };
const BlobVTable = extern struct {
    unknown: UnknownVTable,
    data: *const fn (*Blob) callconv(api_call) ?[*]const u8,
    size: *const fn (*Blob) callconv(api_call) usize,
};
const CreateInstance = *const fn (*const Guid, *const Guid, *?*Compiler) callconv(api_call) i32;
const max_object_bytes = 64 * 1024 * 1024;

extern "kernel32" fn GetModuleHandleExW(u32, [*:0]const u16, *windows.HMODULE) callconv(.winapi) windows.BOOL;

pub fn compile(allocator: std.mem.Allocator, source: []const u8, entry: []const u8) ![]u8 {
    if (comptime builtin.os.tag != .windows) return error.ShaderToolchainUnavailable;
    var image: windows.HMODULE = undefined;
    // FROM_ADDRESS | UNCHANGED_REFCOUNT: anchor belongs to the provider, not
    // the browser executable and not a basename discovered through PATH/CWD.
    if (GetModuleHandleExW(6, @ptrFromInt(@intFromPtr(&compile)), &image) == 0) return error.ShaderToolchainUnavailable;
    var path: [32768]u16 = undefined;
    const length = windows.kernel32.GetModuleFileNameW(image, &path, path.len);
    if (length == 0 or length >= path.len) return error.ShaderToolchainUnavailable;
    const compiler_path = try siblingCompilerPath(allocator, path[0..length]);
    defer allocator.free(compiler_path);
    const module = windows.kernel32.LoadLibraryExW(compiler_path, null, 0x1100) orelse return error.ShaderToolchainUnavailable;
    defer _ = windows.kernel32.FreeLibrary(module);
    const create: CreateInstance = @ptrCast(windows.kernel32.GetProcAddress(module, "DxcCreateInstance") orelse return error.ShaderToolchainUnavailable);
    var instance: ?*Compiler = null;
    const created = create(&compiler_class, &compiler_iid, &instance);
    defer if (instance) |value| {
        _ = value.vtable.unknown.release(value);
    };
    if (created < 0 or instance == null) return error.ShaderToolchainUnavailable;
    return compileWithInstance(allocator, instance.?, source, entry);
}

fn siblingCompilerPath(allocator: std.mem.Allocator, image_path: []const u16) ![:0]u16 {
    const slash = std.mem.lastIndexOfScalar(u16, image_path, '\\') orelse return error.ShaderToolchainUnavailable;
    const name = std.unicode.utf8ToUtf16LeStringLiteral("dxcompiler.dll");
    const length = std.math.add(usize, slash + 1, name.len) catch return error.ShaderToolchainUnavailable;
    const path = try allocator.allocSentinel(u16, length, 0);
    @memcpy(path[0 .. slash + 1], image_path[0 .. slash + 1]);
    @memcpy(path[slash + 1 ..], name);
    return path;
}

fn compileWithInstance(allocator: std.mem.Allocator, instance: *Compiler, source: []const u8, entry: []const u8) ![]u8 {
    if (entry.len == 0 or std.mem.indexOfScalar(u8, entry, 0) != null) return error.InvalidArgument;
    const wide_entry = try std.unicode.utf8ToUtf16LeAllocZ(allocator, entry);
    defer allocator.free(wide_entry);
    const wide = std.unicode.utf8ToUtf16LeStringLiteral;
    const arguments = [_][*:0]const u16{ wide("-T"), wide("cs_6_0"), wide("-E"), wide_entry };
    const input = Buffer{ .ptr = source.ptr, .size = source.len };
    var result: ?*Result = null;
    const compiled = instance.vtable.compile(instance, &input, &arguments, arguments.len, null, &result_iid, &result);
    defer if (result) |value| {
        _ = value.vtable.unknown.release(value);
    };
    if (compiled < 0 or result == null) return error.ShaderCompileFailed;
    var status: i32 = -1;
    if (result.?.vtable.status(result.?, &status) < 0 or status < 0) return error.ShaderCompileFailed;
    var object: ?*Blob = null;
    const fetched = result.?.vtable.object(result.?, &object);
    defer if (object) |value| {
        _ = value.vtable.unknown.release(value);
    };
    if (fetched < 0 or object == null) return error.ShaderCompileFailed;
    const length = object.?.vtable.size(object.?);
    if (length == 0 or length > max_object_bytes) return error.ShaderCompileFailed;
    const bytes = object.?.vtable.data(object.?) orelse return error.ShaderCompileFailed;
    return allocator.dupe(u8, bytes[0..length]);
}

// CPU-only COM boundary controls. The fake owns no files or native GPU objects.
const Fake = struct {
    compiler: Compiler = .{ .vtable = &compiler_vtable },
    result: Result = .{ .vtable = &result_vtable },
    blob: Blob = .{ .vtable = &blob_vtable },
    compile_hr: i32 = 0,
    status_hr: i32 = 0,
    status_value: i32 = 0,
    object_hr: i32 = 0,
    object_present: bool = true,
    data_present: bool = true,
    length: usize = 4,
    released_results: usize = 0,
    released_blobs: usize = 0,
    input_valid: bool = false,
    const unknown = UnknownVTable{ .query = query, .retain = retain, .release = releaseResult };
    const compiler_vtable = CompilerVTablePrefix{ .unknown = unknown, .compile = invoke };
    const result_vtable = ResultVTablePrefix{ .unknown = unknown, .status = status, .object = object, .errors = object };
    const blob_vtable = BlobVTable{ .unknown = .{ .query = query, .retain = retain, .release = releaseBlob }, .data = data, .size = size };
    fn query(_: *anyopaque, _: *const Guid, out: *?*anyopaque) callconv(api_call) i32 {
        out.* = null;
        return -1;
    }
    fn retain(_: *anyopaque) callconv(api_call) u32 {
        return 1;
    }
    fn releaseResult(raw: *anyopaque) callconv(api_call) u32 {
        const self: *Fake = @fieldParentPtr("result", @as(*Result, @ptrCast(@alignCast(raw))));
        self.released_results += 1;
        return 0;
    }
    fn releaseBlob(raw: *anyopaque) callconv(api_call) u32 {
        const self: *Fake = @fieldParentPtr("blob", @as(*Blob, @ptrCast(@alignCast(raw))));
        self.released_blobs += 1;
        return 0;
    }
    fn invoke(instance: *Compiler, input: *const Buffer, args: [*]const [*:0]const u16, count: u32, include: ?*anyopaque, iid: *const Guid, out: *?*Result) callconv(api_call) i32 {
        const self: *Fake = @fieldParentPtr("compiler", instance);
        const wide = std.unicode.utf8ToUtf16LeStringLiteral;
        self.input_valid = input.size == 6 and input.encoding == 65001 and
            std.mem.eql(u8, @as([*]const u8, @ptrCast(input.ptr))[0..input.size], "source") and
            count == 4 and include == null and std.meta.eql(iid.*, result_iid) and
            std.mem.eql(u16, std.mem.span(args[0]), wide("-T")) and
            std.mem.eql(u16, std.mem.span(args[1]), wide("cs_6_0")) and
            std.mem.eql(u16, std.mem.span(args[2]), wide("-E")) and
            std.mem.eql(u16, std.mem.span(args[3]), wide("chosen_entry"));
        out.* = &self.result;
        return self.compile_hr;
    }
    fn status(result: *Result, out: *i32) callconv(api_call) i32 {
        const self: *Fake = @fieldParentPtr("result", result);
        out.* = self.status_value;
        return self.status_hr;
    }
    fn object(result: *Result, out: *?*Blob) callconv(api_call) i32 {
        const self: *Fake = @fieldParentPtr("result", result);
        out.* = if (self.object_present) &self.blob else null;
        return self.object_hr;
    }
    fn data(blob: *Blob) callconv(api_call) ?[*]const u8 {
        const self: *Fake = @fieldParentPtr("blob", blob);
        return if (self.data_present) "DXBC".ptr else null;
    }
    fn size(blob: *Blob) callconv(api_call) usize {
        const self: *Fake = @fieldParentPtr("blob", blob);
        return self.length;
    }
};

test "device persistent cache policy memory DXC uses exact selected entry and owned output" {
    var fake = Fake{};
    const bytes = try compileWithInstance(std.testing.allocator, &fake.compiler, "source", "chosen_entry");
    defer std.testing.allocator.free(bytes);
    try std.testing.expect(fake.input_valid);
    try std.testing.expectEqualStrings("DXBC", bytes);
    try std.testing.expectEqual(@as(usize, 1), fake.released_results);
    try std.testing.expectEqual(@as(usize, 1), fake.released_blobs);
}

test "device persistent cache policy memory DXC rejects failures and releases partial COM outputs" {
    const cases = [_]Fake{
        .{ .compile_hr = -1 }, .{ .status_hr = -1 },                .{ .status_value = -1 },
        .{ .object_hr = -1 },  .{ .object_present = false },        .{ .data_present = false },
        .{ .length = 0 },      .{ .length = max_object_bytes + 1 },
    };
    for (cases, 0..) |value, index| {
        var fake = value;
        try std.testing.expectError(error.ShaderCompileFailed, compileWithInstance(std.testing.allocator, &fake.compiler, "source", "chosen_entry"));
        try std.testing.expectEqual(@as(usize, 1), fake.released_results);
        try std.testing.expectEqual(@as(usize, if (index >= 3 and index != 4) 1 else 0), fake.released_blobs);
    }
    var fake = Fake{};
    try std.testing.expectError(error.InvalidArgument, compileWithInstance(std.testing.allocator, &fake.compiler, "source", ""));
    try std.testing.expectError(error.InvalidArgument, compileWithInstance(std.testing.allocator, &fake.compiler, "source", "chosen_entry\x00other"));
    try std.testing.expectEqual(@as(usize, 0), fake.released_results);
}

test "device persistent cache policy memory DXC compiler path stays provider adjacent" {
    const wide = std.unicode.utf8ToUtf16LeStringLiteral;
    const path = try siblingCompilerPath(std.testing.allocator, wide("C:\\private lib\\Δ\\webgpu_doe_full.dll"));
    defer std.testing.allocator.free(path);
    try std.testing.expectEqualSlices(u16, wide("C:\\private lib\\Δ\\dxcompiler.dll"), path);
    try std.testing.expectError(error.ShaderToolchainUnavailable, siblingCompilerPath(std.testing.allocator, wide("webgpu_doe_full.dll")));
}
