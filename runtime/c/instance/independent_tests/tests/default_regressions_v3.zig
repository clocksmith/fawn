// SPDX-License-Identifier: MIT
// Post-handoff mechanical root for unchanged default regressions required by N05.
comptime {
    _ = @import("src/native/resource/doe_buffer_ops_native.zig");
    _ = @import("src/native/command/doe_encoder_native.zig");
    _ = @import("src/native/command/doe_command_storage.zig");
    _ = @import("src/native/support/doe_future_ids.zig");
    _ = @import("src/native/support/doe_callback_delivery.zig");
    _ = @import("src/native/support/doe_device_loss.zig");
    _ = @import("src/contracts/resource_lease.zig");
    _ = @import("src/dropin/wgpu_dropin_ext_a_pipeline.zig");
    _ = @import("src/native/queue/doe_queue_lifecycle.zig");
    _ = @import("src/native/diagnostics/doe_program_identity_trace.zig");
}
