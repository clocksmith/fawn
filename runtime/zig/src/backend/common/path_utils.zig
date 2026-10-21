const std = @import("std");
const builtin = @import("builtin");

/// Only absence permits trying another candidate; access failures retain their cause.
pub fn file_exists(path: []const u8) !bool {
    // An empty candidate names no file; Windows otherwise probes the current directory.
    if (path.len == 0) return false;
    // Zig 0.15.2 converts into a fixed Windows path buffer before checking its
    // length. Count UTF-16 units first for inputs which could exceed that buffer;
    // a byte-only cap would incorrectly reject valid multibyte paths.
    if (builtin.os.tag == .windows and path.len > std.os.windows.PATH_MAX_WIDE) {
        const units = std.unicode.calcWtf16LeLen(path) catch return error.InvalidWtf8;
        if (units > std.os.windows.PATH_MAX_WIDE) return error.NameTooLong;
    }
    std.fs.cwd().access(path, .{}) catch |err| switch (err) {
        error.FileNotFound => return false,
        else => return err,
    };
    return true;
}

test "path probe distinguishes missing paths from invalid paths" {
    try std.testing.expect(!try file_exists(""));
    var temporary = std.testing.tmpDir(.{});
    defer temporary.cleanup();
    const root = try temporary.dir.realpathAlloc(std.testing.allocator, ".");
    defer std.testing.allocator.free(root);
    try std.testing.expect(try file_exists(root));
    const missing = try std.fs.path.join(std.testing.allocator, &.{ root, "missing" });
    defer std.testing.allocator.free(missing);
    try std.testing.expect(!try file_exists(missing));
    const too_long = [_]u8{'x'} ** (std.fs.max_path_bytes + 1);
    try std.testing.expectError(error.NameTooLong, file_exists(&too_long));
}

test "Windows path probe rejects excess UTF-16 units before conversion" {
    if (builtin.os.tag != .windows) return error.SkipZigTest;
    const too_wide = [_]u8{'x'} ** (std.os.windows.PATH_MAX_WIDE + 1);
    try std.testing.expectError(error.NameTooLong, file_exists(&too_wide));
    var invalid = too_wide;
    invalid[0] = 0xff;
    try std.testing.expectError(error.InvalidWtf8, file_exists(&invalid));
}
