// doe_bind_group_native.zig — Bind group, bind group layout, and pipeline layout
// C ABI exports for the Doe native Metal/Vulkan backend. Sharded from doe_wgpu_native.zig.

const std = @import("std");
const native_types = @import("../support/doe_native_object_types.zig");
const native_shared = @import("../support/doe_native_shared_types.zig");
const native_helpers = @import("../support/doe_native_object_helpers.zig");
const native_exports = @import("../support/doe_native_exports.zig");
const abi_core = @import("../../core/abi/wgpu_core_base_types.zig");
const abi_texture = @import("../../core/abi/wgpu_texture_base_types.zig");
const abi_binding = @import("../../core/abi/wgpu_binding_base_types.zig");
const abi_callback = @import("../../core/abi/wgpu_callback_descriptor_types.zig");
const abi_pipeline = @import("../../core/abi/wgpu_pipeline_descriptor_types.zig");
const binding_contract = @import("../../contracts/binding.zig");
const model_render_types = @import("../../contracts/model/model_render_types.zig");
const texture_contract = @import("../../contracts/texture.zig");
const texture_sampler = @import("doe_texture_sampler_native.zig");
const command_recording = @import("../../contracts/command_recording.zig");
const error_scope = @import("../../runtime/diagnostics/error_scope.zig");
const device_caps = @import("../support/doe_device_caps.zig");
const abi_feature = @import("../../core/abi/wgpu_feature_base_types.zig");

const alloc = native_helpers.alloc;
const make = native_helpers.make;
const cast = native_helpers.cast;
const toOpaque = native_helpers.toOpaque;
const MAX_BIND = native_shared.MAX_BIND;
const MAX_COMPUTE_BIND_GROUPS = native_shared.MAX_COMPUTE_BIND_GROUPS;
const label_store = native_helpers.label_store;

const DoeDevice = native_types.DoeDevice;
const DoeBuffer = native_types.DoeBuffer;
const DoeTexture = native_types.DoeTexture;
const DoeSampler = native_types.DoeSampler;
const DoeBindGroupLayoutEntry = native_shared.DoeBindGroupLayoutEntry;
const DoeBindGroupLayout = native_types.DoeBindGroupLayout;
const DoeBindGroup = native_types.DoeBindGroup;
const DoePipelineLayout = native_types.DoePipelineLayout;
const DoeTextureView = native_types.DoeTextureView;
const DoeExternalTexture = @import("doe_external_texture_native.zig").DoeExternalTexture;

const RESOURCE_KIND_NONE = binding_contract.layoutResourceKindCode(.none);
const RESOURCE_KIND_BUFFER = binding_contract.layoutResourceKindCode(.buffer);
const RESOURCE_KIND_SAMPLER = binding_contract.layoutResourceKindCode(.sampler);
const RESOURCE_KIND_TEXTURE = binding_contract.layoutResourceKindCode(.texture);
const RESOURCE_KIND_STORAGE_TEXTURE = binding_contract.layoutResourceKindCode(.storage_texture);
const RESOURCE_KIND_EXTERNAL_TEXTURE = binding_contract.layoutResourceKindCode(.external_texture);
const FLAT_BUFFER_BIND_GROUP_ENTRY_LIMIT: u32 = 4;

fn chained_struct(raw: ?*anyopaque) ?*const abi_callback.WGPUChainedStruct {
    const ptr = raw orelse return null;
    return @ptrCast(@alignCast(ptr));
}

fn has_chain_type(raw: ?*anyopaque, s_type: abi_core.WGPUSType) bool {
    const chain = chained_struct(raw) orelse return false;
    return chain.sType == s_type;
}

fn external_texture_layout_chain(raw: ?*anyopaque) ?*const abi_pipeline.WGPUExternalTextureBindingLayout {
    if (!has_chain_type(raw, abi_core.WGPUSType_ExternalTextureBindingLayout)) return null;
    const ptr = raw orelse return null;
    return @ptrCast(@alignCast(ptr));
}

fn external_texture_entry_chain(raw: ?*anyopaque) ?*const abi_pipeline.WGPUExternalTextureBindingEntry {
    if (!has_chain_type(raw, abi_core.WGPUSType_ExternalTextureBindingEntry)) return null;
    const ptr = raw orelse return null;
    return @ptrCast(@alignCast(ptr));
}

fn buffer_binding_type_active(value: u32) bool {
    return value != abi_binding.WGPUBufferBindingType_BindingNotUsed and
        value != abi_binding.WGPUBufferBindingType_Undefined;
}

fn texture_sample_type_active(value: u32) bool {
    return value != abi_binding.WGPUTextureSampleType_BindingNotUsed and
        value != abi_binding.WGPUTextureSampleType_Undefined;
}

fn storage_texture_access_active(value: u32) bool {
    return value != abi_binding.WGPUStorageTextureAccess_BindingNotUsed and
        value != abi_binding.WGPUStorageTextureAccess_Undefined;
}

fn sampler_binding_type_active(value: u32) bool {
    return value != abi_binding.WGPUSamplerBindingType_BindingNotUsed and
        value != abi_binding.WGPUSamplerBindingType_Undefined;
}

fn classify_layout_entry(entry: abi_pipeline.WGPUBindGroupLayoutEntry) DoeBindGroupLayoutEntry {
    var out = DoeBindGroupLayoutEntry{
        .binding = entry.binding,
        .resource_kind = RESOURCE_KIND_NONE,
        .binding_array_size = entry.bindingArraySize,
        .visibility = @intCast(entry.visibility),
    };
    if (buffer_binding_type_active(entry.buffer.type)) {
        out.resource_kind = RESOURCE_KIND_BUFFER;
        out.buffer_binding_type = entry.buffer.type;
        out.buffer_has_dynamic_offset = entry.buffer.hasDynamicOffset != 0;
        out.buffer_min_binding_size = entry.buffer.minBindingSize;
        return out;
    }
    if (texture_sample_type_active(entry.texture.sampleType)) {
        out.resource_kind = RESOURCE_KIND_TEXTURE;
        out.texture_sample_type = entry.texture.sampleType;
        out.texture_view_dimension = entry.texture.viewDimension;
        out.texture_multisampled = entry.texture.multisampled != 0;
        return out;
    }
    if (storage_texture_access_active(entry.storageTexture.access)) {
        out.resource_kind = RESOURCE_KIND_STORAGE_TEXTURE;
        out.texture_sample_type = entry.storageTexture.access;
        out.texture_view_dimension = entry.storageTexture.viewDimension;
        out.storage_texture_format = entry.storageTexture.format;
        return out;
    }
    if (sampler_binding_type_active(entry.sampler.type)) {
        out.resource_kind = RESOURCE_KIND_SAMPLER;
        out.sampler_binding_type = entry.sampler.type;
        return out;
    }
    if (external_texture_layout_chain(entry.nextInChain) != null) {
        out.resource_kind = RESOURCE_KIND_EXTERNAL_TEXTURE;
    }
    return out;
}

/// Float and depth formats also bind as unfilterable-float (WebGPU texture
/// format capabilities, "compatible sample types"); a stencil aspect binds as
/// uint.
fn texture_sample_type_compatible(layout_type: u32, view: *DoeTextureView) bool {
    const inferred = texture_contract.inferViewSampleType(view.tex.format, view.aspect);
    if (layout_type == inferred) return true;
    return layout_type == abi_binding.WGPUTextureSampleType_UnfilterableFloat and
        (inferred == abi_binding.WGPUTextureSampleType_Float or
            inferred == abi_binding.WGPUTextureSampleType_Depth);
}

fn resolve_view_dimension(view: *DoeTextureView) u32 {
    if (view.dimension != 0) return view.dimension;
    if (view.tex.texture_binding_view_dimension != 0) return view.tex.texture_binding_view_dimension;
    return texture_contract.defaultViewDimension(view.tex.dimension);
}

fn texture_aspect_matches(tex: *DoeTexture, view: *DoeTextureView) bool {
    const aspect = if (view.aspect != 0) view.aspect else abi_texture.WGPUTextureAspect_All;
    return texture_contract.aspectMatches(tex.format, aspect);
}

fn storage_texture_access_supported(access: u32) bool {
    return binding_contract.storageTextureAccessSupported(access);
}

fn texture_view_matches_layout(layout_entry: DoeBindGroupLayoutEntry, view: *DoeTextureView) bool {
    const tex = view.tex;
    if (layout_entry.texture_multisampled != (tex.sample_count > 1)) return false;
    if (layout_entry.texture_view_dimension != 0 and
        layout_entry.texture_view_dimension != abi_texture.WGPUTextureViewDimension_Undefined and
        layout_entry.texture_view_dimension != resolve_view_dimension(view)) return false;
    if (layout_entry.texture_sample_type != 0 and
        layout_entry.texture_sample_type != abi_binding.WGPUTextureSampleType_Undefined and
        !texture_sample_type_compatible(layout_entry.texture_sample_type, view)) return false;
    if (!texture_aspect_matches(tex, view)) return false;
    return true;
}

