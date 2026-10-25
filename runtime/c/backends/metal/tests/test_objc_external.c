/* SPDX-License-Identifier: MIT
 * Exact IOSurface/CoreVideo and Objective-C call ledger, not Apple execution. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct fixture fixture;
typedef struct object { fixture *owner; unsigned kind; } object;
enum { DEVICE, POOL, CLASS, DESCRIPTOR, SURFACE, PIXEL, PLANE0, PLANE1, EVENT, OBJECTS };
struct fixture {
    doe_mtl_objc runtime;
    object objects[OBJECTS];
    size_t count, widths[2], heights[2], expected_usage;
    unsigned cf_references, event_references, event_reads;
    uint32_t format;
    bool no_backing, no_pool, no_descriptor, no_selector;
    unsigned fail_plane, imported, descriptors, released_planes, drained, pools;
    size_t properties[10], formats[2], native_widths[2], native_heights[2];
};
static bool named(void *selector, const char *name) { return strcmp(selector, name) == 0; }
static fixture *owner(void *raw, unsigned kind)
{
    object *handle = raw;
    assert(handle && handle->kind == kind);
    return handle->owner;
}
static void *object_call(void *raw, void *selector)
{
    object *handle = raw;
    fixture *f = handle->owner;
    if (named(selector, "retain")) {
        assert(handle->kind == EVENT);
        ++f->event_references;
        return raw;
    }
    assert(named(selector, "new"));
    if (handle->kind == POOL) {
        if (f->no_pool) return NULL;
        ++f->pools;
        return &f->objects[POOL];
    }
    assert(handle->kind == CLASS);
    if (f->no_descriptor) return NULL;
    ++f->descriptors;
    return &f->objects[DESCRIPTOR];
}
static void void_call(void *raw, void *selector)
{
    object *handle = raw;
    fixture *f = handle->owner;
    if (named(selector, "drain")) {
        assert(handle->kind == POOL);
        ++f->drained;
    } else {
        assert(named(selector, "release"));
        assert(handle->kind == DESCRIPTOR || handle->kind == PLANE0 || handle->kind == PLANE1);
        if (handle->kind != DESCRIPTOR) ++f->released_planes;
    }
}
static void set_size(void *raw, void *selector, size_t value)
{
    fixture *f = owner(raw, DESCRIPTOR);
    const char *properties[] = {"type","format","width","height","depth","layers","mips","samples","storage","usage"};
    for (size_t i = 0; i < 10; ++i) if (named(selector, properties[i])) { f->properties[i] = value; return; }
    assert(false);
}
static doe_objc_bool responds(void *raw, void *selector, void *query)
{
    object *handle = raw;
    if (handle->kind == EVENT) {
        assert(named(selector,"responds") && named(query,"signaledValue"));
        return 1;
    }
    fixture *f = owner(raw, DEVICE);
    assert(named(selector,"responds") && named(query,"newTextureWithDescriptor:iosurface:plane:"));
    return (doe_objc_bool)!f->no_selector;
}
static void *texture_call(void *raw, void *selector, void *descriptor, void *surface, size_t plane)
{
    fixture *f = owner(raw, DEVICE);
    assert(owner(descriptor, DESCRIPTOR) == f && owner(surface, SURFACE) == f && plane < 2);
    assert(named(selector,"newTextureWithDescriptor:iosurface:plane:"));
    assert(f->properties[0] == DOE_MTL_TEXTURE_2D && f->properties[4] == 1 &&
        f->properties[5] == 1 && f->properties[6] == 1 && f->properties[7] == 1 &&
        f->properties[8] == 0 && f->properties[9] == f->expected_usage);
    f->formats[plane] = f->properties[1];
    f->native_widths[plane] = f->properties[2];
    f->native_heights[plane] = f->properties[3];
    ++f->imported;
    return f->fail_plane == plane + 1 ? NULL : &f->objects[PLANE0 + plane];
}
static size_t surface_planes(void *raw) { return owner(raw,SURFACE)->count; }
static size_t surface_width(void *raw) { return owner(raw,SURFACE)->widths[0]; }
static size_t surface_height(void *raw) { return owner(raw,SURFACE)->heights[0]; }
static uint32_t surface_format(void *raw) { return owner(raw,SURFACE)->format; }
static size_t surface_plane_width(void *raw, size_t plane)
{
    assert(plane < 2);
    return owner(raw,SURFACE)->widths[plane];
}
static size_t surface_plane_height(void *raw, size_t plane)
{
    assert(plane < 2);
    return owner(raw,SURFACE)->heights[plane];
}
static void *pixel_surface(void *raw)
{
    fixture *f = owner(raw,PIXEL);
    return f->no_backing ? NULL : &f->objects[SURFACE];
}
static size_t pixel_planes(void *raw) { return owner(raw,PIXEL)->count; }
static size_t pixel_width(void *raw) { return owner(raw,PIXEL)->widths[0]; }
static size_t pixel_height(void *raw) { return owner(raw,PIXEL)->heights[0]; }
static uint32_t pixel_format(void *raw) { return owner(raw,PIXEL)->format; }
static size_t pixel_plane_width(void *raw, size_t plane)
{
    assert(plane < 2);
    return owner(raw,PIXEL)->widths[plane];
}
static size_t pixel_plane_height(void *raw, size_t plane)
{
    assert(plane < 2);
    return owner(raw,PIXEL)->heights[plane];
}
static const void *cf_retain(const void *raw)
{
    fixture *f = owner((void *)raw,SURFACE);
    ++f->cf_references;
    return raw;
}
static void cf_release(const void *raw)
{
    fixture *f = owner((void *)raw,SURFACE);
    assert(f->cf_references);
    --f->cf_references;
}
static uint64_t event_value(void *raw, void *selector)
{
    fixture *f = owner(raw,EVENT);
    assert(named(selector,"signaledValue"));
    return ++f->event_reads == 3 ? 73 : 0;
}
static void initialize(fixture *f)
{
    *f = (fixture){.expected_usage=1,.count=2,.widths={16,8},.heights={8,4},.format=0x34323076};
    for (unsigned i = 0; i < OBJECTS; ++i) f->objects[i] = (object){f,i};
    f->runtime = (doe_mtl_objc){.pool_class=&f->objects[POOL],.texture_class=&f->objects[CLASS],
        .new_object="new",.retain="retain",.release="release",.drain="drain",.responds="responds",
        .set_type="type",.set_format="format",.set_width="width",.set_height="height",
        .set_depth="depth",.set_layers="layers",.set_mips="mips",.set_samples="samples",
        .set_storage="storage",.set_usage="usage",.object_call=object_call,.void_call=void_call,
        .set_size_call=set_size,.responds_call=responds};
    f->runtime.queries.signaled_value = "signaledValue";
    f->runtime.queries.event_value = event_value;
    f->runtime.external = (doe_mtl_objc_external){.new_texture="newTextureWithDescriptor:iosurface:plane:",
        .cf_retain=cf_retain,.cf_release=cf_release,.texture_call=texture_call,.surface_planes=surface_planes,.surface_width=surface_width,
        .surface_height=surface_height,.surface_plane_width=surface_plane_width,
        .surface_plane_height=surface_plane_height,.surface_format=surface_format,
        .pixel_surface=pixel_surface,.pixel_planes=pixel_planes,.pixel_width=pixel_width,
        .pixel_height=pixel_height,.pixel_plane_width=pixel_plane_width,
        .pixel_plane_height=pixel_plane_height,.pixel_format=pixel_format};
}
static doe_mtl_status run(fixture *f, doe_mtl_external_source source, doe_mtl_native_external *out, doe_mtl_error *error)
{
    return doe_mtl_objc_external_import(&f->runtime, &f->objects[DEVICE], source,
        &f->objects[source == DOE_MTL_EXTERNAL_IOSURFACE ? SURFACE : PIXEL], out, error);
}
int main(void)
{
    fixture f;
    initialize(&f);
    doe_mtl_native_external result = {.count=77};
    assert(run(&f,DOE_MTL_EXTERNAL_IOSURFACE,&result,NULL) == DOE_MTL_OK);
    assert(result.count == 2 && result.planes[0] == &f.objects[PLANE0] && result.planes[1] == &f.objects[PLANE1]);
    assert(f.formats[0] == DOE_MTL_R8_UNORM && f.formats[1] == DOE_MTL_RG8_UNORM);
    assert(f.native_widths[0] == 16 && f.native_widths[1] == 8 && f.native_heights[1] == 4);
    assert(f.pools == 1 && f.drained == 1 && !f.released_planes);
    initialize(&f);
    f.count = 0;
    f.format = 0x42475241;
    assert(run(&f,DOE_MTL_EXTERNAL_CVPIXELBUFFER,&result,NULL) == DOE_MTL_OK);
    assert(result.count == 1 && result.planes[1] == NULL && f.formats[0] == DOE_MTL_BGRA8_UNORM);
    initialize(&f);
    f.fail_plane = 2;
    result.count = 77;
    doe_mtl_error error = {0};
    assert(run(&f,DOE_MTL_EXTERNAL_CVPIXELBUFFER,&result,&error) == DOE_MTL_NATIVE_FAILURE);
    assert(result.count == 77 && f.released_planes == 1 && f.drained == 1);
    assert(!strcmp(error.operation,"newTextureWithDescriptor:iosurface:plane:"));
    initialize(&f);
    f.format = 0;
    assert(run(&f,DOE_MTL_EXTERNAL_IOSURFACE,&result,NULL) == DOE_MTL_UNSUPPORTED);
    assert(!f.imported && !f.pools && result.count == 77);
    initialize(&f);
    f.count = 3;
    assert(run(&f,DOE_MTL_EXTERNAL_IOSURFACE,&result,NULL) == DOE_MTL_UNSUPPORTED);
    initialize(&f);
    f.widths[1] = (size_t)UINT32_MAX + 1;
    assert(run(&f,DOE_MTL_EXTERNAL_IOSURFACE,&result,NULL) == DOE_MTL_RANGE);
    assert(!f.imported && result.count == 77);
    initialize(&f);
    f.no_backing = true;
    assert(run(&f,DOE_MTL_EXTERNAL_CVPIXELBUFFER,&result,NULL) == DOE_MTL_UNSUPPORTED);
    initialize(&f);
    f.no_pool = true;
    assert(run(&f,DOE_MTL_EXTERNAL_IOSURFACE,&result,NULL) == DOE_MTL_NATIVE_FAILURE);
    initialize(&f);
    f.no_descriptor = true;
    assert(run(&f,DOE_MTL_EXTERNAL_IOSURFACE,&result,NULL) == DOE_MTL_OUT_OF_MEMORY);
    assert(f.drained == 1 && result.count == 77);
    initialize(&f);
    void *retained = NULL;
    uint32_t width = 99, height = 98;
    assert(doe_mtl_objc_shared_surface(&f.runtime,&f.objects[SURFACE],&retained,&width,&height,NULL) == DOE_MTL_UNSUPPORTED);
    assert(!retained && width == 99 && height == 98 && !f.cf_references);
    f.count = 0;
    assert(doe_mtl_objc_shared_surface(&f.runtime,&f.objects[SURFACE],&retained,&width,&height,NULL) == DOE_MTL_OK);
    assert(retained == &f.objects[SURFACE] && width == 16 && height == 8 && f.cf_references == 1);
    f.expected_usage = 21;
    doe_mtl_texture_desc shared = {.type=DOE_MTL_TEXTURE_2D,.format=DOE_MTL_BGRA8_SRGB,
        .width=16,.height=8,.depth_or_layers=1,.mip_levels=1,.sample_count=1,
        .usage=DOE_MTL_TEXTURE_SAMPLED | DOE_MTL_TEXTURE_COPY_SRC | DOE_MTL_TEXTURE_RENDER_TARGET,
        .allow_format_views=true};
    void *texture = NULL;
    assert(doe_mtl_objc_shared_texture(&f.runtime,&f.objects[DEVICE],retained,&shared,&texture,NULL) == DOE_MTL_OK);
    assert(texture == &f.objects[PLANE0] && f.formats[0] == DOE_MTL_BGRA8_SRGB && f.properties[9] == 21);
    shared.width = 15;
    texture = NULL;
    assert(doe_mtl_objc_shared_texture(&f.runtime,&f.objects[DEVICE],retained,&shared,&texture,NULL) == DOE_MTL_INVALID_ARGUMENT && !texture);
    doe_mtl_objc_shared_surface_release(&f.runtime,retained);
    assert(!f.cf_references);
    void *event = NULL;
    assert(doe_mtl_objc_shared_event_retain(&f.runtime,&f.objects[EVENT],&event,NULL) == DOE_MTL_OK);
    assert(event == &f.objects[EVENT] && f.event_references == 1);
    assert(doe_mtl_objc_shared_event_wait(&f.runtime,event,73,NULL) == DOE_MTL_OK && f.event_reads == 3);
    puts("PASS shared import typed calls: CF/native identity, packed-plane admission, sRGB/usage and producer-event reads (host only)");
    puts("PASS external import typed calls: exact format/dimensions/source, second-plane rollback and untouched failures (host only)");
}
