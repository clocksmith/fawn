//! Build-bound typed storage only. C owns capture; retained Zig owns classification.
const std = @import("std");
const retained = @import("../../runtime/diagnostics/error_scope.zig");
const abi = @import("../../core/abi/wgpu_handle_types.zig");
pub const c = @cImport({
    @cInclude("doe_error_scopes_zig_bridge.h");
});

// The authoritative native StringView and cImport StringView have separate Zig
// identities. These declarations preserve the original callback type and exact C
// ABI without casting function pointers or reinterpreting unrelated records.
extern fn doe_error_scope_pop(stack: *c.doe_error_scope_stack, callback: ?retained.PopErrorScopeCallback, userdata1: ?*anyopaque, userdata2: ?*anyopaque) c.doe_error_scope_result;
extern fn doe_error_scope_set_handler(stack: *c.doe_error_scope_stack, callback: ?retained.UncapturedErrorCallback, userdata1: ?*anyopaque, userdata2: ?*anyopaque) c.doe_error_scope_result;

fn fatal(operation: []const u8, result: c.doe_error_scope_result) noreturn {
    std.debug.panic("C error scopes {s} failed closed: status {d}, system_error {d}", .{ operation, result.status, result.system_error });
}

fn requireOk(operation: []const u8, result: c.doe_error_scope_result) void {
    if (result.status != c.DOE_ERROR_SCOPE_OK) fatal(operation, result);
}

pub const ErrorScopeStack = struct {
    storage: c.doe_error_scope_stack = undefined,
    initialized: bool = false,

    /// A COLD value may be assigned while building the device. Never copy LIVE.
    pub fn init() ErrorScopeStack {
        return .{};
    }

    pub fn initialize(self: *ErrorScopeStack) !void {
        if (self.initialized) fatal("duplicate init", .{ .status = c.DOE_ERROR_SCOPE_INVALID_STATE, .system_error = 0 });
        try verifyLayout();
        const result = c.doe_error_scopes_zig_init(&self.storage);
        if (result.status != c.DOE_ERROR_SCOPE_OK) {
            std.log.err("C error scopes init: status {d}, system_error {d}", .{ result.status, result.system_error });
            return error.ErrorScopeInitializationFailed;
        }
        self.initialized = true;
    }

    pub fn deinitialize(self: *ErrorScopeStack) void {
        if (!self.initialized) return;
        requireOk("deinit", c.doe_error_scope_deinit(&self.storage));
        self.initialized = false;
    }

    fn stack(self: *ErrorScopeStack, operation: []const u8) *c.doe_error_scope_stack {
        if (!self.initialized) fatal(operation, .{ .status = c.DOE_ERROR_SCOPE_INVALID_STATE, .system_error = 0 });
        return &self.storage;
    }

    pub fn push(self: *ErrorScopeStack, filter: u32) void {
        const result = c.doe_error_scope_push(self.stack("push"), filter);
        switch (result.status) {
            c.DOE_ERROR_SCOPE_OK, c.DOE_ERROR_SCOPE_LIMIT => {},
            else => fatal("push", result),
        }
    }

    pub fn pop(self: *ErrorScopeStack, info: retained.WGPUPopErrorScopeCallbackInfo) bool {
        const result = doe_error_scope_pop(self.stack("pop"), info.callback, info.userdata1, info.userdata2);
        // The callback may have released the device. Never touch self here.
        return switch (result.status) {
            c.DOE_ERROR_SCOPE_OK => true,
            c.DOE_ERROR_SCOPE_EMPTY => false,
            else => fatal("pop", result),
        };
    }

    pub fn deliver(self: *ErrorScopeStack, error_type: u32, message: []const u8) void {
        const result = c.doe_error_scope_deliver(self.stack("deliver"), error_type, message.ptr, message.len);
        // No deferred cleanup, counters, stack reads or lifetime lease after dispatch.
        requireOk("deliver", result);
    }

    pub fn set_uncaptured_handler(self: *ErrorScopeStack, callback: ?retained.UncapturedErrorCallback, userdata1: ?*anyopaque, userdata2: ?*anyopaque) void {
        const result = doe_error_scope_set_handler(self.stack("set_handler"), callback, userdata1, userdata2);
        requireOk("set_handler", result);
    }
};

pub fn verifyLayout() !void {
    const Stack = c.doe_error_scope_stack;
    const Entry = c.doe_error_scope_entry;
    const Result = c.doe_error_scope_result;
    var stack_layout: [c.DOE_ERROR_SCOPES_STACK_LAYOUT_COUNT]usize = undefined;
    var entry_layout: [c.DOE_ERROR_SCOPES_ENTRY_LAYOUT_COUNT]usize = undefined;
    var result_layout: [c.DOE_ERROR_SCOPES_RESULT_LAYOUT_COUNT]usize = undefined;
    var string_layout: [c.DOE_ERROR_SCOPES_STRING_LAYOUT_COUNT]usize = undefined;
    c.doe_error_scopes_zig_stack_layout(&stack_layout);
    c.doe_error_scopes_zig_entry_layout(&entry_layout);
    c.doe_error_scopes_zig_result_layout(&result_layout);
    c.doe_error_scopes_zig_string_layout(&string_layout);
    const stack_expected = [_]usize{ @sizeOf(Stack), @alignOf(Stack) } ++ offsets(Stack, .{ "mutex", "scopes", "depth", "callback", "userdata1", "userdata2", "state" });
    const entry_expected = [_]usize{ @sizeOf(Entry), @alignOf(Entry) } ++ offsets(Entry, .{ "filter", "error_type", "length", "bytes" });
    const result_expected = [_]usize{ @sizeOf(Result), @alignOf(Result) } ++ offsets(Result, .{ "status", "system_error" });
    const string_expected = [_]usize{ @sizeOf(abi.WGPUStringView), @alignOf(abi.WGPUStringView) } ++ offsets(abi.WGPUStringView, .{ "data", "length" });
    if (@sizeOf(c.doe_error_scope_status) != 4 or @sizeOf(c_int) != 4 or @sizeOf(u32) != 4 or
        !std.mem.eql(usize, &stack_layout, &stack_expected) or
        !std.mem.eql(usize, &entry_layout, &entry_expected) or
        !std.mem.eql(usize, &result_layout, &result_expected) or
        !std.mem.eql(usize, &string_layout, &string_expected)) return error.ErrorScopeBridgeLayoutMismatch;
}

fn offsets(comptime T: type, comptime fields: anytype) [fields.len]usize {
    var result: [fields.len]usize = undefined;
    inline for (fields, 0..) |field, index| result[index] = @offsetOf(T, field);
    return result;
}
