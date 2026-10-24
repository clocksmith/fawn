const objects = @import("../support/doe_native_object_types.zig");
const commands = @import("../support/doe_native_command_types.zig");
const errors = @import("../../runtime/diagnostics/error_scope.zig");
const contract = @import("../../contracts/command_recording.zig");

/// Invalidates the encoder with its first cause. WebGPU reports encoder
/// validation at finish() (https://gpuweb.github.io/gpuweb/#dom-gpucommandencoder-finish),
/// so the error is generated there, inside the caller's error scope.
pub fn fail(encoder: *objects.DoeCommandEncoder, cause: contract.Failure) void {
    _ = encoder.state.fail(cause);
}

/// The error finish() generates for a non-open encoder: the recorded cause,
/// or InvalidState for a finished or still-locked (open pass) encoder.
pub fn reportAtFinish(encoder: *objects.DoeCommandEncoder) void {
    const cause: contract.Failure = switch (encoder.state) {
        .open => return,
        .failed => |recorded| recorded,
        .pass, .finished => error.InvalidState,
    };
    encoder.dev.error_scopes.deliver(errors.zig_error_to_type(cause), contract.message(cause));
}

/// https://gpuweb.github.io/gpuweb/#abstract-opdef-validate-the-encoder-state:
/// a command on an ended encoder or pass generates its validation error now;
/// a command on a locked encoder (a pass is open) invalidates it, reported at
/// finish(); an already invalid encoder stays silent.
fn reportEnded(encoder: *objects.DoeCommandEncoder) void {
    encoder.dev.error_scopes.deliver(errors.ERROR_TYPE_VALIDATION, contract.message(error.InvalidState));
}

pub fn requireOpen(encoder: *objects.DoeCommandEncoder) bool {
    switch (encoder.state) {
        .open => return true,
        .finished => reportEnded(encoder),
        .pass => fail(encoder, error.InvalidState),
        .failed => {},
    }
    return false;
}

pub fn requireRecording(encoder: *objects.DoeCommandEncoder) bool {
    switch (encoder.state) {
        .open, .pass => return true,
        .finished => reportEnded(encoder),
        .failed => {},
    }
    return false;
}

pub fn requirePass(encoder: *objects.DoeCommandEncoder, identity: usize) bool {
    switch (encoder.state) {
        .pass => |current| if (current == identity) return true else reportEnded(encoder),
        .failed => {},
        .open, .finished => reportEnded(encoder),
    }
    return false;
}

pub fn endPass(encoder: *objects.DoeCommandEncoder, identity: usize) void {
    if (encoder.state == .pass and encoder.state.pass == identity) encoder.state = .open;
}

pub fn reserve(encoder: *objects.DoeCommandEncoder, command_count: usize, reference_count: usize) bool {
    if (!requireRecording(encoder)) return false;
    const before = encoder.cmds.capacity;
    encoder.cmds.ensureUnusedCapacity(encoder.allocator, command_count) catch |err| {
        observeGrowth(encoder, before, true);
        fail(encoder, err);
        return false;
    };
    observeGrowth(encoder, before, false);
    encoder.references.ensureUnusedCapacity(encoder.allocator, reference_count) catch |err| {
        fail(encoder, err);
        return false;
    };
    return true;
}

fn observeGrowth(encoder: *objects.DoeCommandEncoder, before: usize, failed: bool) void {
    if (comptime @import("doe_command_storage.zig").OBSERVATION_MODE == .ordinary) return;
    if (encoder.dev.command_storage) |*pool| pool.observeGrowth(before, encoder.cmds.capacity, failed);
}

pub fn append(encoder: *objects.DoeCommandEncoder, command: commands.RecordedCmd) bool {
    if (!reserve(encoder, 1, 0)) return false;
    encoder.cmds.appendAssumeCapacity(command);
    return true;
}