fn storage_texture_matches_layout(layout_entry: DoeBindGroupLayoutEntry, view: *DoeTextureView) bool {
    // A swizzled view never binds as a storage texture.
    if (view.swizzled) return false;
    switch (view.tex.format) {
        abi_texture.WGPUTextureFormat_Stencil8, abi_texture.WGPUTextureFormat_Depth16Unorm, abi_texture.WGPUTextureFormat_Depth24Plus, abi_texture.WGPUTextureFormat_Depth24PlusStencil8, abi_texture.WGPUTextureFormat_Depth32Float, abi_texture.WGPUTextureFormat_Depth32FloatStencil8 => return false,
        else => {},
    }
    const usage = view.usage | view.tex.usage;
    if ((usage & abi_texture.WGPUTextureUsage_StorageBinding) == 0) return false;
    if (layout_entry.texture_view_dimension != 0 and
        layout_entry.texture_view_dimension != abi_texture.WGPUTextureViewDimension_Undefined and
        layout_entry.texture_view_dimension != resolve_view_dimension(view)) return false;
    if (!texture_aspect_matches(view.tex, view)) return false;
    return storage_texture_access_supported(layout_entry.texture_sample_type);
}

/// Minimum dynamic offset alignments, from the device limits.
pub const DynamicOffsetAlignment = struct {
    uniform: u64,
    storage: u64,
};

pub fn deviceDynamicOffsetAlignment(device: *const native_types.DoeDevice) DynamicOffsetAlignment {
    return .{ .uniform = device.buffer_offset_alignment.uniform, .storage = device.buffer_offset_alignment.storage };
}

/// WebGPU setBindGroup dynamic offsets: one per dynamic buffer entry in
/// ascending binding order, aligned, and keeping each binding in its buffer
/// (https://gpuweb.github.io/gpuweb/#dom-gpubindingcommandsmixin-setbindgroup).
/// Returns the bytes to add per binding number.
pub fn resolveDynamicOffsets(
    group: *const DoeBindGroup,
    offsets: []const u32,
    alignment: DynamicOffsetAlignment,
) command_recording.BindGroupError!native_shared.BindGroupDynamicOffsets {
    var resolved = native_shared.NO_DYNAMIC_OFFSETS;
    const entries: []const DoeBindGroupLayoutEntry = if (group.layout) |layout| layout.entries orelse &.{} else &.{};
    var used: usize = 0;
    for (0..MAX_BIND) |binding| {
        const entry = for (entries) |candidate| {
            if (candidate.binding == binding) break candidate;
        } else continue;
        if (!entry.buffer_has_dynamic_offset) continue;
        if (used == offsets.len) return error.BindGroupDynamicOffsetCount;
        const offset: u64 = offsets[used];
        used += 1;
        const required = if (entry.buffer_binding_type == abi_binding.WGPUBufferBindingType_Uniform)
            alignment.uniform
        else
            alignment.storage;
        if (required == 0 or offset % required != 0) return error.BindGroupDynamicOffsetUnaligned;
        const buffer = group.retained_buffers[binding] orelse return error.BindGroupDynamicOffsetOutOfBounds;
        const binding_end = std.math.add(u64, group.offsets[binding], group.buffer_sizes[binding]) catch
            return error.BindGroupDynamicOffsetOutOfBounds;
        const end = std.math.add(u64, binding_end, offset) catch return error.BindGroupDynamicOffsetOutOfBounds;
        if (end > buffer.size) return error.BindGroupDynamicOffsetOutOfBounds;
        resolved[binding] = offset;
    }
    if (used != offsets.len) return error.BindGroupDynamicOffsetCount;
    return resolved;
}

fn find_layout_entry(layout: *DoeBindGroupLayout, binding: u32) ?DoeBindGroupLayoutEntry {
    const entries = layout.entries orelse return null;
    for (entries) |entry| {
        if (entry.binding == binding) return entry;
    }
    return null;
}

fn retain_buffer(bg: *DoeBindGroup, binding: usize, buffer: *DoeBuffer) void {
    native_helpers.object_add_ref(DoeBuffer, toOpaque(buffer));
    bg.retained_buffers[binding] = buffer;
}

fn remember_vulkan_buffer_binding(bg: *DoeBindGroup, binding: usize, buffer: *const DoeBuffer) void {
    if (buffer.vk_id == 0) return;
    bg.vk_buffer_handles[binding] = buffer.vk_id;
    bg.vk_buffer_binding_mask |= @as(u64, 1) << @intCast(binding);
}

fn resolve_buffer_binding_size(buffer: *const DoeBuffer, offset: u64, requested_size: u64) ?u64 {
    if (offset > buffer.size) return null;
    if (requested_size == abi_core.WGPU_WHOLE_SIZE) return buffer.size - offset;
    const end = std.math.add(u64, offset, requested_size) catch return null;
    if (end > buffer.size) return null;
    return requested_size;
}

fn append_render_texture_binding(bg: *DoeBindGroup, binding: u32, resource_kind: u32, view: *DoeTextureView) bool {
    if (bg.render_texture_view_count >= model_render_types.MAX_RENDER_BIND_ENTRIES) return false;
    const index: usize = @intCast(bg.render_texture_view_count);
    native_helpers.object_add_ref(DoeTextureView, toOpaque(view));
    bg.render_texture_views[index] = view;
    bg.render_texture_bindings[index] = binding;
    bg.render_texture_resource_kinds[index] = resource_kind;
    bg.render_texture_view_count += 1;
    return true;
}

fn append_render_sampler_binding(bg: *DoeBindGroup, binding: u32, sampler: *DoeSampler) bool {
    if (bg.render_sampler_count >= model_render_types.MAX_RENDER_BIND_ENTRIES) return false;
    const index: usize = @intCast(bg.render_sampler_count);
    native_helpers.object_add_ref(DoeSampler, toOpaque(sampler));
    bg.render_samplers[index] = sampler;
    bg.render_sampler_bindings[index] = binding;
    bg.render_sampler_count += 1;
    return true;
}

fn retain_external_texture(bg: *DoeBindGroup, binding: usize, external_texture: abi_core.WGPUExternalTexture) void {
    native_helpers.object_add_ref(DoeExternalTexture, external_texture);
    bg.retained_external_textures[binding] = external_texture;
}

fn resolve_external_texture(entry: abi_pipeline.WGPUBindGroupEntry) ?abi_core.WGPUExternalTexture {
    if (external_texture_entry_chain(entry.nextInChain)) |chain| {
        return chain.externalTexture;
    }
    return null;
}

// ============================================================
// Bind Group Layout / Bind Group / Pipeline Layout
// ============================================================

pub export fn doeNativeDeviceCreateBindGroupLayout(dev_raw: ?*anyopaque, desc: ?*const abi_pipeline.WGPUBindGroupLayoutDescriptor) callconv(.c) ?*anyopaque {
    const owner = cast(DoeDevice, dev_raw) orelse return null;
    if (!owner.requireAlive()) return null;
    const d = desc orelse return null;
    if (d.entryCount > 0 and d.entries == null) {
        return null;
    }
    if (d.entryCount > 0) for (d.entries[0..d.entryCount]) |entry| {
        if (entry.binding < native_shared.MAX_BINDINGS_PER_BIND_GROUP) continue;
        var buf: [160]u8 = undefined;
        owner.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, std.fmt.bufPrint(&buf, "createBindGroupLayout: entry binding {d} is not below maxBindingsPerBindGroup ({d})", .{ entry.binding, native_shared.MAX_BINDINGS_PER_BIND_GROUP }) catch "createBindGroupLayout: entry binding exceeds maxBindingsPerBindGroup");
        return null;
    };
    const bgl = make(DoeBindGroupLayout) orelse return null;
    var stored_entries: ?[]DoeBindGroupLayoutEntry = null;
    if (d.entryCount > 0) {
        stored_entries = alloc.alloc(DoeBindGroupLayoutEntry, d.entryCount) catch {
            alloc.destroy(bgl);
            return null;
        };
        for (d.entries[0..d.entryCount], 0..) |entry, i| {
            stored_entries.?[i] = classify_layout_entry(entry);
        }
    }
    bgl.* = .{
        .device = owner,
        .entry_count = @intCast(d.entryCount),
        .entries = stored_entries,
    };
    const result = toOpaque(bgl);
    label_store.set(result, d.label.data, d.label.length);
    return result;
}

