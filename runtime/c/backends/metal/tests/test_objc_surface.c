/* SPDX-License-Identifier: MIT
 * Typed selector control only: no native drawable or display is simulated. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct fixture fixture;
typedef struct object { fixture *owner; } object;
struct fixture {
    doe_mtl_objc runtime;
    object pool_class, pool, layer, device, drawable, texture, command;
    unsigned pools, drains, retains, configured, presentations;
    bool available, missing_selector, wrong_width;
    size_t status;
};
static void *selector(const char *name) { return (void *)name; }
static bool is(void *token, const char *name) { return !strcmp(token, name); }
static fixture *owner(void *receiver) { assert(receiver); return ((object *)receiver)->owner; }
static void *object_call(void *receiver, void *token) {
    fixture *f = owner(receiver);
    if (is(token, "new")) { assert(receiver == &f->pool_class); ++f->pools; return &f->pool; }
    if (is(token, "retain")) { ++f->retains; return receiver; }
    if (is(token, "nextDrawable")) { assert(receiver == &f->layer); return f->available ? &f->drawable : NULL; }
    assert(is(token, "texture") && receiver == &f->drawable);
    return &f->texture;
}
static void void_call(void *receiver, void *token) {
    fixture *f = owner(receiver);
    assert(is(token, "drain") && receiver == &f->pool);
    ++f->drains;
}
static doe_objc_bool responds(void *receiver, void *token, void *method) {
    fixture *f = owner(receiver);
    assert(is(token, "respondsToSelector:") && method);
    return (doe_objc_bool)!f->missing_selector;
}
static void set_object(void *receiver, void *token, void *value) {
    fixture *f = owner(receiver);
    if (is(token, "setDevice:")) { assert(receiver == &f->layer && value == &f->device); ++f->configured; }
    else { assert(is(token, "presentDrawable:") && receiver == &f->command && value == &f->drawable); ++f->presentations; }
}
static void set_size(void *receiver, void *token, size_t value) {
    fixture *f = owner(receiver);
    assert(receiver == &f->layer && is(token, "setPixelFormat:") && value == DOE_MTL_BGRA8_UNORM);
}
static void set_dimensions(void *receiver, void *token, doe_mtl_drawable_size value) {
    fixture *f = owner(receiver);
    assert(receiver == &f->layer && is(token, "setDrawableSize:") && value.width == 8 && value.height == 4);
}
static void set_bool(void *receiver, void *token, doe_objc_bool value) {
    fixture *f = owner(receiver);
    assert(receiver == &f->layer);
    if (is(token, "setFramebufferOnly:")) assert(!value);
    else assert(value && (is(token, "setAllowsNextDrawableTimeout:") || is(token, "setDisplaySyncEnabled:") || is(token, "setOpaque:")));
}
static size_t size_call(void *receiver, void *token) {
    fixture *f = owner(receiver);
    if (is(token, "status")) { assert(receiver == &f->command); return f->status; }
    assert(receiver == &f->texture);
    if (is(token, "width")) return f->wrong_width ? 9 : 8;
    if (is(token, "height")) return 4;
    assert(is(token, "pixelFormat"));
    return DOE_MTL_BGRA8_UNORM;
}
int main(void) {
    fixture f = {0};
    f.pool_class.owner = f.pool.owner = f.layer.owner = f.device.owner = f.drawable.owner =
        f.texture.owner = f.command.owner = &f;
    f.runtime = (doe_mtl_objc){.selector=selector,.pool_class=&f.pool_class,.new_object=selector("new"),
        .retain=selector("retain"),.drain=selector("drain"),.responds=selector("respondsToSelector:"),
        .status=selector("status"),.object_call=object_call,.void_call=void_call,.responds_call=responds,
        .set_object_call=set_object,.set_size_call=set_size,.set_bool_call=set_bool,
        .set_drawable_size_call=set_dimensions,.size_call=size_call};
    doe_mtl_surface_desc desc = {8,4,DOE_MTL_TEXTURE_RENDER_TARGET | DOE_MTL_TEXTURE_COPY_SRC,DOE_MTL_BGRA8_UNORM,true,true};
    void *layer = NULL, *drawable = NULL, *texture = NULL;
    assert(doe_mtl_objc_surface_configure(&f.runtime, &f.device, &f.layer, &desc, &layer, NULL) == DOE_MTL_OK);
    assert(layer == &f.layer && f.configured == 1 && f.retains == 1);
    assert(doe_mtl_objc_surface_acquire(&f.runtime, layer, &desc, &drawable, &texture, NULL) == DOE_MTL_PENDING);
    assert(!drawable && !texture && f.retains == 1);
    f.available = f.wrong_width = true;
    assert(doe_mtl_objc_surface_acquire(&f.runtime, layer, &desc, &drawable, &texture, NULL) == DOE_MTL_INVALID_STATE);
    assert(!drawable && !texture && f.retains == 1);
    f.wrong_width = false;
    assert(doe_mtl_objc_surface_acquire(&f.runtime, layer, &desc, &drawable, &texture, NULL) == DOE_MTL_OK);
    assert(drawable == &f.drawable && texture == &f.texture && f.retains == 3);
    assert(doe_mtl_objc_surface_present(&f.runtime, &f.command, drawable, NULL) == DOE_MTL_OK && f.presentations == 1);
    f.status = 4;
    assert(doe_mtl_objc_surface_observe(&f.runtime, &f.command, drawable, false, NULL) == 4);
    f.status = 3;
    assert(doe_mtl_objc_surface_observe(&f.runtime, &f.command, drawable, false, NULL) == 3);
    assert(f.pools == f.drains);
    puts("PASS typed Metal surface selectors: configuration, drawable shape, native leases and bounded GPU completion after presentation registration; no display/GPU proof");
    return 0;
}
