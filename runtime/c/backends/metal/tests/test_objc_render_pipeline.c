/* SPDX-License-Identifier: MIT
 * Typed construction/error/temporary-reference controls, not native Metal execution. */
#include "../src/objc_abi.h"
#include <assert.h>
#include <stdio.h>

typedef struct object { const char *kind; unsigned references, index; } object;
typedef struct setting { object *receiver; const char *selector; uint64_t value; void *object_value; } setting;
typedef struct fixture {
    doe_mtl_objc runtime;
    object pool_class, string_class, classes[5], pool, device, error, description;
    object objects[128];
    setting settings[160];
    unsigned object_count, setting_count, pools, drains, libraries, functions, strings, stage;
    unsigned fail_library, fail_function;
    const char *fail_class, *fail_selector;
    bool fail_pipeline, fail_depth;
} fixture;
static fixture *active;
static bool named(void *selector, const char *name) { return strcmp(selector, name) == 0; }
static void *selector(const char *name) { return (void *)name; }
static object *make_object(const char *kind, bool owned) {
    object *value;
    assert(active->object_count < 128);
    value = &active->objects[active->object_count++];
    *value = (object){kind, owned ? 1u : 0u, 0};
    return value;
}
static void *get_class(const char *name) {
    unsigned i;
    static const char *names[] = {"MTLRenderPipelineDescriptor", "MTLVertexDescriptor", "MTLRenderPassDescriptor",
        "MTLDepthStencilDescriptor", "MTLStencilDescriptor"};
    for (i = 0; i < 5; ++i) if (strcmp(name, names[i]) == 0) {
        active->classes[i].kind = names[i];
        return &active->classes[i];
    }
    assert(false); return NULL;
}
static void *object_call(void *receiver, void *sel) {
    object *value = receiver;
    if (receiver == &active->pool_class) {
        assert(named(sel, "new")); active->pools++; return &active->pool;
    }
    if (receiver == &active->string_class) {
        assert(named(sel, "alloc")); active->strings++; return make_object("string", true);
    }
    if (named(sel, "new")) {
        if (active->fail_class != NULL && strcmp(value->kind, active->fail_class) == 0) return NULL;
        return make_object(value->kind, true);
    }
    if (named(sel, "localizedDescription")) { assert(receiver == &active->error); return &active->description; }
    assert(named(sel, "colorAttachments") || named(sel, "layouts") || named(sel, "attributes"));
    if (active->fail_selector != NULL && named(sel, active->fail_selector)) return NULL;
    return make_object(sel, false);
}
static void void_call(void *receiver, void *sel) {
    if (named(sel, "drain")) { assert(receiver == &active->pool); active->drains++; }
    else {
        object *value = receiver;
        assert(named(sel, "release") && value->references == 1);
        value->references--;
    }
}
static void *string_init(void *receiver, void *sel, const void *bytes, size_t length, size_t encoding) {
    unsigned index = active->strings - 1;
    static const char *expected[] = {"v\0src", "v_exact", "f\0src", "f_exact"};
    static const size_t lengths[] = {5, 7, 5, 7};
    assert(named(sel, "initWithBytes:length:encoding:") && encoding == 4 && index < 4);
    assert(length == lengths[index] && memcmp(bytes, expected[index], length) == 0);
    return receiver;
}
static void *library(void *receiver, void *sel, void *source, void *options, void **error) {
    assert(receiver == &active->device && named(sel, "newLibraryWithSource:options:error:") && source != NULL && options == NULL);
    active->libraries++;
    if (active->fail_library == active->libraries) { *error = &active->error; return NULL; }
    return make_object("library", true);
}
static void *named_call(void *receiver, void *sel, void *argument) {
    object *value = receiver;
    assert(argument != NULL);
    if (named(sel, "newFunctionWithName:")) {
        assert(strcmp(value->kind, "library") == 0);
        active->functions++;
        if (active->fail_function == active->functions) return NULL;
        return make_object("function", true);
    }
    assert(receiver == &active->device && named(sel, "newDepthStencilStateWithDescriptor:"));
    return active->fail_depth ? NULL : make_object("depth-state", true);
}
static void *pipeline(void *receiver, void *sel, void *descriptor, void **error) {
    assert(receiver == &active->device && named(sel, "newRenderPipelineStateWithDescriptor:error:") && descriptor != NULL);
    if (active->fail_pipeline) { *error = &active->error; return NULL; }
    return make_object("pipeline", true);
}
static void *indexed(void *receiver, void *sel, size_t index) {
    object *array = receiver, *item;
    assert(named(sel, "objectAtIndexedSubscript:") && index < 31);
    item = make_object(array->kind, false);
    item->index = (unsigned)index;
    return item;
}
static void record(void *receiver, void *sel, uint64_t value, void *object_value) {
    setting *s;
    assert(active->setting_count < 160);
    s = &active->settings[active->setting_count++];
    *s = (setting){receiver, sel, value, object_value};
}
static void set_size(void *receiver, void *sel, size_t value) { record(receiver, sel, value, NULL); }
static void set_bool(void *receiver, void *sel, doe_objc_bool value) { record(receiver, sel, (uint64_t)(value != 0), NULL); }
static void set_u32(void *receiver, void *sel, uint32_t value) { record(receiver, sel, value, NULL); }
static void set_object(void *receiver, void *sel, void *value) { record(receiver, sel, 0, value); }
static intptr_t integer(void *receiver, void *sel) {
    assert(receiver == &active->error && named(sel, "code")); return -445;
}
static const char *text_call(void *receiver, void *sel) {
    assert(receiver == &active->description && named(sel, "UTF8String")); return "render source error preserved";
}
static uint64_t property(const char *kind, unsigned index, const char *sel) {
    unsigned i;
    for (i = 0; i < active->setting_count; ++i) {
        setting *s = &active->settings[i];
        if (strcmp(s->receiver->kind, kind) == 0 && s->receiver->index == index && strcmp(s->selector, sel) == 0)
            return s->value;
    }
    assert(false); return UINT64_MAX;
}
static void initialize(fixture *f) {
    doe_mtl_objc *r;
    memset(f, 0, sizeof(*f)); active = f; r = &f->runtime;
    r->get_class = get_class; r->selector = selector;
    assert(doe_mtl_objc_load_render(r, NULL));
    r->pool_class = &f->pool_class; r->string_class = &f->string_class;
    r->new_object = selector("new"); r->alloc = selector("alloc"); r->release = selector("release"); r->drain = selector("drain");
    r->string_init = selector("initWithBytes:length:encoding:"); r->new_library = selector("newLibraryWithSource:options:error:");
    r->new_function = selector("newFunctionWithName:"); r->set_format = selector("setPixelFormat:");
    r->localized_description = selector("localizedDescription"); r->code = selector("code"); r->utf8 = selector("UTF8String");
    r->object_call = object_call; r->void_call = void_call; r->string_init_call = string_init;
    r->library_call = library; r->named_call = named_call; r->pipeline_call = pipeline;
    r->set_size_call = set_size; r->set_object_call = set_object; r->integer_call = integer; r->string_call = text_call;
    r->graphics.index_object = indexed; r->graphics.set_bool = set_bool; r->graphics.set_u32 = set_u32;
}
static void assert_closed(const fixture *f) {
    unsigned i;
    assert(f->pools == f->drains);
    for (i = 0; i < f->object_count; ++i) assert(f->objects[i].references == 0);
}
static doe_mtl_render_pipeline_desc descriptor(const doe_mtl_vertex_layout *layout,
    const doe_mtl_vertex_attribute *attribute) {
    doe_mtl_render_pipeline_desc desc = {0};
    desc.vertex = (doe_mtl_shader_stage){"v\0src", 5, "v_exact", 30};
    desc.fragment = (doe_mtl_shader_stage){"f\0src", 5, "f_exact", 29};
    desc.color_count = 2;
    desc.colors[0] = (doe_mtl_color_target){DOE_MTL_RGBA8_UNORM, 0, false, {0, 1, 0}, {0, 1, 0}};
    desc.colors[1] = (doe_mtl_color_target){DOE_MTL_RGBA16_FLOAT, 9, true, {2, 4, 5}, {1, 11, 12}};
    desc.layouts = layout; desc.layout_count = 1; desc.attributes = attribute; desc.attribute_count = 1;
    desc.sample_count = 4; desc.alpha_to_coverage = true;
    desc.depth_stencil.depth_format = DOE_MTL_DEPTH32_FLOAT;
    desc.depth_stencil.stencil_format = DOE_MTL_STENCIL8;
    desc.depth_stencil.depth_compare = 3; desc.depth_stencil.depth_write = true;
    desc.depth_stencil.front = (doe_mtl_stencil_face){7, 5, 3, 2};
    desc.depth_stencil.back = (doe_mtl_stencil_face){6, 1, 4, 7};
    desc.depth_stencil.stencil_read_mask = UINT32_C(0xabcdef01);
    desc.depth_stencil.stencil_write_mask = UINT32_C(0x12345678);
    return desc;
}
static void construction(void) {
    fixture f;
    doe_mtl_vertex_layout layout = {5, 24, 2};
    doe_mtl_vertex_attribute attribute = {7, 5, 8, 31};
    doe_mtl_render_pipeline_desc desc = descriptor(&layout, &attribute);
    void *out = NULL, *depth = NULL;
    initialize(&f);
    assert(doe_mtl_objc_render_pipeline(&f.runtime, &f.device, &desc, &out, &depth, NULL) == DOE_MTL_OK);
    assert(f.libraries == 2 && f.functions == 2 && f.strings == 4);
    assert(property("MTLRenderPipelineDescriptor", 0, "setRasterSampleCount:") == 4);
    assert(property("MTLRenderPipelineDescriptor", 0, "setAlphaToCoverageEnabled:") == 1);
    assert(property("colorAttachments", 0, "setWriteMask:") == 0);
    assert(property("colorAttachments", 1, "setWriteMask:") == 9);
    assert(property("colorAttachments", 1, "setSourceRGBBlendFactor:") == 4);
    assert(property("colorAttachments", 1, "setRgbBlendOperation:") == 2);
    assert(property("layouts", 5, "setStride:") == 24 && property("layouts", 5, "setStepFunction:") == 2);
    assert(property("attributes", 7, "setBufferIndex:") == 5 && property("attributes", 7, "setOffset:") == 8);
    assert(property("MTLRenderPipelineDescriptor", 0, "setDepthAttachmentPixelFormat:") == 252);
    assert(property("MTLDepthStencilDescriptor", 0, "setDepthWriteEnabled:") == 1);
    assert(property("MTLStencilDescriptor", 0, "setReadMask:") == UINT32_C(0xabcdef01));
    assert(property("MTLStencilDescriptor", 0, "setStencilFailureOperation:") == 5);
    void_call(out, f.runtime.release); void_call(depth, f.runtime.release); assert_closed(&f);
}
static void failures(void) {
    unsigned failure;
    doe_mtl_vertex_layout layout = {5, 24, 2};
    doe_mtl_vertex_attribute attribute = {7, 5, 8, 31};
    doe_mtl_render_pipeline_desc desc = descriptor(&layout, &attribute);
    for (failure = 0; failure < 8; ++failure) {
        fixture f;
        doe_mtl_error error = {0};
        void *out, *depth;
        initialize(&f); out = &f.device; depth = &f.device;
        f.fail_library = failure == 0 ? 1 : failure == 1 ? 2 : 0;
        f.fail_function = failure == 2 ? 2 : 0;
        f.fail_class = failure == 3 ? "MTLVertexDescriptor" : failure == 4 ? "MTLStencilDescriptor" : NULL;
        f.fail_pipeline = failure == 5; f.fail_depth = failure == 6;
        f.fail_selector = failure == 7 ? "layouts" : NULL;
        assert(doe_mtl_objc_render_pipeline(&f.runtime, &f.device, &desc, &out, &depth, &error) == DOE_MTL_NATIVE_FAILURE);
        assert(out == &f.device && depth == &f.device);
        if (failure < 2 || failure == 5) assert(error.native_code == -445 && strstr(error.message, "preserved") != NULL);
        assert_closed(&f);
    }
}
int main(void) {
    construction(); failures();
    puts("Metal typed render pipeline controls: exact stage/descriptor/depth/stencil fields and eight failure paths passed; no SDK/GPU");
    return 0;
}
