//! Default-off selection only. All label consumers share this one owner.
const owner = if (@import("build_options").experimental_c_labels)
    @import("doe_label_c_adapter.zig")
else
    @import("doe_label_store.zig");

pub const set = owner.set;
pub const get = owner.get;
pub const remove = owner.remove;
pub const doeNativeObjectSetLabel = owner.doeNativeObjectSetLabel;
pub const doeNativeObjectGetLabel = owner.doeNativeObjectGetLabel;
pub const doeNativeObjectRemoveLabel = owner.doeNativeObjectRemoveLabel;