pub export fn doeNativeDeviceCreateBufferBindGroupLayoutFlat4(dev_raw: ?*anyopaque, entry_count: u32, b0: u32, b1: u32, b2: u32, b3: u32) callconv(.c) ?*anyopaque {
    const owner = cast(DoeDevice, dev_raw) orelse return null;
    if (!owner.requireAlive()) return null;
    if (entry_count > FLAT_BUFFER_BIND_GROUP_ENTRY_LIMIT) return null;
    const bgl = make(DoeBindGroupLayout) orelse return null;
    bgl.* = .{
        .device = owner,
        .entry_count = entry_count,
        .entries_inline = true,
    };
    if (entry_count > 0) {
        const count: usize = @intCast(entry_count);
        const bindings = [_]u32{ b0, b1, b2, b3 };
        for (0..count) |i| {
            bgl.inline_entries[i] = .{
                .binding = bindings[i],
                .resource_kind = RESOURCE_KIND_BUFFER,
            };
        }
        bgl.entries = bgl.inline_entries[0..count];
    }
    return toOpaque(bgl);
}

pub export fn doeNativeBindGroupLayoutRelease(raw: ?*anyopaque) callconv(.c) void {
    if (cast(DoeBindGroupLayout, raw)) |l| {
        if (!native_helpers.object_should_destroy(l)) return;
        label_store.remove(raw);
        if (!l.entries_inline) {
            if (l.entries) |entries| alloc.free(entries);
        }
        alloc.destroy(l);
    }
}

/// The entries of a bind group descriptor against its layout
/// (https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbindgroup): one entry
/// per layout binding (each element of a binding array), no duplicates, no
/// binding the layout lacks, and exactly one resource of the layout entry's
/// type. Any binding number below maxBindingsPerBindGroup is checked.
fn bindGroupEntriesFailure(device: ?*const DoeDevice, layout: *const DoeBindGroupLayout, entries: []const abi_pipeline.WGPUBindGroupEntry) ?[]const u8 {
    if (device) |owner| if (layout.device) |creator| if (creator != owner)
        return "createBindGroup: the layout belongs to another device";
    const layout_entries = if (layout.entries) |all| all[0..@min(all.len, layout.entry_count)] else &[_]DoeBindGroupLayoutEntry{};
    var expected: usize = 0;
    for (layout_entries) |entry| expected += @max(entry.binding_array_size, 1);
    if (entries.len != expected) return "createBindGroup: descriptor.entries must have one entry per layout binding";
    for (entries, 0..) |entry, index| {
        for (entries[0..index]) |earlier| {
            if (earlier.binding == entry.binding) return "createBindGroup: two entries name the same binding";
        }
        const layout_entry = for (layout_entries) |candidate| {
            if (entry.binding >= candidate.binding and entry.binding - candidate.binding < @max(candidate.binding_array_size, 1)) break candidate;
        } else return "createBindGroup: an entry names a binding the layout does not have";
        const external = resolve_external_texture(entry) != null;
        const resources = @as(u32, @intFromBool(entry.buffer != null)) + @intFromBool(entry.sampler != null) +
            @intFromBool(entry.textureView != null) + @intFromBool(external);
        if (resources != 1) return "createBindGroup: an entry must have exactly one resource";
        const matches = switch (layout_entry.resource_kind) {
            RESOURCE_KIND_BUFFER => entry.buffer != null,
            RESOURCE_KIND_SAMPLER => entry.sampler != null,
            RESOURCE_KIND_TEXTURE, RESOURCE_KIND_STORAGE_TEXTURE => entry.textureView != null,
            RESOURCE_KIND_EXTERNAL_TEXTURE => external or entry.textureView != null,
            else => true,
        };
        if (!matches) return "createBindGroup: an entry's resource is not of its layout binding's type";
        if (device) |owner| if (entryResourceFailure(owner, layout_entry, entry)) |message| return message;
    }
    return null;
}

/// webgpu.h WGPUBufferBindingType_Undefined, WGPUSamplerBindingType_Undefined,
/// WGPUTextureSampleType_Undefined and WGPUTextureViewDimension_Undefined
/// select the layout entry defaults.
fn layoutBufferType(entry: DoeBindGroupLayoutEntry) u32 {
    return if (entry.buffer_binding_type == abi_binding.WGPUBufferBindingType_Undefined) abi_binding.WGPUBufferBindingType_Uniform else entry.buffer_binding_type;
}

fn layoutViewDimension(entry: DoeBindGroupLayoutEntry) u32 {
    return if (entry.texture_view_dimension == 0 or entry.texture_view_dimension == abi_texture.WGPUTextureViewDimension_Undefined)
        abi_texture.WGPUTextureViewDimension_2D
    else
        entry.texture_view_dimension;
}

fn deviceFeature(device: *const DoeDevice, feature: u32) bool {
    for (device.enabled_features) |enabled| {
        if (enabled == feature) return true;
    }
    return false;
}

/// The sample types a view can bind as
/// (https://gpuweb.github.io/gpuweb/#texture-format-caps): depth aspects as
/// depth or unfilterable-float, stencil as uint, 32-bit floats as
/// unfilterable-float (float too with float32-filterable), other floats as
/// float or unfilterable-float, integers as their own type.
fn sampleTypeCompatible(device: *const DoeDevice, layout_type: u32, view: *const DoeTextureView) bool {
    const aspect = if (view.aspect != 0) view.aspect else abi_texture.WGPUTextureAspect_All;
    const format = if (view.format != 0) view.format else view.tex.format;
    const inferred = texture_contract.inferViewSampleType(format, aspect);
    const unfilterable = abi_binding.WGPUTextureSampleType_UnfilterableFloat;
    return switch (inferred) {
        abi_binding.WGPUTextureSampleType_Depth => layout_type == inferred or layout_type == unfilterable,
        abi_binding.WGPUTextureSampleType_Float => switch (format) {
            abi_texture.WGPUTextureFormat_R32Float, abi_texture.WGPUTextureFormat_RG32Float, abi_texture.WGPUTextureFormat_RGBA32Float => layout_type == unfilterable or
                (layout_type == inferred and deviceFeature(device, abi_feature.WGPUFeatureName_Float32Filterable)),
            else => layout_type == inferred or layout_type == unfilterable,
        },
        else => layout_type == inferred,
    };
}

