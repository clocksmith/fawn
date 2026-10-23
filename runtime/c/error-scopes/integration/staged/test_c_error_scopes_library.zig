//! Test-only DSO: unchanged actual native exports plus one CPU topology factory.
comptime {
    _ = @import("src/dropin/wgpu_dropin_lib.zig");
    _ = @import("tests/support/c_error_scopes_fixture.zig");
}
