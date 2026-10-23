//! One stack owner selected at build time. Classification remains retained Zig.
const selected = @import("build_options").experimental_c_error_scopes;
const owner = if (selected) @import("doe_error_scope_c_adapter.zig") else @import("../../runtime/diagnostics/error_scope.zig");

pub const ErrorScopeStack = owner.ErrorScopeStack;

/// Called only at the final device address, before loss-event publication.
pub fn initialize(stack: *ErrorScopeStack) !void {
    if (selected) try stack.initialize();
}

/// The caller already excludes every operation and new entry.
pub fn deinitialize(stack: *ErrorScopeStack) void {
    if (selected) stack.deinitialize();
}
