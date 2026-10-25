/* SPDX-License-Identifier: MIT
 * Exact typed Objective-C call signatures, not an Objective-C runtime or GPU. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct property { const char *name; size_t value; } property;
typedef struct transfer {
    size_t offset, row, stride, source_slice, destination_slice, source_mip, destination_mip, options;
    doe_mtl_origin source_origin, destination_origin;
    doe_mtl_size size;
} transfer;
typedef struct fixture {
    doe_mtl_objc runtime;
    int pool, texture_class, sampler_class, descriptor, device, texture, sampler, view, command, encoder, buffer, pipeline;
    unsigned pools, drains, releases, ends, property_count, transfer_count, dispatches;
    unsigned buffer_sets, texture_sets, sampler_sets;
    bool no_pool, no_descriptor, no_result, no_encoder, no_view, no_family, float_filter;
    intptr_t family;
    property properties[16];
    transfer transfers[4];
    float min_lod, max_lod;
    size_t format, type, read_write_tier;
    doe_mtl_range_abi mips, layers;
    doe_mtl_swizzle_abi swizzle;
    void *textures[DOE_MTL_TEXTURE_SLOTS], *samplers[DOE_MTL_SAMPLER_SLOTS];
} fixture;
static fixture *active;
static bool named(void *selector, const char *name) { return strcmp(selector, name) == 0; }
static void *get_class(const char *name) {
    if (strcmp(name, "MTLTextureDescriptor") == 0) return &active->texture_class;
    assert(strcmp(name, "MTLSamplerDescriptor") == 0);
    return &active->sampler_class;
}
static void *selector(const char *name) { return (void *)name; }
static void *object_call(void *object, void *sel) {
    fixture *f = active;
    if (object == &f->pool && named(sel, "new")) {
        if (f->no_pool) return NULL;
        f->pools++;
        return &f->pool;
    }
    if (object == &f->texture_class || object == &f->sampler_class) {
        assert(named(sel, "new"));
        return f->no_descriptor ? NULL : &f->descriptor;
    }
    assert(object == &f->command && (named(sel, "blitCommandEncoder") || named(sel, "computeCommandEncoder")));
    return f->no_encoder ? NULL : &f->encoder;
}
static void void_call(void *object, void *sel) {
    if (named(sel, "drain")) { assert(object == &active->pool); active->drains++; }
    else if (named(sel, "release")) { assert(object == &active->descriptor); active->releases++; }
    else { assert(object == &active->encoder && named(sel, "endEncoding")); active->ends++; }
}
static void set_size(void *object, void *sel, size_t value) {
    fixture *f = active;
    assert(object == &f->descriptor && f->property_count < 16);
    f->properties[f->property_count++] = (property){sel, value};
}
static size_t value(const fixture *f, const char *name) {
    unsigned i;
    for (i = 0; i < f->property_count; ++i) if (strcmp(f->properties[i].name, name) == 0) return f->properties[i].value;
    assert(false);
    return SIZE_MAX;
}
static void set_float(void *object, void *sel, float v) {
    assert(object == &active->descriptor);
    if (named(sel, "setLodMinClamp:")) active->min_lod = v;
    else { assert(named(sel, "setLodMaxClamp:")); active->max_lod = v; }
}
static void *named_call(void *object, void *sel, void *descriptor) {
    assert(object == &active->device && descriptor == &active->descriptor);
    if (active->no_result) return NULL;
    if (named(sel, "newTextureWithDescriptor:")) return &active->texture;
    assert(named(sel, "newSamplerStateWithDescriptor:"));
    return &active->sampler;
}
static doe_objc_bool responds(void *object, void *sel, void *query) {
    assert(named(sel, "respondsToSelector:"));
    if (object == &active->texture) {
        assert(named(query, "newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:"));
        return (doe_objc_bool)!active->no_view;
    }
    assert(object == &active->device);
    return (doe_objc_bool)!active->no_family;
}
static doe_objc_bool family(void *object, void *sel, intptr_t v) {
    assert(object == &active->device && named(sel, "supportsFamily:"));
    return (doe_objc_bool)(v == active->family ||
        (v >= 1002 && v <= 1010 && active->family >= v && active->family <= 1010));
}
static doe_objc_bool boolean(void *object, void *sel) {
    if (named(sel, "supports32BitFloatFiltering")) {
        assert(object == &active->device);
        return (doe_objc_bool)active->float_filter;
    }
    assert(object == &active->device && (named(sel, "supportsBCTextureCompression") ||
        named(sel, "isDepth24Stencil8PixelFormatSupported")));
    return 1;
}
static size_t size_query(void *object,void *sel) {
    assert(object==&active->device && named(sel,"readWriteTextureSupport"));
    return active->read_write_tier;
}
static doe_objc_bool samples(void *object, void *sel, size_t count) {
    assert(object == &active->device && named(sel, "supportsTextureSampleCount:"));
    return (doe_objc_bool)(count == 1 || count == 4);
}
static void *view_call(void *object, void *sel, size_t format, size_t type,
    doe_mtl_range_abi mips, doe_mtl_range_abi layers, doe_mtl_swizzle_abi swizzle) {
    assert(object == &active->texture && named(sel, "newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:"));
    active->format = format; active->type = type;
    active->mips = mips; active->layers = layers; active->swizzle = swizzle;
    return active->no_result ? NULL : &active->view;
}
static void upload(void *object, void *sel, void *buffer, size_t offset, size_t row,
    size_t stride, doe_mtl_size size, void *texture, size_t slice, size_t mip,
    doe_mtl_origin origin, size_t options) {
    transfer *t = &active->transfers[active->transfer_count++];
    assert(object == &active->encoder && buffer == &active->buffer && texture == &active->texture);
    assert(named(sel, "copyFromBuffer:sourceOffset:sourceBytesPerRow:sourceBytesPerImage:sourceSize:toTexture:destinationSlice:destinationLevel:destinationOrigin:options:"));
    t->options = options; t->offset = offset; t->row = row; t->stride = stride; t->size = size;
    t->destination_slice = slice; t->destination_mip = mip; t->destination_origin = origin;
}
static void read_image(void *object, void *sel, void *texture, size_t slice, size_t mip,
    doe_mtl_origin origin, doe_mtl_size size, void *buffer, size_t offset, size_t row, size_t stride, size_t options) {
    transfer *t = &active->transfers[active->transfer_count++];
    assert(object == &active->encoder && buffer == &active->buffer && texture == &active->texture);
    assert(named(sel, "copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toBuffer:destinationOffset:destinationBytesPerRow:destinationBytesPerImage:options:"));
    t->options = options; t->offset = offset; t->row = row; t->stride = stride; t->size = size;
    t->source_slice = slice; t->source_mip = mip; t->source_origin = origin;
}
static void copy_image(void *object, void *sel, void *source, size_t source_slice, size_t source_mip,
    doe_mtl_origin source_origin, doe_mtl_size size, void *destination,
    size_t destination_slice, size_t destination_mip, doe_mtl_origin destination_origin) {
    transfer *t = &active->transfers[active->transfer_count++];
    assert(object == &active->encoder && source == &active->texture && destination == &active->view);
    assert(named(sel, "copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:destinationSlice:destinationLevel:destinationOrigin:"));
    t->source_slice = source_slice; t->source_mip = source_mip; t->source_origin = source_origin;
    t->destination_slice = destination_slice; t->destination_mip = destination_mip;
    t->destination_origin = destination_origin; t->size = size;
}
static void set_object(void *object, void *sel, void *pipeline) {
    assert(object == &active->encoder && pipeline == &active->pipeline && named(sel, "setComputePipelineState:"));
}
static void bind_buffer(void *object, void *sel, void *buffer, size_t offset, size_t slot) {
    assert(object == &active->encoder && named(sel, "setBuffer:offset:atIndex:"));
    assert(slot < DOE_MTL_BUFFER_SLOTS && buffer == NULL && offset == 0);
    active->buffer_sets++;
}
static void bind_image(void *object, void *sel, void *image, size_t slot) {
    assert(object == &active->encoder);
    if (named(sel, "setTexture:atIndex:")) {
        assert(slot < DOE_MTL_TEXTURE_SLOTS); active->textures[slot] = image; active->texture_sets++;
    } else {
        assert(named(sel, "setSamplerState:atIndex:") && slot < DOE_MTL_SAMPLER_SLOTS);
        active->samplers[slot] = image; active->sampler_sets++;
    }
}
static void dispatch(void *object, void *sel, doe_mtl_size grid, doe_mtl_size group) {
    assert(object == &active->encoder && named(sel, "dispatchThreadgroups:threadsPerThreadgroup:"));
    assert(grid.width == 3 && grid.height == 2 && grid.depth == 1 && group.width == 8);
    active->dispatches++;
}
static void initialize(fixture *f) {
    doe_mtl_objc *r;
    memset(f, 0, sizeof(*f)); active = f; r = &f->runtime;
    r->get_class = get_class; r->selector = selector;
    r->pool_class = &f->pool; r->new_object = selector("new");
    r->release = selector("release"); r->drain = selector("drain"); r->responds = selector("respondsToSelector:");
    r->blit_encoder = selector("blitCommandEncoder"); r->compute_encoder = selector("computeCommandEncoder");
    r->end_encoding = selector("endEncoding"); r->set_pipeline = selector("setComputePipelineState:");
    r->set_buffer = selector("setBuffer:offset:atIndex:"); r->dispatch = selector("dispatchThreadgroups:threadsPerThreadgroup:");
    r->object_call = object_call; r->void_call = void_call; r->set_size_call = set_size; r->size_call = size_query;
    r->set_float_call = set_float; r->named_call = named_call; r->view_call = view_call;
    r->responds_call = responds; r->family_call = family; r->bool_call = boolean; r->samples_call = samples;
    r->upload_image_call = upload; r->read_image_call = read_image; r->copy_image_call = copy_image;
    r->set_object_call = set_object; r->bind_buffer_call = bind_buffer;
    r->bind_image_call = bind_image; r->dispatch_call = dispatch;
    assert(doe_mtl_objc_load_image_selectors(r));
}
static void descriptors(void) {
    fixture f;
    doe_mtl_texture_desc desc = {DOE_MTL_TEXTURE_CUBE_ARRAY, DOE_MTL_RGBA8_UNORM, 16, 16, 12, 3,
        DOE_MTL_TEXTURE_SAMPLED | DOE_MTL_TEXTURE_STORAGE | DOE_MTL_TEXTURE_RENDER_TARGET, true, 1};
    doe_mtl_sampler_desc sd = {DOE_MTL_FILTER_LINEAR, DOE_MTL_FILTER_NEAREST, DOE_MTL_FILTER_LINEAR,
        DOE_MTL_ADDRESS_CLAMP, DOE_MTL_ADDRESS_REPEAT, DOE_MTL_ADDRESS_MIRROR, 0.5f, 6.25f, 3, 1};
    void *out = NULL;
    initialize(&f);
    assert(doe_mtl_objc_texture(&f.runtime, &f.device, &desc, &out, NULL) == DOE_MTL_OK && out == &f.texture);
    assert(value(&f, "setTextureType:") == 6 && value(&f, "setArrayLength:") == 2);
    assert(value(&f, "setDepth:") == 1 && value(&f, "setUsage:") == 23);
    assert(value(&f, "setStorageMode:") == 2 && value(&f, "setSampleCount:") == 1);
    assert(value(&f, "setMipmapLevelCount:") == 3 && value(&f, "setWidth:") == 16);
    assert(f.releases == 1 && f.pools == f.drains);
    initialize(&f);
    desc.type = DOE_MTL_TEXTURE_2D_MS_ARRAY;
    desc.sample_count = 4;
    desc.mip_levels = 1;
    assert(doe_mtl_objc_texture(&f.runtime, &f.device, &desc, &out, NULL) == DOE_MTL_OK);
    assert(value(&f, "setTextureType:") == 8 && value(&f, "setSampleCount:") == 4);
    assert(value(&f, "setArrayLength:") == 12 && value(&f, "setMipmapLevelCount:") == 1);
    initialize(&f); desc.type = DOE_MTL_TEXTURE_3D;
    desc.sample_count = 1;
    assert(doe_mtl_objc_texture(&f.runtime, &f.device, &desc, &out, NULL) == DOE_MTL_OK);
    assert(value(&f, "setArrayLength:") == 1 && value(&f, "setDepth:") == 12);
    initialize(&f);
    assert(doe_mtl_objc_sampler(&f.runtime, &f.device, &sd, &out, NULL) == DOE_MTL_OK && out == &f.sampler);
    assert(value(&f, "setMinFilter:") == 1 && value(&f, "setMagFilter:") == 0 && value(&f, "setMipFilter:") == 2);
    assert(value(&f, "setTAddressMode:") == 2 && value(&f, "setRAddressMode:") == 3);
    assert(value(&f, "setCompareFunction:") == 3 && f.min_lod == 0.5f && f.max_lod == 6.25f);
    assert(f.releases == 1 && f.pools == f.drains);
    initialize(&f); f.no_result = true; out = &f.buffer;
    assert(doe_mtl_objc_sampler(&f.runtime, &f.device, &sd, &out, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(out == &f.buffer && f.releases == 1 && f.pools == f.drains);
    initialize(&f); f.no_descriptor = true;
    assert(doe_mtl_objc_texture(&f.runtime, &f.device, &desc, &out, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(f.releases == 0 && f.pools == f.drains);
}
static void views_and_caps(void) {
    fixture f;
    doe_mtl_caps caps = {0};
    doe_mtl_texture_view_desc vd = {DOE_MTL_TEXTURE_CUBE, DOE_MTL_RGBA8_SRGB,
        2, 3, 6, 6, DOE_MTL_SWIZZLE_BLUE, DOE_MTL_SWIZZLE_GREEN, DOE_MTL_SWIZZLE_RED, DOE_MTL_SWIZZLE_ONE};
    void *out = NULL;
    initialize(&f);
    assert(sizeof(doe_mtl_swizzle_abi) == 4 && _Alignof(doe_mtl_swizzle_abi) == 1);
    assert(doe_mtl_objc_view(&f.runtime, &f.texture, &vd, &out, NULL) == DOE_MTL_OK && out == &f.view);
    assert(f.format == 71 && f.type == 5 && f.mips.location == 2 && f.mips.length == 3);
    assert(f.layers.location == 6 && f.layers.length == 6 && f.swizzle.red == 4 && f.swizzle.alpha == 1);
    f.no_view = true; out = &f.buffer;
    assert(doe_mtl_objc_view(&f.runtime, &f.texture, &vd, &out, NULL) == DOE_MTL_UNSUPPORTED && out == &f.buffer);
    f.family = 2002; f.float_filter = true; doe_mtl_objc_image_caps(&f.runtime, &f.device, &caps);
    assert(caps.max_texture_2d == 16384 && caps.max_texture_3d == 2048 && caps.bc_texture_compression);
    assert(caps.float32_filterable && caps.float32_blendable && caps.rg11b10_renderable);
    assert(caps.sample_count_mask == 5 && caps.depth24_stencil8 && caps.multisample_array && caps.store_and_resolve);
    assert(!caps.separate_depth_stencil && caps.max_threads_per_workgroup == 1024);
    assert(caps.texture_swizzle && !caps.etc2_texture_compression && !caps.astc_texture_compression && !caps.astc_sliced_3d);
    assert(caps.bgra8unorm_storage && caps.texture_formats_tier1 && !caps.texture_formats_tier2);
    f.read_write_tier=2;
    memset(&caps,0,sizeof(caps));
    doe_mtl_objc_image_caps(&f.runtime,&f.device,&caps);
    assert(caps.texture_formats_tier2);
    f.read_write_tier=1;
    memset(&caps,0,sizeof(caps));
    doe_mtl_objc_image_caps(&f.runtime,&f.device,&caps);
    assert(caps.texture_formats_tier1 && !caps.texture_formats_tier2);
    memset(&caps, 0, sizeof(caps)); f.family = 1002; f.float_filter = false;
    doe_mtl_objc_image_caps(&f.runtime, &f.device, &caps);
    assert(!caps.float32_filterable && caps.float32_blendable && caps.rg11b10_renderable);
    assert(caps.max_texture_2d == 8192 && caps.max_texture_layers == 2048 && caps.max_threads_per_workgroup == 512);
    assert(caps.texture_swizzle && caps.etc2_texture_compression && caps.astc_texture_compression && !caps.astc_sliced_3d);
    memset(&caps, 0, sizeof(caps)); f.family = 1003;
    doe_mtl_objc_image_caps(&f.runtime, &f.device, &caps);
    assert(caps.separate_depth_stencil && caps.depth_resolve && !caps.stencil_resolve && caps.max_threads_per_workgroup == 512);
    assert(caps.counting_visibility && caps.max_visibility_offset == 65528 && !caps.multisample_array);
    assert(caps.texture_swizzle && caps.etc2_texture_compression && caps.astc_texture_compression && caps.astc_sliced_3d);
    memset(&caps, 0, sizeof(caps)); f.family = 1005;
    doe_mtl_objc_image_caps(&f.runtime, &f.device, &caps);
    assert(caps.stencil_resolve && caps.max_threads_per_workgroup == 1024);
    memset(&caps, 0, sizeof(caps)); f.family = 1006;
    doe_mtl_objc_image_caps(&f.runtime, &f.device, &caps);
    assert(!caps.subgroups && !caps.subgroup_min_size && !caps.subgroup_max_size);
    memset(&caps, 0, sizeof(caps)); f.family = 1007;
    doe_mtl_objc_image_caps(&f.runtime, &f.device, &caps);
    assert(caps.subgroups && caps.subgroup_min_size == 32 && caps.subgroup_max_size == 32);
    memset(&caps, 0, sizeof(caps)); f.no_family = true;
    doe_mtl_objc_image_caps(&f.runtime, &f.device, &caps);
    assert(!caps.float32_filterable && !caps.float32_blendable && !caps.rg11b10_renderable);
    assert(caps.max_texture_2d == 0 && !caps.bc_texture_compression && !caps.max_threads_per_workgroup);
    assert(f.pools == f.drains);
}
static void transfers(void) {
    fixture f;
    doe_mtl_native_texture_copy c = {32, 256, 2048, {2, 4, 8, 3, 12, 6, 2}, false, 0};
    doe_mtl_native_texture_copy to = {0, 0, 0, {1, 8, 10, 7, 12, 6, 2}, true, 0};
    initialize(&f);
    assert(doe_mtl_objc_upload_image(&f.runtime, &f.command, &f.buffer, &f.texture, &c, NULL) == DOE_MTL_OK);
    assert(f.transfer_count == 2 && f.transfers[0].offset == 32 && f.transfers[1].offset == 2080);
    assert(f.transfers[0].stride == 0 && f.transfers[0].row == 256 && f.transfers[0].size.depth == 1);
    assert(f.transfers[0].destination_slice == 3 && f.transfers[1].destination_slice == 4);
    assert(f.transfers[0].destination_origin.x == 4 && f.transfers[0].destination_origin.z == 0);
    assert(f.ends == 1 && f.pools == f.drains);
    initialize(&f); c.volume = true;
    assert(doe_mtl_objc_read_image(&f.runtime, &f.command, &f.texture, &f.buffer, &c, NULL) == DOE_MTL_OK);
    assert(f.transfer_count == 1 && f.transfers[0].stride == 2048 && f.transfers[0].size.depth == 2);
    assert(f.transfers[0].source_slice == 0 && f.transfers[0].source_origin.z == 3);
    initialize(&f); c.volume = false;
    assert(doe_mtl_objc_copy_image(&f.runtime, &f.command, &f.texture, &c, &f.view, &to, NULL) == DOE_MTL_OK);
    assert(f.transfer_count == 2 && f.transfers[0].source_slice == 3 && f.transfers[1].source_slice == 4);
    assert(f.transfers[0].destination_slice == 0 && f.transfers[0].destination_origin.z == 7);
    assert(f.transfers[1].destination_origin.z == 8 && f.transfers[1].size.depth == 1);
    initialize(&f); c.options = 2;
    assert(doe_mtl_objc_upload_image(&f.runtime, &f.command, &f.buffer, &f.texture, &c, NULL) == DOE_MTL_OK);
    assert(f.transfers[0].options == 2 && f.transfers[1].options == 2);
    initialize(&f); c.options = 1;
    assert(doe_mtl_objc_read_image(&f.runtime, &f.command, &f.texture, &f.buffer, &c, NULL) == DOE_MTL_OK);
    assert(f.transfers[0].options == 1 && f.transfers[1].options == 1);
    initialize(&f); f.no_encoder = true;
    assert(doe_mtl_objc_upload_image(&f.runtime, &f.command, &f.buffer, &f.texture, &c, NULL) == DOE_MTL_NATIVE_FAILURE);
    assert(f.transfer_count == 0 && f.ends == 0 && f.pools == f.drains);
}
static void image_dispatch(void) {
    fixture f;
    doe_mtl_native_resources resources = {0};
    initialize(&f);
    resources.textures[7] = &f.view; resources.texture_count = 8;
    resources.samplers[3] = &f.sampler; resources.sampler_count = 4;
    assert(doe_mtl_objc_dispatch(&f.runtime, &f.command, &f.pipeline, &resources, UINT32_MAX,
        (doe_mtl_dimensions){3, 2, 1}, (doe_mtl_dimensions){8, 1, 1}, 2, NULL, NULL) == DOE_MTL_OK);
    assert(f.texture_sets == 8 && f.sampler_sets == 4 && f.buffer_sets == 31 && f.dispatches == 2);
    assert(f.textures[7] == &f.view && f.textures[6] == NULL && f.samplers[3] == &f.sampler);
    assert(f.ends == 1 && f.pools == f.drains);
}
int main(void) {
    descriptors(); views_and_caps(); transfers(); image_dispatch();
    puts("Metal typed image controls: descriptors, views, samplers, 2D/3D transfers and mixed dispatch passed; no SDK/GPU");
    return 0;
}
