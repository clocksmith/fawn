/* SPDX-License-Identifier: MIT
 * Storage-texture admission against the WebGPU core format table: storage
 * formats and access modes in layouts, StorageBinding texture usage, and bind
 * group usage/format/mip checks. Host admission only; execution is native. */
#include "fixture.h"

static const WGPUTextureFormat storage[] = {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_RGBA8Uint, WGPUTextureFormat_RGBA8Sint, WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA16Sint, WGPUTextureFormat_RGBA16Float, WGPUTextureFormat_R32Uint,
    WGPUTextureFormat_R32Sint, WGPUTextureFormat_R32Float, WGPUTextureFormat_RG32Uint,
    WGPUTextureFormat_RG32Sint, WGPUTextureFormat_RG32Float, WGPUTextureFormat_RGBA32Uint,
    WGPUTextureFormat_RGBA32Sint, WGPUTextureFormat_RGBA32Float};
static const WGPUTextureFormat non_storage[] = {WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_R8Unorm, WGPUTextureFormat_RG8Unorm, WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RG16Float, WGPUTextureFormat_RGB10A2Unorm, WGPUTextureFormat_Depth32Float};

static doe_resource_status layout_status(WGPUTextureFormat format, WGPUStorageTextureAccess access)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_layout_entry entry = {.visibility = WGPUShaderStage_Compute, .kind = DOE_BIND_STORAGE_TEXTURE,
        .storage_access = access, .storage_format = format};
    doe_resource_layout *layout = NULL;
    doe_resource_status status = doe_resource_layout_create(&c, &entry, 1, false, &layout);
    if (layout) doe_resource_layout_release(layout);
    empty(&f);
    return status;
}

static doe_resource_status texture_status(WGPUTextureFormat format, WGPUTextureUsage usage)
{
    doe_resource_texture_desc desc = texture_desc();
    desc.format = format;
    desc.usage = usage;
    doe_resource_texture_limits limits = texture_limits();
    return doe_resource_texture_validate(&desc, &limits);
}

static void test_format_table(void)
{
    for (size_t i = 0; i < sizeof(storage) / sizeof(*storage); i++) {
        bool read_write = storage[i] == WGPUTextureFormat_R32Uint || storage[i] == WGPUTextureFormat_R32Sint ||
            storage[i] == WGPUTextureFormat_R32Float;
        assert(layout_status(storage[i], WGPUStorageTextureAccess_WriteOnly) == DOE_RESOURCE_OK);
        assert(layout_status(storage[i], WGPUStorageTextureAccess_ReadOnly) == DOE_RESOURCE_OK);
        assert(layout_status(storage[i], WGPUStorageTextureAccess_ReadWrite) ==
            (read_write ? DOE_RESOURCE_OK : DOE_RESOURCE_LAYOUT));
        assert(texture_status(storage[i], WGPUTextureUsage_StorageBinding) == DOE_RESOURCE_OK);
    }
    for (size_t i = 0; i < sizeof(non_storage) / sizeof(*non_storage); i++) {
        assert(layout_status(non_storage[i], WGPUStorageTextureAccess_WriteOnly) == DOE_RESOURCE_LAYOUT);
        assert(texture_status(non_storage[i], WGPUTextureUsage_StorageBinding) == DOE_RESOURCE_USAGE);
    }
}

static void test_bind_group_rules(void)
{
    fixture f = fresh();
    doe_resource_config c = config(&f);
    doe_resource_texture_limits limits = texture_limits();
    native_token tokens[7] = {{0}};
    doe_resource_texture_desc desc = texture_desc();
    desc.format = WGPUTextureFormat_R32Float;
    desc.mip_levels = 2;
    desc.usage = WGPUTextureUsage_StorageBinding;
    doe_resource_texture *storage_only, *sampled_only;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &tokens[0]), &storage_only) == DOE_RESOURCE_OK);
    desc.usage = WGPUTextureUsage_TextureBinding;
    assert(doe_resource_texture_create(&c, &desc, &limits, native(&f, &tokens[1]), &sampled_only) == DOE_RESOURCE_OK);
    doe_resource_view *single, *both_mips, *sampled, *swizzled;
    assert(doe_resource_view_create(storage_only, &(doe_resource_view_desc){.mip_count = 1},
        native(&f, &tokens[2]), &single) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(storage_only, &(doe_resource_view_desc){.mip_count = 1, .swizzled = true},
        native(&f, &tokens[6]), &swizzled) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(storage_only, &(doe_resource_view_desc){0}, native(&f, &tokens[3]), &both_mips) == DOE_RESOURCE_OK);
    assert(doe_resource_view_create(sampled_only, &(doe_resource_view_desc){.mip_count = 1},
        native(&f, &tokens[4]), &sampled) == DOE_RESOURCE_OK);
    doe_resource_layout_entry storage_entry = {.visibility = WGPUShaderStage_Compute,
        .kind = DOE_BIND_STORAGE_TEXTURE, .storage_access = WGPUStorageTextureAccess_ReadWrite,
        .storage_format = WGPUTextureFormat_R32Float};
    doe_resource_layout_entry sampled_entry = {.visibility = WGPUShaderStage_Compute,
        .kind = DOE_BIND_TEXTURE, .sample_type = WGPUTextureSampleType_UnfilterableFloat};
    doe_resource_layout *storage_layout, *sampled_layout;
    assert(doe_resource_layout_create(&c, &storage_entry, 1, false, &storage_layout) == DOE_RESOURCE_OK);
    assert(doe_resource_layout_create(&c, &sampled_entry, 1, false, &sampled_layout) == DOE_RESOURCE_OK);
    doe_resource_binding_limits binding_limits = {16, 16, 1024, 1024};
    doe_resource_bind_group *group = NULL;
    doe_resource_binding binding = {.kind = DOE_BIND_STORAGE_TEXTURE, .resource.view = single};
    assert(doe_resource_bind_group_create(storage_layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_OK);
    doe_resource_bind_group_release(group);
    binding.resource.view = both_mips; /* Storage bindings select exactly one mip. */
    assert(doe_resource_bind_group_create(storage_layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_LAYOUT);
    binding.resource.view = swizzled; /* A swizzled view is created but never storage-bound. */
    assert(doe_resource_bind_group_create(storage_layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_LAYOUT);
    binding.resource.view = sampled; /* Missing StorageBinding usage. */
    assert(doe_resource_bind_group_create(storage_layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_USAGE);
    binding = (doe_resource_binding){.kind = DOE_BIND_TEXTURE, .resource.view = single};
    /* A storage-only texture cannot be sampled, filterable or not. */
    assert(doe_resource_bind_group_create(sampled_layout, &binding, 1, &binding_limits, &group) == DOE_RESOURCE_USAGE);
    doe_resource_layout_release(storage_layout);
    doe_resource_layout_release(sampled_layout);
    doe_resource_view_release(single);
    doe_resource_view_release(both_mips);
    doe_resource_view_release(sampled);
    doe_resource_view_release(swizzled);
    doe_resource_texture_release(storage_only);
    doe_resource_texture_release(sampled_only);
    empty(&f);
}

int main(void)
{
    test_format_table();
    test_bind_group_rules();
    puts("storage format owner: 2 host suites passed; no native storage execution claimed");
    return 0;
}
