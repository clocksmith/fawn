const builtin = @import("builtin");
const has_vulkan = builtin.os.tag == .linux;

const backend_lifecycle = @import("../../backend/dropin_lifecycle.zig");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");
const abi_texture = @import("../../core/abi/wgpu_texture_base_types.zig");
const binding_contract = @import("../../contracts/binding.zig");

pub const NativeVulkanRuntime = if (has_vulkan) backend_lifecycle.NativeVulkanRuntime else void;
pub const NativeD3D12Runtime = backend_lifecycle.NativeD3D12Runtime;

pub const MAX_BIND: usize = 32;
/// WebGPU maxBindingsPerBindGroup on every Doe device: maximum-class limits
/// never drop below the default, and adapters report exactly the default.
/// A binding number at or above it is a validation error in a bind group
/// layout and in a pipeline whose entry points use it.
pub const MAX_BINDINGS_PER_BIND_GROUP: u32 = 1000;
pub const MAX_RENDER_BIND_GROUPS: usize = 4;
pub const MAX_COMPUTE_BIND_GROUPS: usize = 4;
pub const MAX_FLAT_BIND: usize = MAX_BIND * MAX_COMPUTE_BIND_GROUPS;
/// Bytes setBindGroup's dynamic offsets add to each binding number of a group.
pub const BindGroupDynamicOffsets = [MAX_BIND]u64;
pub const NO_DYNAMIC_OFFSETS: BindGroupDynamicOffsets = [_]u64{0} ** MAX_BIND;
pub const VULKAN_PREPARED_BINDING_CACHE_CAPACITY: usize = 32;
pub const MAX_VERTEX_BUFFERS: usize = 8;
pub const MAX_VERTEX_ATTRIBUTES: usize = 16;
pub const VERTEX_BUFFER_SLOT_BASE: u32 = 8;
pub const ERR_CAP: usize = 512;

pub const extractWorkgroupSize = @import("../../compiler/wgsl/runtime/shader_info.zig").extractWorkgroupSize;

pub const BindingInfo = struct {
    group: u32,
    binding: u32,
    kind: u32 = @intFromEnum(binding_contract.ShaderKind.buffer),
    addr_space: u32 = 0,
    access: u32 = 0,
};

pub const MAX_SHADER_BINDINGS: usize = binding_contract.MAX_SHADER_BINDINGS;

pub const CompilationMessageKind = enum(u8) {
    none,
    @"error",
    warning,
    info,
};

pub const DoeBindGroupLayoutEntry = struct {
    binding: u32 = 0,
    resource_kind: u32 = 0,
    buffer_binding_type: u32 = abi_binding.WGPUBufferBindingType_Undefined,
    buffer_has_dynamic_offset: bool = false,
    texture_sample_type: u32 = abi_binding.WGPUTextureSampleType_Undefined,
    texture_view_dimension: u32 = abi_texture.WGPUTextureViewDimension_Undefined,
    texture_multisampled: bool = false,
    binding_array_size: u32 = 0,
    /// Remaining WebGPU entry members, so equal-entry layout comparison sees
    /// every member (https://gpuweb.github.io/gpuweb/#bind-group-layout-equal).
    visibility: u64 = 0,
    buffer_min_binding_size: u64 = 0,
    sampler_binding_type: u32 = 0,
    storage_texture_format: u32 = 0,
};