/// The per-entry resource rules of createBindGroup
/// (https://gpuweb.github.io/gpuweb/#dom-gpudevice-createbindgroup), after the
/// structural checks: the resource is valid to use with the device and meets
/// its layout binding's type-specific rules.
fn entryResourceFailure(device: *const DoeDevice, layout_entry: DoeBindGroupLayoutEntry, entry: abi_pipeline.WGPUBindGroupEntry) ?[]const u8 {
    switch (layout_entry.resource_kind) {
        RESOURCE_KIND_BUFFER => {
            const buffer = cast(DoeBuffer, entry.buffer) orelse return "createBindGroup: a buffer entry needs a valid buffer";
            if (buffer.error_object) return "createBindGroup: an entry's buffer is invalid";
            if (buffer.device_ref) |creator| if (creator != device) return "createBindGroup: an entry's buffer belongs to another device";
            const size = resolve_buffer_binding_size(buffer, entry.offset, entry.size) orelse
                return "createBindGroup: a buffer binding's offset and size must lie within the buffer";
            if (size == 0) return "createBindGroup: a buffer binding must not be empty";
            if (size < layout_entry.buffer_min_binding_size) return "createBindGroup: a buffer binding is smaller than its layout minBindingSize";
            var limits = std.mem.zeroes(abi_callback.WGPULimits);
            _ = device_caps.doeNativeDeviceGetLimits(toOpaque(@constCast(device)), &limits);
            if (layoutBufferType(layout_entry) == abi_binding.WGPUBufferBindingType_Uniform) {
                if (buffer.usage & abi_core.WGPUBufferUsage_Uniform == 0) return "createBindGroup: a uniform buffer binding needs UNIFORM usage";
                if (size > limits.maxUniformBufferBindingSize) return "createBindGroup: a uniform buffer binding exceeds maxUniformBufferBindingSize";
                if (entry.offset % device.buffer_offset_alignment.uniform != 0) return "createBindGroup: a uniform buffer binding offset must be a multiple of minUniformBufferOffsetAlignment";
            } else {
                if (buffer.usage & abi_core.WGPUBufferUsage_Storage == 0) return "createBindGroup: a storage buffer binding needs STORAGE usage";
                if (size > limits.maxStorageBufferBindingSize) return "createBindGroup: a storage buffer binding exceeds maxStorageBufferBindingSize";
                if (size % 4 != 0) return "createBindGroup: a storage buffer binding size must be a multiple of 4";
                if (entry.offset % device.buffer_offset_alignment.storage != 0) return "createBindGroup: a storage buffer binding offset must be a multiple of minStorageBufferOffsetAlignment";
            }
        },
        RESOURCE_KIND_SAMPLER => {
            const sampler = cast(DoeSampler, entry.sampler) orelse return "createBindGroup: a sampler entry needs a valid sampler";
            if (sampler.error_object) return "createBindGroup: an entry's sampler is invalid";
            if (sampler.device_ref) |creator| if (creator != device) return "createBindGroup: an entry's sampler belongs to another device";
            const ok = switch (layout_entry.sampler_binding_type) {
                abi_binding.WGPUSamplerBindingType_NonFiltering => !sampler.is_filtering and !sampler.is_comparison,
                abi_binding.WGPUSamplerBindingType_Comparison => sampler.is_comparison,
                else => !sampler.is_comparison, // filtering, the default
            };
            if (!ok) return "createBindGroup: a sampler does not match its layout sampler type";
        },
        RESOURCE_KIND_TEXTURE, RESOURCE_KIND_STORAGE_TEXTURE => {
            const view = cast(DoeTextureView, entry.textureView) orelse return "createBindGroup: a texture entry needs a valid texture view";
            const texture = view.tex;
            if (texture.error_object) return "createBindGroup: an entry's texture view is invalid";
            if (texture.device_ref) |creator| if (creator != device) return "createBindGroup: an entry's texture view belongs to another device";
            if (layoutViewDimension(layout_entry) != resolve_view_dimension(view)) return "createBindGroup: a texture view's dimension does not match its layout viewDimension";
            const usage = if (view.usage != 0) view.usage else texture.usage;
            if (layout_entry.resource_kind == RESOURCE_KIND_TEXTURE) {
                if (usage & abi_texture.WGPUTextureUsage_TextureBinding == 0) return "createBindGroup: a sampled texture view needs TEXTURE_BINDING usage";
                // Sample types are defined per aspect: a combined depth-stencil
                // view must select DepthOnly or StencilOnly to be sampled.
                const view_aspect = if (view.aspect != 0) view.aspect else abi_texture.WGPUTextureAspect_All;
                if (view_aspect == abi_texture.WGPUTextureAspect_All and texture_contract.aspectMatches(texture.format, abi_texture.WGPUTextureAspect_DepthOnly) and
                    texture_contract.aspectMatches(texture.format, abi_texture.WGPUTextureAspect_StencilOnly))
                    return "createBindGroup: a combined depth-stencil view must select one aspect to bind as a texture";
                const sample_type = if (layout_entry.texture_sample_type == abi_binding.WGPUTextureSampleType_Undefined) abi_binding.WGPUTextureSampleType_Float else layout_entry.texture_sample_type;
                if (!sampleTypeCompatible(device, sample_type, view)) return "createBindGroup: a texture view's format cannot bind as its layout sampleType";
                if (layout_entry.texture_multisampled != (texture.sample_count > 1)) return "createBindGroup: a texture view's sample count does not match its layout multisampled";
            } else {
                if (usage & abi_texture.WGPUTextureUsage_StorageBinding == 0) return "createBindGroup: a storage texture view needs STORAGE_BINDING usage";
                const format = if (view.format != 0) view.format else texture.format;
                if (format != layout_entry.storage_texture_format) return "createBindGroup: a storage texture view's format does not match its layout format";
                if (view.mip_level_count != 1) return "createBindGroup: a storage texture view must have one mip level";
                if (view.swizzled) return "createBindGroup: a swizzled texture view cannot bind as a storage texture";
            }
        },
        RESOURCE_KIND_EXTERNAL_TEXTURE => {
            // A GPUTextureView bound to an externalTexture entry: a 2d
            // single-mip, single-layer, single-sample view with
            // TEXTURE_BINDING usage of rgba8unorm, bgra8unorm or rgba16float.
            // A GPUExternalTexture is checked where it is resolved.
            const view = cast(DoeTextureView, entry.textureView) orelse return null;
            const texture = view.tex;
            if (texture.error_object) return "createBindGroup: an entry's texture view is invalid";
            if (texture.device_ref) |creator| if (creator != device) return "createBindGroup: an entry's texture view belongs to another device";
            if (resolve_view_dimension(view) != abi_texture.WGPUTextureViewDimension_2D) return "createBindGroup: a texture view bound to an externalTexture entry must be 2d";
            if (view.mip_level_count != 1) return "createBindGroup: a texture view bound to an externalTexture entry must have one mip level";
            if (view.array_layer_count > 1) return "createBindGroup: a texture view bound to an externalTexture entry must have one array layer";
            const usage = if (view.usage != 0) view.usage else texture.usage;
            if (usage & abi_texture.WGPUTextureUsage_TextureBinding == 0) return "createBindGroup: a texture view bound to an externalTexture entry needs TEXTURE_BINDING usage";
            const format = if (view.format != 0) view.format else texture.format;
            if (format != abi_texture.WGPUTextureFormat_RGBA8Unorm and format != abi_texture.WGPUTextureFormat_BGRA8Unorm and format != abi_texture.WGPUTextureFormat_RGBA16Float)
                return "createBindGroup: a texture view bound to an externalTexture entry must be rgba8unorm, bgra8unorm or rgba16float";
            if (texture.sample_count != 1) return "createBindGroup: a texture view bound to an externalTexture entry must be single-sampled";
        },
        else => {},
    }
    return null;
}

