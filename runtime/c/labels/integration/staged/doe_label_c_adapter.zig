//! Typed copied-byte/borrowed-view seam. No map or object policy lives here.
const std = @import("std");
const c = @cImport({
    @cInclude("doe_labels_native_private.h");
});

fn fatal(operation: []const u8, status: c.doe_label_status) noreturn {
    std.debug.panic("C labels {s}: unexpected status {d}", .{ operation, status });
}

pub fn set(handle: ?*anyopaque, data: ?[*]const u8, len: usize) void {
    switch (c.doe_labels_native_set(handle, data, len)) {
        c.DOE_LABEL_OK, c.DOE_LABEL_OUT_OF_MEMORY, c.DOE_LABEL_LAYOUT_OVERFLOW => {},
        else => |status| fatal("set", status),
    }
}

pub fn get(handle: ?*anyopaque) []const u8 {
    var view: c.doe_label_view = undefined;
    const status = c.doe_labels_native_get(handle, &view);
    if (status != c.DOE_LABEL_OK) fatal("get", status);
    return if (view.length == 0) "" else view.data[0..view.length];
}

pub fn remove(handle: ?*anyopaque) void {
    const status = c.doe_labels_native_remove(handle);
    if (status != c.DOE_LABEL_OK) fatal("remove", status);
}

pub export fn doeNativeObjectSetLabel(handle: ?*anyopaque, data: ?[*]const u8, len: usize) callconv(.c) void {
    set(handle, data, len);
}

pub export fn doeNativeObjectGetLabel(handle: ?*anyopaque, out_len: ?*usize) callconv(.c) ?[*]const u8 {
    const label = get(handle);
    if (out_len) |length| length.* = label.len;
    return if (label.len == 0) null else label.ptr;
}

pub export fn doeNativeObjectRemoveLabel(handle: ?*anyopaque) callconv(.c) void {
    remove(handle);
}
