const gpu = @import("model_texture_value_types.zig");
const resource = @import("model_resource_types.zig");
const texture_copy = @import("../texture_copy.zig");

pub const TextureWriteCommand = struct {
    texture: resource.CopyTextureResource,
    data: []const u8,
    /// Destination subresource, origin, extent and aspect of a queue write.
    /// Null writes the whole first layer of `texture.mip_level` at the origin.
    copy: ?texture_copy.Copy = null,
};

pub const TextureQueryCommand = struct {
    handle: u64,
    expected_width: ?u32 = null,
    expected_height: ?u32 = null,
    expected_depth_or_array_layers: ?u32 = null,
    expected_format: ?gpu.WGPUTextureFormat = null,
    expected_dimension: ?u32 = null,
    expected_view_dimension: ?u32 = null,
    expected_sample_count: ?u32 = null,
    expected_usage: ?gpu.WGPUFlags = null,
};

pub const TextureDestroyCommand = struct {
    handle: u64,
};