pub export fn doeNativeDeviceCreateBindGroup(dev_raw: ?*anyopaque, desc: ?*const abi_pipeline.WGPUBindGroupDescriptor) callconv(.c) ?*anyopaque {
    const owner = cast(DoeDevice, dev_raw) orelse return null;
    if (!owner.requireAlive()) return null;
    const d = desc orelse return null;
    if (d.entryCount > 0 and d.entries == null) return null;
    if (cast(DoeBindGroupLayout, d.layout)) |bgl| {
        if (bindGroupEntriesFailure(owner, bgl, if (d.entryCount > 0) d.entries[0..d.entryCount] else &.{})) |message| {
            owner.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, message);
            return null;
        }
    }
    const bg = make(DoeBindGroup) orelse return null;
    bg.* = .{};
    const layout = cast(DoeBindGroupLayout, d.layout);
    if (layout) |bgl| {
        native_helpers.object_add_ref(DoeBindGroupLayout, toOpaque(bgl));
        bg.layout = bgl;
    }

    // For Vulkan devices, store the DoeBuffer* opaque pointer in buffers[] instead
    // of the MTL handle, so the compute dispatch can look up the vk_id at submit time.
    const is_vulkan = if (cast(DoeDevice, dev_raw)) |dev| dev.backend == .vulkan else false;

    for (d.entries[0..d.entryCount]) |e| {
        var resource_kind = RESOURCE_KIND_NONE;
        var buffer_binding_type = abi_binding.WGPUBufferBindingType_Undefined;
        if (layout) |bgl| {
            if (find_layout_entry(bgl, e.binding)) |layout_entry| {
                resource_kind = layout_entry.resource_kind;
                buffer_binding_type = layout_entry.buffer_binding_type;
                if (layout_entry.resource_kind == RESOURCE_KIND_TEXTURE) {
                    const view = cast(DoeTextureView, e.textureView) orelse {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    };
                    if (view.tex.error_object) {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    }
                    if (!texture_view_matches_layout(layout_entry, view)) {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    }
                } else if (layout_entry.resource_kind == RESOURCE_KIND_STORAGE_TEXTURE) {
                    const view = cast(DoeTextureView, e.textureView) orelse {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    };
                    if (view.tex.error_object) {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    }
                    if (!storage_texture_matches_layout(layout_entry, view)) {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    }
                } else if (layout_entry.resource_kind == RESOURCE_KIND_EXTERNAL_TEXTURE and e.textureView != null) {
                    // A texture view in an externalTexture slot binds as a
                    // sampled texture (its rules were checked above).
                    resource_kind = RESOURCE_KIND_TEXTURE;
                } else if (layout_entry.resource_kind == RESOURCE_KIND_EXTERNAL_TEXTURE) {
                    const external_texture = resolve_external_texture(e) orelse {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    };
                    const ext = native_helpers.cast(DoeExternalTexture, external_texture) orelse {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    };
                    if (ext.expired or ext.plane0 == null) {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    }
                }
            }
        }
        if (cast(DoeBuffer, e.buffer)) |doe_buf| {
            if (e.binding < MAX_BIND) {
                if (doe_buf.error_object or doe_buf.destroyed) {
                    doeNativeBindGroupRelease(toOpaque(bg));
                    return null;
                }
                if (is_vulkan) {
                    // Store the DoeBuffer handle — dispatch reads vk_id from it.
                    bg.buffers[e.binding] = toOpaque(doe_buf);
                    remember_vulkan_buffer_binding(bg, @intCast(e.binding), doe_buf);
                    bg.vk_buffer_binding_types[e.binding] = buffer_binding_type;
                } else {
                    bg.buffers[e.binding] = doe_buf.mtl;
                }
                const binding_size = resolve_buffer_binding_size(doe_buf, e.offset, e.size) orelse {
                    doeNativeBindGroupRelease(toOpaque(bg));
                    return null;
                };
                bg.offsets[e.binding] = e.offset;
                bg.buffer_sizes[e.binding] = binding_size;
                retain_buffer(bg, e.binding, doe_buf);
                bg.resource_kinds[e.binding] = resource_kind;
                if (e.binding + 1 > bg.count) bg.count = e.binding + 1;
            }
        } else if (cast(DoeTextureView, e.textureView)) |view| {
            if (view.tex.error_object or !append_render_texture_binding(bg, e.binding, resource_kind, view)) {
                doeNativeBindGroupRelease(toOpaque(bg));
                return null;
            }
            if (e.binding < MAX_BIND) {
                bg.textures[e.binding] = if (view.handle) |handle| handle else view.tex.mtl;
                bg.texture_views[e.binding] = toOpaque(view);
                bg.resource_kinds[e.binding] = resource_kind;
                if (e.binding + 1 > bg.count) bg.count = e.binding + 1;
            }
        } else if (cast(DoeSampler, e.sampler)) |sampler| {
            if (sampler.error_object or !append_render_sampler_binding(bg, e.binding, sampler)) {
                doeNativeBindGroupRelease(toOpaque(bg));
                return null;
            }
            if (e.binding < MAX_BIND) {
                bg.samplers[e.binding] = if (is_vulkan) toOpaque(sampler) else sampler.mtl;
                bg.resource_kinds[e.binding] = resource_kind;
                if (e.binding + 1 > bg.count) bg.count = e.binding + 1;
            }
        } else if (resolve_external_texture(e)) |external_texture| {
            if (e.binding < MAX_BIND) {
                const ext_mod = @import("doe_external_texture_native.zig");
                const ext = native_helpers.cast(DoeExternalTexture, external_texture) orelse continue;
                if (is_vulkan) {
                    const plane0_view = texture_sampler.registeredTextureView(ext.plane0) orelse {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    };
                    if (plane0_view.tex.error_object or plane0_view.tex.vk_id == 0) {
                        doeNativeBindGroupRelease(toOpaque(bg));
                        return null;
                    }
                    bg.textures[e.binding] = toOpaque(plane0_view.tex);
                    bg.texture_views[e.binding] = toOpaque(plane0_view);
                } else {
                    bg.textures[e.binding] = ext_mod.resolvePlane0MtlHandle(ext);
                    bg.texture_views[e.binding] = ext.plane0;
                }
                retain_external_texture(bg, e.binding, external_texture);
                if (ext_mod.isMultiPlane(ext)) {
                    const next_slot = e.binding + 1;
                    if (next_slot < MAX_BIND) {
                        if (is_vulkan) {
                            const plane1_view = texture_sampler.registeredTextureView(ext.plane1) orelse {
                                doeNativeBindGroupRelease(toOpaque(bg));
                                return null;
                            };
                            if (plane1_view.tex.error_object or plane1_view.tex.vk_id == 0) {
                                doeNativeBindGroupRelease(toOpaque(bg));
                                return null;
                            }
                            bg.textures[next_slot] = toOpaque(plane1_view.tex);
                            bg.texture_views[next_slot] = toOpaque(plane1_view);
                            bg.resource_kinds[next_slot] = RESOURCE_KIND_TEXTURE;
                        } else {
                            bg.textures[next_slot] = ext_mod.resolvePlane1MtlHandle(ext);
                            bg.texture_views[next_slot] = ext.plane1;
                        }
                        if (next_slot + 1 > bg.count) bg.count = next_slot + 1;
                    }
                }
                bg.resource_kinds[e.binding] = resource_kind;
                if (e.binding + 1 > bg.count) bg.count = e.binding + 1;
            }
        } else continue;
        if (e.binding < MAX_BIND) {
            if (e.binding + 1 > bg.count) bg.count = e.binding + 1;
        }
    }
    if (is_vulkan) bg.vk_buffer_binding_cache_complete = true;
    const bg_result = toOpaque(bg);
    label_store.set(bg_result, d.label.data, d.label.length);
    return bg_result;
}

pub export fn doeNativeDeviceCreateBufferBindGroupFlat4(
    dev_raw: ?*anyopaque,
    layout_raw: ?*anyopaque,
    entry_count: u32,
    b0: u32,
    buffer0_raw: ?*anyopaque,
    offset0: u64,
    b1: u32,
    buffer1_raw: ?*anyopaque,
    offset1: u64,
    b2: u32,
    buffer2_raw: ?*anyopaque,
    offset2: u64,
    b3: u32,
    buffer3_raw: ?*anyopaque,
    offset3: u64,
) callconv(.c) ?*anyopaque {
    const owner = cast(DoeDevice, dev_raw) orelse return null;
    if (!owner.requireAlive()) return null;
    if (entry_count > FLAT_BUFFER_BIND_GROUP_ENTRY_LIMIT) return null;
    if (cast(DoeBindGroupLayout, layout_raw)) |layout| {
        // The fast path's buffer entries meet the same layout checks.
        const flat_bindings = [_]u32{ b0, b1, b2, b3 };
        const flat_buffers = [_]?*anyopaque{ buffer0_raw, buffer1_raw, buffer2_raw, buffer3_raw };
        const flat_offsets = [_]u64{ offset0, offset1, offset2, offset3 };
        var entries = [_]abi_pipeline.WGPUBindGroupEntry{std.mem.zeroes(abi_pipeline.WGPUBindGroupEntry)} ** FLAT_BUFFER_BIND_GROUP_ENTRY_LIMIT;
        for (entries[0..entry_count], flat_bindings[0..entry_count], flat_buffers[0..entry_count], flat_offsets[0..entry_count]) |*entry, binding, buffer, offset| {
            entry.binding = binding;
            entry.buffer = @ptrCast(buffer);
            // The fast path binds each buffer from its offset to its end.
            entry.offset = offset;
            entry.size = abi_core.WGPU_WHOLE_SIZE;
        }
        if (bindGroupEntriesFailure(owner, layout, entries[0..entry_count])) |message| {
            owner.error_scopes.deliver(error_scope.ERROR_TYPE_VALIDATION, message);
            return null;
        }
    }
    const bg = make(DoeBindGroup) orelse return null;
    bg.* = .{};
    if (cast(DoeBindGroupLayout, layout_raw)) |layout| {
        native_helpers.object_add_ref(DoeBindGroupLayout, toOpaque(layout));
        bg.layout = layout;
    }
    const is_vulkan = if (cast(DoeDevice, dev_raw)) |dev| dev.backend == .vulkan else false;
    const bindings = [_]u32{ b0, b1, b2, b3 };
    const buffers = [_]?*anyopaque{ buffer0_raw, buffer1_raw, buffer2_raw, buffer3_raw };
    const offsets = [_]u64{ offset0, offset1, offset2, offset3 };
    const count: usize = @intCast(entry_count);
    for (0..count) |i| {
        const binding = bindings[i];
        if (binding >= MAX_BIND) continue;
        const doe_buf = cast(DoeBuffer, buffers[i]) orelse {
            doeNativeBindGroupRelease(toOpaque(bg));
            return null;
        };
        if (doe_buf.error_object or doe_buf.destroyed) {
            doeNativeBindGroupRelease(toOpaque(bg));
            return null;
        }
        bg.buffers[binding] = if (is_vulkan) toOpaque(doe_buf) else doe_buf.mtl;
        if (is_vulkan) remember_vulkan_buffer_binding(bg, @intCast(binding), doe_buf);
        bg.offsets[binding] = offsets[i];
        bg.buffer_sizes[binding] = doe_buf.size -| offsets[i];
        retain_buffer(bg, binding, doe_buf);
        if (binding + 1 > bg.count) bg.count = binding + 1;
    }
    if (is_vulkan) bg.vk_buffer_binding_cache_complete = true;
    return toOpaque(bg);
}

pub export fn doeNativeBindGroupRelease(raw: ?*anyopaque) callconv(.c) void {
    if (cast(DoeBindGroup, raw)) |g| {
        if (!native_helpers.object_should_destroy(g)) return;
        label_store.remove(raw);
        if (g.layout) |layout| doeNativeBindGroupLayoutRelease(toOpaque(layout));
        for (g.retained_buffers) |maybe_buffer| {
            if (maybe_buffer) |buffer| native_exports.doeNativeBufferRelease(toOpaque(buffer));
        }
        for (g.render_texture_views[0..@intCast(g.render_texture_view_count)]) |maybe_view| {
            if (maybe_view) |view| native_exports.doeNativeTextureViewRelease(toOpaque(view));
        }
        for (g.render_samplers[0..@intCast(g.render_sampler_count)]) |maybe_sampler| {
            if (maybe_sampler) |sampler| native_exports.doeNativeSamplerRelease(toOpaque(sampler));
        }
        for (g.retained_external_textures) |external_texture| {
            if (external_texture != null) native_exports.doeNativeExternalTextureRelease(external_texture);
        }
        alloc.destroy(g);
    }
}

