//! Default-off payload selection; the accepted future owner remains independent.
const selected = @import("build_options").experimental_c_callbacks;
const retained = @import("doe_callback_delivery.zig");
const owner = if (selected) @import("doe_callback_c_adapter.zig") else retained;

pub const ready = owner.ready;
pub const publish = owner.publish;
pub const instanceForDevice = retained.instanceForDevice;
pub const Publication = if (selected) owner.Publication else @import("doe_future_owner.zig").Completion;

pub fn markReady(publication: *Publication) void {
    if (selected) owner.markReady(publication) else publication.markReady();
}