/// createPipelineLayout validation
/// (https://gpuweb.github.io/gpuweb/#dom-gpudevice-createpipelinelayout):
/// immediateSize is 4-byte aligned and within maxImmediateSize, every bind
/// group layout belongs to the device, and the layouts' dynamic uniform and
/// storage buffers stay within the per-pipeline-layout limits. Returns the
/// failure message, or null when the layout is valid. No shader uses
/// immediates yet, so an accepted immediateSize is never read.
fn pipelineLayoutFailure(device: *const DoeDevice, layouts: []const ?*anyopaque, immediate_size: u32) ?[]const u8 {
    if (layouts.len > MAX_COMPUTE_BIND_GROUPS) return "createPipelineLayout: bindGroupLayouts exceeds maxBindGroups";
    if (immediate_size % 4 != 0 or immediate_size > device.max_immediate_size)
        return "createPipelineLayout: immediateSize must be a multiple of 4 and at most maxImmediateSize";
    var dynamic_uniform: u32 = 0;
    var dynamic_storage: u32 = 0;
    for (layouts) |raw| {
        const handle = raw orelse continue;
        const layout = cast(DoeBindGroupLayout, handle) orelse return "createPipelineLayout: bindGroupLayouts holds an invalid bind group layout";
        if (layout.device) |owner| if (owner != device) return "createPipelineLayout: a bind group layout belongs to a different device";
        const entries = layout.entries orelse continue;
        for (entries[0..@min(entries.len, layout.entry_count)]) |entry| {
            if (entry.resource_kind != RESOURCE_KIND_BUFFER or !entry.buffer_has_dynamic_offset) continue;
            if (entry.buffer_binding_type == abi_binding.WGPUBufferBindingType_Storage or
                entry.buffer_binding_type == abi_binding.WGPUBufferBindingType_ReadOnlyStorage)
                dynamic_storage += 1
            else
                dynamic_uniform += 1;
        }
    }
    if (dynamic_uniform > device.max_dynamic_uniform_buffers_per_pipeline_layout)
        return "createPipelineLayout: dynamic uniform buffers exceed maxDynamicUniformBuffersPerPipelineLayout";
    if (dynamic_storage > device.max_dynamic_storage_buffers_per_pipeline_layout)
        return "createPipelineLayout: dynamic storage buffers exceed maxDynamicStorageBuffersPerPipelineLayout";
    return null;
}

fn rejectPipelineLayout(device: *DoeDevice, layouts: []const ?*anyopaque, immediate_size: u32) bool {
    const message = pipelineLayoutFailure(device, layouts, immediate_size) orelse return false;
    device.error_scopes.deliver(@import("../../runtime/diagnostics/error_scope.zig").ERROR_TYPE_VALIDATION, message);
    return true;
}

pub export fn doeNativeDeviceCreatePipelineLayout(dev_raw: ?*anyopaque, desc: ?*const abi_pipeline.WGPUPipelineLayoutDescriptor) callconv(.c) ?*anyopaque {
    const owner = cast(DoeDevice, dev_raw) orelse return null;
    if (!owner.requireAlive()) return null;
    if (desc) |pd| {
        const layouts: []const ?*anyopaque = if (pd.bindGroupLayoutCount == 0) &.{} else @ptrCast(pd.bindGroupLayouts[0..pd.bindGroupLayoutCount]);
        if (rejectPipelineLayout(owner, layouts, pd.immediateSize)) return null;
    }
    const pl = make(DoePipelineLayout) orelse return null;
    pl.* = .{};
    const pl_result = toOpaque(pl);
    if (desc) |pd| {
        pl.immediate_size = pd.immediateSize;
        pl.bind_group_layout_count = @intCast(pd.bindGroupLayoutCount);
        for (0..pd.bindGroupLayoutCount) |index| {
            // WebGPU bindGroupLayouts is sequence<GPUBindGroupLayout?>: a null
            // entry is an empty group, owned here like an explicit layout.
            if (pd.bindGroupLayouts[index] == null) {
                const empty = make(DoeBindGroupLayout) orelse {
                    doeNativePipelineLayoutRelease(pl_result);
                    return null;
                };
                empty.* = .{};
                pl.bind_group_layouts[index] = empty;
                continue;
            }
            const layout = cast(DoeBindGroupLayout, pd.bindGroupLayouts[index]) orelse {
                doeNativePipelineLayoutRelease(pl_result);
                return null;
            };
            native_helpers.object_add_ref(DoeBindGroupLayout, toOpaque(layout));
            pl.bind_group_layouts[index] = layout;
        }
        label_store.set(pl_result, pd.label.data, pd.label.length);
    }
    return pl_result;
}

var next_auto_layout_id = std.atomic.Value(u64).init(1);

/// A pipeline's automatic-layout identity, assigned on first use.
pub fn pipelineAutoLayoutId(id: *u64) u64 {
    if (id.* == 0) id.* = next_auto_layout_id.fetchAdd(1, .monotonic);
    return id.*;
}

/// Marks a bind group layout as part of the automatic layout owned by `id`:
/// only that pipeline accepts groups made from it
/// (https://gpuweb.github.io/gpuweb/#default-pipeline-layout).
pub fn adoptAutoLayout(layout: *DoeBindGroupLayout, id: u64) void {
    layout.automatic = true;
    layout.auto_layout_id = id;
}

/// Makes a layout the caller built from an automatic-layout pipeline's
/// reflection part of that pipeline's automatic layout. `render` names the
/// pipeline kind (0 compute, 1 render). Returns 0, changing nothing, for a
/// pipeline with an explicit layout, a group index past maxBindGroups or a
/// layout another pipeline already owns.
pub export fn doeNativePipelineAdoptAutoBindGroupLayout(pipeline_raw: ?*anyopaque, render: u32, group_index: u32, layout_raw: ?*anyopaque) callconv(.c) u32 {
    const layout = cast(DoeBindGroupLayout, layout_raw) orelse return 0;
    if (group_index >= MAX_COMPUTE_BIND_GROUPS) return 0;
    const id_slot = if (render == 0) blk: {
        const pipeline = cast(native_types.DoeComputePipeline, pipeline_raw) orelse return 0;
        break :blk if (pipeline.layout == null) &pipeline.auto_layout_id else return 0;
    } else blk: {
        const pipeline = cast(native_types.DoeRenderPipeline, pipeline_raw) orelse return 0;
        break :blk if (pipeline.layout == null) &pipeline.auto_layout_id else return 0;
    };
    const id = pipelineAutoLayoutId(id_slot);
    if (layout.automatic and layout.auto_layout_id != id) return 0;
    adoptAutoLayout(layout, id);
    return 1;
}

pub export fn doeNativeDeviceCreatePipelineLayoutOne(dev_raw: ?*anyopaque, layout_raw: ?*anyopaque, immediate_size: u32) callconv(.c) ?*anyopaque {
    const owner = cast(DoeDevice, dev_raw) orelse return null;
    if (!owner.requireAlive()) return null;
    if (rejectPipelineLayout(owner, &.{layout_raw}, immediate_size)) return null;
    const layout = cast(DoeBindGroupLayout, layout_raw) orelse return null;
    const pl = make(DoePipelineLayout) orelse return null;
    pl.* = .{
        .immediate_size = immediate_size,
        .bind_group_layout_count = 1,
    };
    native_helpers.object_add_ref(DoeBindGroupLayout, toOpaque(layout));
    pl.bind_group_layouts[0] = layout;
    return toOpaque(pl);
}

pub export fn doeNativePipelineLayoutRelease(raw: ?*anyopaque) callconv(.c) void {
    if (cast(DoePipelineLayout, raw)) |l| {
        if (!native_helpers.object_should_destroy(l)) return;
        label_store.remove(raw);
        for (l.bind_group_layouts[0..l.bind_group_layout_count]) |layout| {
            if (layout) |bgl| doeNativeBindGroupLayoutRelease(toOpaque(bgl));
        }
        alloc.destroy(l);
    }
}

fn layout_entry_for_kind_test() abi_pipeline.WGPUBindGroupLayoutEntry {
    return .{
        .nextInChain = null,
        .binding = 0,
        .visibility = 0,
        .bindingArraySize = 0,
        .buffer = .{
            .nextInChain = null,
            .type = abi_binding.WGPUBufferBindingType_BindingNotUsed,
            .hasDynamicOffset = 0,
            .minBindingSize = 0,
        },
        .sampler = .{
            .nextInChain = null,
            .type = abi_binding.WGPUSamplerBindingType_BindingNotUsed,
        },
        .texture = .{
            .nextInChain = null,
            .sampleType = abi_binding.WGPUTextureSampleType_BindingNotUsed,
            .viewDimension = abi_texture.WGPUTextureViewDimension_Undefined,
            .multisampled = 0,
        },
        .storageTexture = .{
            .nextInChain = null,
            .access = abi_binding.WGPUStorageTextureAccess_BindingNotUsed,
            .format = abi_texture.WGPUTextureFormat_Undefined,
            .viewDimension = abi_texture.WGPUTextureViewDimension_Undefined,
        },
    };
}

test "bind group layout classification follows active binding sentinel" {
    var buffer_entry = layout_entry_for_kind_test();
    buffer_entry.buffer.type = abi_binding.WGPUBufferBindingType_Storage;
    buffer_entry.buffer.hasDynamicOffset = 1;
    buffer_entry.texture.viewDimension = abi_texture.WGPUTextureViewDimension_2D;
    buffer_entry.storageTexture.format = abi_texture.WGPUTextureFormat_RGBA8Unorm;
    const classified_buffer = classify_layout_entry(buffer_entry);
    try std.testing.expectEqual(RESOURCE_KIND_BUFFER, classified_buffer.resource_kind);
    try std.testing.expectEqual(abi_binding.WGPUBufferBindingType_Storage, classified_buffer.buffer_binding_type);
    try std.testing.expect(classified_buffer.buffer_has_dynamic_offset);

    var texture_entry = layout_entry_for_kind_test();
    texture_entry.texture.sampleType = abi_binding.WGPUTextureSampleType_Float;
    texture_entry.storageTexture.format = abi_texture.WGPUTextureFormat_RGBA8Unorm;
    texture_entry.storageTexture.viewDimension = abi_texture.WGPUTextureViewDimension_2D;
    const classified_texture = classify_layout_entry(texture_entry);
    try std.testing.expectEqual(RESOURCE_KIND_TEXTURE, classified_texture.resource_kind);
    try std.testing.expectEqual(abi_binding.WGPUTextureSampleType_Float, classified_texture.texture_sample_type);

    var storage_entry = layout_entry_for_kind_test();
    storage_entry.storageTexture.access = abi_binding.WGPUStorageTextureAccess_WriteOnly;
    storage_entry.storageTexture.format = abi_texture.WGPUTextureFormat_RGBA8Unorm;
    storage_entry.storageTexture.viewDimension = abi_texture.WGPUTextureViewDimension_2D;
    try std.testing.expectEqual(RESOURCE_KIND_STORAGE_TEXTURE, classify_layout_entry(storage_entry).resource_kind);
}

test "inactive nested binding defaults do not classify a resource kind" {
    var view_only_entry = layout_entry_for_kind_test();
    view_only_entry.texture.viewDimension = abi_texture.WGPUTextureViewDimension_2D;
    try std.testing.expectEqual(RESOURCE_KIND_NONE, classify_layout_entry(view_only_entry).resource_kind);

    var storage_shape_only_entry = layout_entry_for_kind_test();
    storage_shape_only_entry.storageTexture.format = abi_texture.WGPUTextureFormat_RGBA8Unorm;
    storage_shape_only_entry.storageTexture.viewDimension = abi_texture.WGPUTextureViewDimension_2D;
    try std.testing.expectEqual(RESOURCE_KIND_NONE, classify_layout_entry(storage_shape_only_entry).resource_kind);
}

test "external texture layout ignores Dawn undefined nested defaults" {
    try std.testing.expectEqual(@as(abi_core.WGPUSType, 0x00050001), abi_core.WGPUSType_ExternalTextureBindingEntry);
    try std.testing.expectEqual(@as(abi_core.WGPUSType, 0x00050002), abi_core.WGPUSType_ExternalTextureBindingLayout);

    var external_layout = abi_pipeline.WGPUExternalTextureBindingLayout{
        .chain = .{
            .next = null,
            .sType = abi_core.WGPUSType_ExternalTextureBindingLayout,
        },
    };
    var entry = layout_entry_for_kind_test();
    entry.nextInChain = @ptrCast(&external_layout.chain);
    entry.buffer.type = abi_binding.WGPUBufferBindingType_Undefined;
    entry.sampler.type = abi_binding.WGPUSamplerBindingType_Undefined;
    entry.texture.sampleType = abi_binding.WGPUTextureSampleType_Undefined;
    entry.storageTexture.access = abi_binding.WGPUStorageTextureAccess_Undefined;

    try std.testing.expectEqual(RESOURCE_KIND_EXTERNAL_TEXTURE, classify_layout_entry(entry).resource_kind);
}

test "doeNativePipelineAdoptAutoBindGroupLayout gives each automatic pipeline its own layout identity" {
    var first = native_types.DoeComputePipeline{};
    var second = native_types.DoeRenderPipeline{};
    var explicit_layout = DoePipelineLayout{};
    var explicit = native_types.DoeComputePipeline{ .layout = &explicit_layout };
    var a = DoeBindGroupLayout{};
    var b = DoeBindGroupLayout{};
    var c = DoeBindGroupLayout{};
    try std.testing.expectEqual(@as(u32, 1), doeNativePipelineAdoptAutoBindGroupLayout(toOpaque(&first), 0, 0, toOpaque(&a)));
    try std.testing.expectEqual(@as(u32, 1), doeNativePipelineAdoptAutoBindGroupLayout(toOpaque(&second), 1, 0, toOpaque(&b)));
    try std.testing.expect(a.automatic and b.automatic);
    try std.testing.expect(a.auto_layout_id == first.auto_layout_id and b.auto_layout_id == second.auto_layout_id);
    try std.testing.expect(first.auto_layout_id != second.auto_layout_id);
    try std.testing.expectEqual(@as(u32, 0), doeNativePipelineAdoptAutoBindGroupLayout(toOpaque(&second), 1, 0, toOpaque(&a)));
    try std.testing.expectEqual(@as(u32, 0), doeNativePipelineAdoptAutoBindGroupLayout(toOpaque(&explicit), 0, 0, toOpaque(&c)));
    try std.testing.expectEqual(@as(u32, 0), doeNativePipelineAdoptAutoBindGroupLayout(toOpaque(&first), 0, MAX_COMPUTE_BIND_GROUPS, toOpaque(&c)));
    try std.testing.expect(!c.automatic);
}

test "createPipelineLayout rejects another device's layouts and too many dynamic buffers" {
    const abi = @import("../../core/abi/wgpu_pipeline_descriptor_types.zig");
    var device = DoeDevice{};
    var other = DoeDevice{};
    var foreign = DoeBindGroupLayout{ .device = &other };
    var dynamic_entries = [_]DoeBindGroupLayoutEntry{.{ .resource_kind = RESOURCE_KIND_BUFFER, .buffer_has_dynamic_offset = true, .buffer_binding_type = abi_binding.WGPUBufferBindingType_Storage }} ** 3;
    for (&dynamic_entries, 0..) |*entry, binding| entry.binding = @intCast(binding);
    var storage = DoeBindGroupLayout{ .device = &device, .entry_count = 3, .entries = dynamic_entries[0..] };
    try std.testing.expectEqual(@as(?[]const u8, null), pipelineLayoutFailure(&device, &.{ toOpaque(&storage), null }, 0));
    try std.testing.expect(pipelineLayoutFailure(&device, &.{ toOpaque(&storage), toOpaque(&storage) }, 0) != null);
    try std.testing.expect(pipelineLayoutFailure(&device, &.{toOpaque(&foreign)}, 0) != null);
    var descriptor = std.mem.zeroes(abi.WGPUPipelineLayoutDescriptor);
    const layouts = [_]?*anyopaque{toOpaque(&foreign)};
    descriptor.bindGroupLayoutCount = 1;
    descriptor.bindGroupLayouts = @ptrCast(&layouts);
    try std.testing.expect(doeNativeDeviceCreatePipelineLayout(toOpaque(&device), &descriptor) == null);
    try std.testing.expectEqual(@as(u32, 1), foreign.ref_count);
}

test "a bind group layout entry binding at maxBindingsPerBindGroup is a validation error" {
    var device = DoeDevice{};
    const Capture = struct {
        kind: u32 = error_scope.ERROR_TYPE_NO_ERROR,
        fn receive(kind: u32, _: @import("../../core/abi/wgpu_handle_types.zig").WGPUStringView, userdata: ?*anyopaque, _: ?*anyopaque) callconv(.c) void {
            const self: *@This() = @ptrCast(@alignCast(userdata.?));
            self.kind = kind;
        }
    };
    var entry = layout_entry_for_kind_test();
    entry.buffer.type = abi_binding.WGPUBufferBindingType_Storage;
    var desc = std.mem.zeroes(abi_pipeline.WGPUBindGroupLayoutDescriptor);
    desc.entryCount = 1;
    desc.entries = @ptrCast(&entry);

    entry.binding = native_shared.MAX_BINDINGS_PER_BIND_GROUP - 1;
    device.error_scopes.push(error_scope.FILTER_VALIDATION);
    const admitted = doeNativeDeviceCreateBindGroupLayout(toOpaque(&device), &desc) orelse return error.TestUnexpectedResult;
    doeNativeBindGroupLayoutRelease(admitted);
    var capture = Capture{};
    try std.testing.expect(device.error_scopes.pop(.{ .callback = Capture.receive, .userdata1 = &capture }));
    try std.testing.expectEqual(error_scope.ERROR_TYPE_NO_ERROR, capture.kind);

    entry.binding = native_shared.MAX_BINDINGS_PER_BIND_GROUP;
    device.error_scopes.push(error_scope.FILTER_VALIDATION);
    try std.testing.expect(doeNativeDeviceCreateBindGroupLayout(toOpaque(&device), &desc) == null);
    capture = .{};
    try std.testing.expect(device.error_scopes.pop(.{ .callback = Capture.receive, .userdata1 = &capture }));
    try std.testing.expectEqual(error_scope.ERROR_TYPE_VALIDATION, capture.kind);
}

test "bind group entries must match the layout at every binding number" {
    var layout_entries = [_]DoeBindGroupLayoutEntry{
        .{ .binding = 40, .resource_kind = RESOURCE_KIND_BUFFER },
        .{ .binding = 900, .resource_kind = RESOURCE_KIND_SAMPLER },
    };
    const layout = DoeBindGroupLayout{ .entry_count = 2, .entries = layout_entries[0..] };
    var buffer = DoeBuffer{ .size = 16 };
    var sampler: u8 = 0;
    var entries = [_]abi_pipeline.WGPUBindGroupEntry{ std.mem.zeroes(abi_pipeline.WGPUBindGroupEntry), std.mem.zeroes(abi_pipeline.WGPUBindGroupEntry) };
    entries[0].binding = 40;
    entries[0].buffer = @ptrCast(&buffer);
    entries[1].binding = 900;
    entries[1].sampler = @ptrCast(&sampler);
    try std.testing.expectEqual(@as(?[]const u8, null), bindGroupEntriesFailure(null, &layout, &entries));
    // Missing entry, unknown binding, duplicate, wrong and doubled resources.
    try std.testing.expect(bindGroupEntriesFailure(null, &layout, entries[0..1]) != null);
    entries[1].binding = 901;
    try std.testing.expect(bindGroupEntriesFailure(null, &layout, &entries) != null);
    entries[1].binding = 40;
    try std.testing.expect(bindGroupEntriesFailure(null, &layout, &entries) != null);
    entries[1].binding = 900;
    entries[1].sampler = null;
    entries[1].buffer = @ptrCast(&buffer);
    try std.testing.expect(bindGroupEntriesFailure(null, &layout, &entries) != null);
    entries[1].sampler = @ptrCast(&sampler);
    try std.testing.expect(bindGroupEntriesFailure(null, &layout, &entries) != null);
}

test "bind group entries meet their layout binding's resource rules" {
    var device = DoeDevice{};
    var other = DoeDevice{};
    var uniform_entries = [_]DoeBindGroupLayoutEntry{.{ .binding = 0, .resource_kind = RESOURCE_KIND_BUFFER, .buffer_binding_type = abi_binding.WGPUBufferBindingType_Uniform, .buffer_min_binding_size = 16 }};
    const uniform_layout = DoeBindGroupLayout{ .entry_count = 1, .entries = uniform_entries[0..] };
    var buffer = DoeBuffer{ .size = 1024, .usage = abi_core.WGPUBufferUsage_Uniform, .device_ref = &device };
    var entries = [_]abi_pipeline.WGPUBindGroupEntry{std.mem.zeroes(abi_pipeline.WGPUBindGroupEntry)};
    entries[0].buffer = @ptrCast(&buffer);
    entries[0].size = abi_core.WGPU_WHOLE_SIZE;
    try std.testing.expectEqual(@as(?[]const u8, null), bindGroupEntriesFailure(&device, &uniform_layout, &entries));
    // Usage, offset alignment, empty and below-minBindingSize ranges, device.
    buffer.usage = abi_core.WGPUBufferUsage_Storage;
    try std.testing.expect(bindGroupEntriesFailure(&device, &uniform_layout, &entries) != null);
    buffer.usage = abi_core.WGPUBufferUsage_Uniform;
    entries[0].offset = 4;
    try std.testing.expect(bindGroupEntriesFailure(&device, &uniform_layout, &entries) != null);
    entries[0].offset = 1024;
    try std.testing.expect(bindGroupEntriesFailure(&device, &uniform_layout, &entries) != null);
    entries[0].offset = 0;
    entries[0].size = 8;
    try std.testing.expect(bindGroupEntriesFailure(&device, &uniform_layout, &entries) != null);
    entries[0].size = abi_core.WGPU_WHOLE_SIZE;
    buffer.device_ref = &other;
    try std.testing.expect(bindGroupEntriesFailure(&device, &uniform_layout, &entries) != null);
    buffer.device_ref = &device;

    // Sampler binding types against comparison and filtering samplers.
    var sampler_entries = [_]DoeBindGroupLayoutEntry{.{ .binding = 0, .resource_kind = RESOURCE_KIND_SAMPLER, .sampler_binding_type = abi_binding.WGPUSamplerBindingType_NonFiltering }};
    const sampler_layout = DoeBindGroupLayout{ .entry_count = 1, .entries = sampler_entries[0..] };
    var sampler = DoeSampler{ .device_ref = &device };
    entries[0] = std.mem.zeroes(abi_pipeline.WGPUBindGroupEntry);
    entries[0].sampler = @ptrCast(&sampler);
    try std.testing.expectEqual(@as(?[]const u8, null), bindGroupEntriesFailure(&device, &sampler_layout, &entries));
    sampler.is_filtering = true;
    try std.testing.expect(bindGroupEntriesFailure(&device, &sampler_layout, &entries) != null);
    sampler_entries[0].sampler_binding_type = abi_binding.WGPUSamplerBindingType_Filtering;
    try std.testing.expectEqual(@as(?[]const u8, null), bindGroupEntriesFailure(&device, &sampler_layout, &entries));
    sampler.is_comparison = true;
    try std.testing.expect(bindGroupEntriesFailure(&device, &sampler_layout, &entries) != null);
    sampler_entries[0].sampler_binding_type = abi_binding.WGPUSamplerBindingType_Comparison;
    try std.testing.expectEqual(@as(?[]const u8, null), bindGroupEntriesFailure(&device, &sampler_layout, &entries));
}

test "a texture view binds to an externalTexture entry when it is a 2d single-mip rgba8unorm, bgra8unorm or rgba16float view" {
    var device = DoeDevice{};
    var layout_entries = [_]DoeBindGroupLayoutEntry{.{ .binding = 0, .resource_kind = RESOURCE_KIND_EXTERNAL_TEXTURE }};
    const layout = DoeBindGroupLayout{ .entry_count = 1, .entries = layout_entries[0..] };
    var texture = DoeTexture{ .format = abi_texture.WGPUTextureFormat_RGBA8Unorm, .usage = abi_texture.WGPUTextureUsage_TextureBinding, .device_ref = &device };
    var view = DoeTextureView{ .tex = &texture, .dimension = abi_texture.WGPUTextureViewDimension_2D, .mip_level_count = 1, .array_layer_count = 1 };
    var entries = [_]abi_pipeline.WGPUBindGroupEntry{std.mem.zeroes(abi_pipeline.WGPUBindGroupEntry)};
    entries[0].textureView = @ptrCast(&view);
    try std.testing.expectEqual(@as(?[]const u8, null), bindGroupEntriesFailure(&device, &layout, &entries));
    texture.format = abi_texture.WGPUTextureFormat_R8Unorm;
    try std.testing.expect(bindGroupEntriesFailure(&device, &layout, &entries) != null);
    texture.format = abi_texture.WGPUTextureFormat_RGBA8Unorm;
    view.mip_level_count = 2;
    try std.testing.expect(bindGroupEntriesFailure(&device, &layout, &entries) != null);
    view.mip_level_count = 1;
    texture.usage = abi_texture.WGPUTextureUsage_CopyDst;
    try std.testing.expect(bindGroupEntriesFailure(&device, &layout, &entries) != null);
    texture.usage = abi_texture.WGPUTextureUsage_TextureBinding;
    texture.sample_count = 4;
    try std.testing.expect(bindGroupEntriesFailure(&device, &layout, &entries) != null);
}
