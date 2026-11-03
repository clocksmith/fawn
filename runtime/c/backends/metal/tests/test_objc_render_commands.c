/* SPDX-License-Identifier: MIT
 * Exact render encoder ABI/argument/state controls, no rasterizer. */
#include "../src/objc_abi.h"
#include "../src/texture_internal.h"
#include <assert.h>
#include <stdio.h>

/* Dense input is test construction convenience only. The call below converts
 * occupied slots to the same compact borrowed spans production consumes. */
typedef struct fixture_draw {
    void *pipeline, *depth_stencil;
    doe_mtl_native_resources vertex, fragment;
    uint32_t vertex_sizes_slot, fragment_sizes_slot;
    void *index_buffer;
    size_t index_offset;
    void *indirect_arguments;
    size_t indirect_offset;
    doe_mtl_render_draw values;
} fixture_draw;
static doe_mtl_status fixture_render(void *userdata, void *command,
    const doe_mtl_native_render_pass *pass, const fixture_draw *inputs, size_t count, doe_mtl_error *error) {
    assert(count <= 16);
    doe_mtl_native_render_draw draws[16] = {0};
    doe_mtl_buffer buffers[16][2][DOE_MTL_BUFFER_SLOTS] = {0};
    doe_mtl_texture_view views[16][2][DOE_MTL_TEXTURE_SLOTS] = {0};
    doe_mtl_sampler samplers[16][2][DOE_MTL_SAMPLER_SLOTS] = {0};
    doe_mtl_buffer_binding buffer_bindings[16][2][DOE_MTL_BUFFER_SLOTS] = {0};
    doe_mtl_texture_binding texture_bindings[16][2][DOE_MTL_TEXTURE_SLOTS] = {0};
    doe_mtl_sampler_binding sampler_bindings[16][2][DOE_MTL_SAMPLER_SLOTS] = {0};
    for (size_t i = 0; i < count; ++i) {
        draws[i] = (doe_mtl_native_render_draw){.pipeline=inputs[i].pipeline,.depth_stencil=inputs[i].depth_stencil,
            .vertex_sizes_slot=inputs[i].vertex_sizes_slot,.fragment_sizes_slot=inputs[i].fragment_sizes_slot,
            .index_buffer=inputs[i].index_buffer,.index_offset=inputs[i].index_offset,
            .indirect_arguments=inputs[i].indirect_arguments,.indirect_offset=inputs[i].indirect_offset,.values=inputs[i].values};
        for (size_t stage = 0; stage < 2; ++stage) {
            const doe_mtl_native_resources *source = stage ? &inputs[i].fragment : &inputs[i].vertex;
            doe_mtl_render_bindings *out = stage ? &draws[i].values.fragment : &draws[i].values.vertex;
            *out = (doe_mtl_render_bindings){.buffers=buffer_bindings[i][stage],.textures=texture_bindings[i][stage],
                .samplers=sampler_bindings[i][stage],.immediates={source->immediate_words,source->immediate_size,source->immediate_slot}};
            for (uint32_t slot = 0; slot < DOE_MTL_BUFFER_SLOTS; ++slot) if (source->buffers[slot].buffer) {
                buffers[i][stage][slot].native = source->buffers[slot].buffer;
                buffer_bindings[i][stage][out->buffer_count++] = (doe_mtl_buffer_binding){slot,
                    &buffers[i][stage][slot],source->buffers[slot].offset,source->buffers[slot].size};
            }
            for (uint32_t slot = 0; slot < DOE_MTL_TEXTURE_SLOTS; ++slot) if (source->textures[slot]) {
                views[i][stage][slot].native = source->textures[slot];
                texture_bindings[i][stage][out->texture_count++] = (doe_mtl_texture_binding){slot,&views[i][stage][slot],false};
            }
            for (uint32_t slot = 0; slot < DOE_MTL_SAMPLER_SLOTS; ++slot) if (source->samplers[slot]) {
                samplers[i][stage][slot].native = source->samplers[slot];
                sampler_bindings[i][stage][out->sampler_count++] = (doe_mtl_sampler_binding){slot,&samplers[i][stage][slot]};
            }
        }
    }
    return doe_mtl_objc_render(userdata, command, pass, draws, count, error);
}

typedef struct token { unsigned kind, index; } token;
typedef struct fixture {
    doe_mtl_objc runtime;
    token pool, classes[5], descriptor, colors, attachments[8], depth, stencil, encoder, command;
    token timestamp_attachments, timestamp_attachment;
    int pipeline, depth_state, buffer, index_buffer, texture, sampler;
    unsigned pools, drains, releases, begins, ends, draws, indexed_draws, indirect_draws, indexed_indirect_draws;
    unsigned pipeline_sets, depth_sets, viewport_sets, scissor_sets, winding_sets, cull_sets, blend_sets, bias_sets, stencil_sets;
    unsigned buffer_sets[2], texture_sets[2], sampler_sets[2], bytes_sets[2];
    bool no_descriptor, no_encoder, no_attachment, no_timestamp_attachment, no_timestamp_selector, timestamp_expected;
    void *timestamp_buffer;
    size_t timestamp_begin, timestamp_end, timestamp_vertex_end, timestamp_fragment_begin;
    size_t load[10], store[10], level[10], slice[10];
    void *attachment_texture[10], *resolve_texture[10], *visibility_buffer;
    size_t resolve_level[10], resolve_slice[10], resolve_filter[10];
    size_t visibility_modes[8], visibility_offsets[8];
    unsigned visibility_sets;
    doe_mtl_clear_color clear;
    double clear_depth;
    uint32_t clear_stencil, stencil_reference;
    doe_mtl_viewport viewport;
    doe_mtl_scissor_abi scissor;
    float blend[4], bias[3];
    size_t depth_clip, depth_clip_values[16], depth_clip_count, winding, cull, topology, first_vertex, count, instances, first_instance, index_type, index_offset;
    intptr_t base_vertex;
    void *vertex_textures[DOE_MTL_TEXTURE_SLOTS], *fragment_textures[DOE_MTL_TEXTURE_SLOTS];
    void *vertex_samplers[DOE_MTL_SAMPLER_SLOTS], *fragment_samplers[DOE_MTL_SAMPLER_SLOTS];
    doe_mtl_native_binding buffers[2][DOE_MTL_BUFFER_SLOTS];
    uint32_t sizes[2][DOE_MTL_BUFFER_SLOTS];
    bool bytes_bound[2][DOE_MTL_BUFFER_SLOTS];
} fixture;
static fixture *active;
static bool named(void *sel, const char *name) { return strcmp(sel, name) == 0; }
static void *selector(const char *name) { return (void *)name; }
static void *get_class(const char *name) {
    static const char *names[] = {"MTLRenderPipelineDescriptor", "MTLVertexDescriptor", "MTLRenderPassDescriptor",
        "MTLDepthStencilDescriptor", "MTLStencilDescriptor"};
    unsigned i;
    for (i = 0; i < 5; ++i) if (strcmp(name, names[i]) == 0) return &active->classes[i];
    assert(false); return NULL;
}
static void *object_call(void *object, void *sel) {
    if (object == &active->pool) { assert(named(sel, "new")); active->pools++; return &active->pool; }
    if (object == &active->classes[2]) {
        assert(named(sel, "new")); return active->no_descriptor ? NULL : &active->descriptor;
    }
    assert(object == &active->descriptor);
    if (named(sel, "colorAttachments")) return &active->colors;
    if (named(sel, "depthAttachment")) return &active->depth;
    if (named(sel, "sampleBufferAttachments")) return &active->timestamp_attachments;
    assert(named(sel, "stencilAttachment")); return &active->stencil;
}
static void void_call(void *object, void *sel) {
    if (named(sel, "drain")) { assert(object == &active->pool); active->drains++; }
    else if (named(sel, "release")) { assert(object == &active->descriptor); active->releases++; }
    else { assert(object == &active->encoder && named(sel, "endEncoding")); active->ends++; }
}
static void *indexed(void *object, void *sel, size_t index) {
    if (object == &active->timestamp_attachments) {
        assert(index == 0 && named(sel, "objectAtIndexedSubscript:"));
        return active->no_timestamp_attachment ? NULL : &active->timestamp_attachment;
    }
    assert(object == &active->colors && named(sel, "objectAtIndexedSubscript:") && index < 8);
    return active->no_attachment ? NULL : &active->attachments[index];
}
static void set_object(void *object, void *sel, void *value) {
    if (named(sel, "setSampleBuffer:")) {
        assert(object == &active->timestamp_attachment);
        active->timestamp_buffer = value;
    } else if (named(sel, "setVisibilityResultBuffer:")) {
        assert(object == &active->descriptor);
        active->visibility_buffer = value;
    } else if (named(sel, "setResolveTexture:")) {
        token *t = object;
        assert(t->index < 10);
        active->resolve_texture[t->index] = value;
    } else if (named(sel, "setTexture:")) {
        token *t = object;
        assert(t->index < 10); active->attachment_texture[t->index] = value;
    } else {
        assert(object == &active->encoder);
        if (named(sel, "setRenderPipelineState:")) { assert(value == &active->pipeline || value == &active->index_buffer); ++active->pipeline_sets; }
        else { assert(named(sel, "setDepthStencilState:")); assert(value == NULL || value == &active->depth_state); ++active->depth_sets; }
    }
}
static void set_size(void *object, void *sel, size_t value) {
    token *t = object;
    if (object == &active->timestamp_attachment) {
        if (named(sel, "setStartOfVertexSampleIndex:")) active->timestamp_begin = value;
        else if (named(sel, "setEndOfVertexSampleIndex:")) active->timestamp_vertex_end = value;
        else if (named(sel, "setStartOfFragmentSampleIndex:")) active->timestamp_fragment_begin = value;
        else { assert(named(sel, "setEndOfFragmentSampleIndex:")); active->timestamp_end = value; }
    } else if (object == &active->encoder) {
        if (named(sel, "setDepthClipMode:")) {
            assert(active->depth_clip_count < 16);
            active->depth_clip_values[active->depth_clip_count++] = value;
            active->depth_clip = value;
        }
        else if (named(sel, "setFrontFacingWinding:")) { active->winding = value; ++active->winding_sets; }
        else { assert(named(sel, "setCullMode:")); active->cull = value; ++active->cull_sets; }
    } else {
        assert(t->index < 10);
        if (named(sel, "setLoadAction:")) active->load[t->index] = value;
        else if (named(sel, "setStoreAction:")) active->store[t->index] = value;
        else if (named(sel, "setLevel:")) active->level[t->index] = value;
        else if (named(sel, "setResolveLevel:")) active->resolve_level[t->index] = value;
        else if (named(sel, "setResolveSlice:")) active->resolve_slice[t->index] = value;
        else if (named(sel, "setDepthResolveFilter:") || named(sel, "setStencilResolveFilter:"))
            active->resolve_filter[t->index] = value;
        else { assert(named(sel, "setSlice:")); active->slice[t->index] = value; }
    }
}
static void clear(void *object, void *sel, doe_mtl_clear_color value) {
    assert(object == &active->attachments[0] && named(sel, "setClearColor:")); active->clear = value;
}
static void set_double(void *object, void *sel, double value) {
    assert(object == &active->depth && named(sel, "setClearDepth:")); active->clear_depth = value;
}
static void set_u32(void *object, void *sel, uint32_t value) {
    if (named(sel, "setClearStencil:")) { assert(object == &active->stencil); active->clear_stencil = value; }
    else { assert(object == &active->encoder && named(sel, "setStencilReferenceValue:")); active->stencil_reference = value; ++active->stencil_sets; }
}
static void *named_call(void *object, void *sel, void *descriptor) {
    assert(object == &active->command && named(sel, "renderCommandEncoderWithDescriptor:") && descriptor == &active->descriptor);
    assert(active->releases == 0 && active->drains == 0);
    if (active->timestamp_expected) {
        assert(active->timestamp_buffer == &active->buffer);
        assert(active->timestamp_vertex_end == SIZE_MAX && active->timestamp_fragment_begin == SIZE_MAX);
    }
    active->begins++;
    memset(active->buffers, 0, sizeof(active->buffers));
    memset(active->vertex_textures, 0, sizeof(active->vertex_textures));
    memset(active->fragment_textures, 0, sizeof(active->fragment_textures));
    memset(active->vertex_samplers, 0, sizeof(active->vertex_samplers));
    memset(active->fragment_samplers, 0, sizeof(active->fragment_samplers));
    memset(active->bytes_bound, 0, sizeof(active->bytes_bound));
    return active->no_encoder ? NULL : &active->encoder;
}
static void viewport(void *object, void *sel, doe_mtl_viewport value) {
    assert(object == &active->encoder && named(sel, "setViewport:")); active->viewport = value; ++active->viewport_sets;
}
static void scissor(void *object, void *sel, doe_mtl_scissor_abi value) {
    assert(object == &active->encoder && named(sel, "setScissorRect:")); active->scissor = value; ++active->scissor_sets;
}
static void blend(void *object, void *sel, float r, float g, float b, float a) {
    assert(object == &active->encoder && named(sel, "setBlendColorRed:green:blue:alpha:"));
    ++active->blend_sets;
    active->blend[0] = r; active->blend[1] = g; active->blend[2] = b; active->blend[3] = a;
}
static void bias(void *object, void *sel, float constant, float slope, float clamp) {
    assert(object == &active->encoder && named(sel, "setDepthBias:slopeScale:clamp:"));
    ++active->bias_sets;
    active->bias[0] = constant; active->bias[1] = slope; active->bias[2] = clamp;
}
static void bind_buffer(void *object, void *sel, void *buffer, size_t offset, size_t slot) {
    unsigned stage = named(sel, "setVertexBuffer:offset:atIndex:") ? 0u : 1u;
    assert(object == &active->encoder && slot < DOE_MTL_BUFFER_SLOTS);
    if (stage == 1) assert(named(sel, "setFragmentBuffer:offset:atIndex:"));
    active->buffers[stage][slot] = (doe_mtl_native_binding){buffer, offset, 0}; active->buffer_sets[stage]++;
    active->bytes_bound[stage][slot] = false;
}
static void bind_bytes(void *object, void *sel, const void *bytes, size_t length, size_t slot) {
    unsigned stage = named(sel, "setVertexBytes:length:atIndex:") ? 0u : 1u;
    assert(object == &active->encoder && slot < DOE_MTL_BUFFER_SLOTS && length && length <= sizeof(active->sizes[stage]));
    if (stage == 1) assert(named(sel, "setFragmentBytes:length:atIndex:"));
    memcpy(active->sizes[stage], bytes, length); active->bytes_sets[stage]++;
    active->buffers[stage][slot] = (doe_mtl_native_binding){0};
    active->bytes_bound[stage][slot] = true;
}
static void bind_image(void *object, void *sel, void *image, size_t slot) {
    assert(object == &active->encoder);
    if (named(sel, "setVertexTexture:atIndex:")) {
        assert(slot < DOE_MTL_TEXTURE_SLOTS); active->vertex_textures[slot] = image; active->texture_sets[0]++;
    } else if (named(sel, "setFragmentTexture:atIndex:")) {
        assert(slot < DOE_MTL_TEXTURE_SLOTS); active->fragment_textures[slot] = image; active->texture_sets[1]++;
    } else if (named(sel, "setVertexSamplerState:atIndex:")) {
        assert(slot < DOE_MTL_SAMPLER_SLOTS); active->vertex_samplers[slot] = image; active->sampler_sets[0]++;
    } else {
        assert(named(sel, "setFragmentSamplerState:atIndex:") && slot < DOE_MTL_SAMPLER_SLOTS);
        active->fragment_samplers[slot] = image; active->sampler_sets[1]++;
    }
}
static void draw(void *object, void *sel, size_t topology, size_t first, size_t count, size_t instances, size_t first_instance) {
    assert(object == &active->encoder && named(sel, "drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:"));
    active->topology = topology; active->first_vertex = first; active->count = count;
    active->instances = instances; active->first_instance = first_instance; active->draws++;
    assert(active->vertex_textures[127] == &active->texture && active->fragment_samplers[15] == &active->sampler);
}
static void draw_indexed(void *object, void *sel, size_t topology, size_t count, size_t type,
    void *buffer, size_t offset, size_t instances, intptr_t base_vertex, size_t first_instance) {
    assert(object == &active->encoder && buffer == &active->index_buffer);
    assert(named(sel, "drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:"));
    active->topology = topology; active->count = count; active->index_type = type; active->index_offset = offset;
    active->instances = instances; active->base_vertex = base_vertex; active->first_instance = first_instance; active->indexed_draws++;
    assert(active->vertex_textures[127] == NULL && active->fragment_samplers[15] == NULL);
}
static void visibility(void *object, void *sel, size_t mode, size_t offset) {
    unsigned i = active->visibility_sets++;
    assert(object == &active->encoder && named(sel, "setVisibilityResultMode:offset:") && i < 8);
    active->visibility_modes[i] = mode;
    active->visibility_offsets[i] = offset;
}
static void indirect(void *object,void *sel,size_t topology,void *arguments,size_t offset) {
    assert(object==&active->encoder && named(sel,"drawPrimitives:indirectBuffer:indirectBufferOffset:"));
    assert(topology==3 && arguments==&active->buffer && offset==40);
    ++active->indirect_draws;
}
static void indexed_indirect(void *object,void *sel,size_t topology,size_t type,
    void *indices,size_t offset,void *arguments,size_t argument_offset) {
    assert(object==&active->encoder && named(sel,"drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:"));
    assert(topology==4 && type==0 && indices==&active->index_buffer && offset==2);
    assert(arguments==&active->buffer && argument_offset==80);
    ++active->indexed_indirect_draws;
}
static doe_objc_bool responds_call(void *object, void *sel, void *method) {
    assert(object == &active->descriptor && named(sel, "respondsToSelector:") && named(method, "sampleBufferAttachments"));
    return (doe_objc_bool)!active->no_timestamp_selector;
}
static void initialize(fixture *f) {
    doe_mtl_objc *r;
    unsigned i;
    memset(f, 0, sizeof(*f)); active = f; r = &f->runtime;
    r->get_class = get_class; r->selector = selector; assert(doe_mtl_objc_load_render(r, NULL));
    r->responds = selector("respondsToSelector:"); r->responds_call = responds_call;
    r->queries.attachments = selector("sampleBufferAttachments");
    r->queries.set_sample_buffer = selector("setSampleBuffer:");
    r->queries.set_vertex_begin = selector("setStartOfVertexSampleIndex:");
    r->queries.set_vertex_end = selector("setEndOfVertexSampleIndex:");
    r->queries.set_fragment_begin = selector("setStartOfFragmentSampleIndex:");
    r->queries.set_fragment_end = selector("setEndOfFragmentSampleIndex:");
    for (i = 0; i < 8; ++i) f->attachments[i].index = i;
    f->depth.index = 8; f->stencil.index = 9;
    r->pool_class = &f->pool; r->new_object = selector("new"); r->release = selector("release");
    r->drain = selector("drain"); r->end_encoding = selector("endEncoding");
    r->object_call = object_call; r->void_call = void_call; r->named_call = named_call;
    r->set_size_call = set_size; r->set_object_call = set_object; r->bind_buffer_call = bind_buffer;
    r->bind_bytes_call = bind_bytes; r->bind_image_call = bind_image;
    r->graphics.index_object = indexed; r->graphics.clear_color = clear; r->graphics.set_double = set_double;
    r->graphics.set_u32 = set_u32; r->graphics.viewport = viewport; r->graphics.scissor = scissor;
    r->graphics.blend_color = blend; r->graphics.depth_bias = bias; r->graphics.draw = draw; r->graphics.draw_indexed = draw_indexed; r->graphics.visibility = visibility;
    r->graphics.draw_indirect=indirect; r->graphics.draw_indexed_indirect=indexed_indirect;
}
static doe_mtl_native_render_pass pass_new(fixture *f) {
    doe_mtl_native_render_pass pass = {0};
    pass.width = 64; pass.height = 32; pass.color_count = 2;
    pass.colors[0] = (doe_mtl_native_render_attachment){&f->texture, DOE_MTL_CLEAR, DOE_MTL_STORE, {1, 0.5, 0.25, 0}, 0, 0, NULL, 0};
    pass.depth = (doe_mtl_native_render_attachment){&f->texture, DOE_MTL_CLEAR, DOE_MTL_STORE_DISCARD, {0, 0, 0, 0}, 0.75, 0, NULL, 0};
    pass.stencil = (doe_mtl_native_render_attachment){&f->texture, DOE_MTL_CLEAR, DOE_MTL_STORE, {0, 0, 0, 0}, 0, 254, NULL, 0};
    return pass;
}
static void encoding(void) {
    fixture f;
    doe_mtl_native_render_pass pass;
    fixture_draw draws[2] = {0};
    initialize(&f); pass = pass_new(&f);
    draws[0].pipeline = &f.pipeline; draws[0].depth_stencil = &f.depth_state;
    draws[0].vertex_sizes_slot = 30; draws[0].fragment_sizes_slot = UINT32_MAX;
    draws[0].vertex.buffers[4] = (doe_mtl_native_binding){&f.buffer, 12, 20};
    draws[0].vertex.textures[127] = &f.texture; draws[0].fragment.samplers[15] = &f.sampler;
    draws[0].vertex.texture_count = 128; draws[0].fragment.sampler_count = 16;
    draws[0].values = (doe_mtl_render_draw){.topology = 4, .count = 5, .instance_count = 3, .first_vertex = 7, .first_instance = 2,
        .unclipped_depth = true, .viewport_enabled = true, .viewport = {1, 2, 10, 11, 0.1, 0.9}, .scissor_enabled = true, .scissor = {3, 4, 8, 9}};
    draws[1].pipeline = &f.pipeline; draws[1].vertex_sizes_slot = UINT32_MAX; draws[1].fragment_sizes_slot = UINT32_MAX;
    draws[1].index_buffer = &f.index_buffer; draws[1].index_offset = 24;
    draws[1].values = (doe_mtl_render_draw){.topology = 3, .count = 6, .instance_count = 4,
        .first_instance = 8, .index_type = 1, .base_vertex = -11, .winding = 1, .cull = 2,
        .blend_constant = {0.25f, 0.5f, 0.75f, 1}, .depth_bias = -2, .depth_slope = 3, .depth_clamp = 4, .stencil_reference = 255};
    assert(fixture_render(&f.runtime, &f.command, &pass, draws, 2, NULL) == DOE_MTL_OK);
    assert(f.draws == 1 && f.indexed_draws == 1 && f.index_offset == 24 && f.base_vertex == -11 && f.first_instance == 8);
    assert(f.clear.red == 1 && f.clear.blue == 0.25 && f.clear_depth == 0.75 && f.clear_stencil == 254);
    assert(f.store[8] == 0 && f.load[0] == 2 && f.attachment_texture[1] == NULL && f.level[0] == 0 && f.slice[0] == 0);
    assert(f.buffer_sets[0] == 3 && f.texture_sets[0] == 2 && f.sampler_sets[1] == 2);
    assert(f.bytes_sets[0] == 1 && f.sizes[0][4] == 20 && f.sizes[0][3] == 0);
    assert(f.viewport.width == 64 && f.viewport.min_depth == 0 && f.scissor.width == 64 && f.scissor.height == 32);
    assert(f.depth_clip_count == 2 && f.depth_clip_values[0] == 1 && f.depth_clip_values[1] == 0 && f.depth_clip == 0);
    assert(f.blend[2] == 0.75f && f.bias[0] == -2 && f.winding == 1 && f.cull == 2 && f.stencil_reference == 255);
    assert(f.begins == 1 && f.ends == 1 && f.releases == 1 && f.pools == f.drains);
}
static void clear_and_failure(void) {
    unsigned failure;
    for (failure = 0; failure < 4; ++failure) {
        fixture f;
        doe_mtl_native_render_pass pass;
        initialize(&f); pass = pass_new(&f);
        f.no_descriptor = failure == 1; f.no_attachment = failure == 2; f.no_encoder = failure == 3;
        assert(fixture_render(&f.runtime, &f.command, &pass, NULL, 0, NULL) ==
            (failure == 0 ? DOE_MTL_OK : DOE_MTL_NATIVE_FAILURE));
        assert(f.draws == 0 && f.indexed_draws == 0 && f.pools == f.drains);
        assert(f.ends == (failure == 0 ? 1u : 0u));
        assert(f.releases == (failure == 1 ? 0u : 1u));
    }
}
static void resolves_and_visibility(void) {
    fixture f;
    doe_mtl_native_render_pass pass;
    fixture_draw draws[4] = {0};
    unsigned i;
    initialize(&f);
    pass = pass_new(&f);
    pass.samples = 4;
    pass.colors[0].resolve = &f.buffer;
    pass.colors[0].store = DOE_MTL_STORE_RESOLVE;
    pass.depth.resolve = &f.buffer;
    pass.depth.store = DOE_MTL_RESOLVE;
    pass.depth.resolve_filter = 2;
    pass.stencil.resolve = &f.buffer;
    pass.stencil.store = DOE_MTL_STORE_RESOLVE;
    pass.stencil.resolve_filter = 1;
    pass.visibility_buffer = &f.index_buffer;
    for (i = 0; i < 4; ++i) {
        draws[i].pipeline = &f.pipeline;
        draws[i].vertex_sizes_slot = UINT32_MAX;
        draws[i].fragment_sizes_slot = UINT32_MAX;
        draws[i].vertex.textures[127] = &f.texture;
        draws[i].fragment.samplers[15] = &f.sampler;
        draws[i].vertex.texture_count = 128; draws[i].fragment.sampler_count = 16;
        draws[i].values.count = 3;
        draws[i].values.instance_count = 1;
    }
    draws[0].values.visibility_mode = 2;
    draws[0].values.visibility_offset = 24;
    draws[1].values = draws[0].values;
    draws[3].values.visibility_mode = 1;
    draws[3].values.visibility_offset = 48;
    assert(fixture_render(&f.runtime, &f.command, &pass, draws, 4, NULL) == DOE_MTL_OK);
    assert(f.resolve_texture[0] == &f.buffer && f.resolve_texture[8] == &f.buffer && f.resolve_texture[9] == &f.buffer);
    assert(f.resolve_level[8] == 0 && f.resolve_slice[9] == 0 && f.resolve_filter[8] == 2 && f.resolve_filter[9] == 1);
    assert(f.store[0] == 3 && f.store[8] == 2 && f.store[9] == 3);
    assert(f.visibility_buffer == &f.index_buffer && f.visibility_sets == 3);
    assert(f.visibility_modes[0] == 2 && f.visibility_offsets[0] == 24);
    assert(f.visibility_modes[1] == 0 && f.visibility_offsets[1] == 0);
    assert(f.visibility_modes[2] == 1 && f.visibility_offsets[2] == 48);
    assert(f.draws == 4 && f.ends == 1 && f.pools == f.drains);
}
static void empty_raster_state(void) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        fixture f;
        fixture_draw draw = {0};
        initialize(&f);
        doe_mtl_native_render_pass pass = pass_new(&f);
        draw.pipeline = &f.pipeline;
        draw.vertex_sizes_slot = draw.fragment_sizes_slot = UINT32_MAX;
        draw.vertex.textures[127] = &f.texture;
        draw.fragment.samplers[15] = &f.sampler;
        draw.vertex.texture_count = 128; draw.fragment.sampler_count = 16;
        draw.values = (doe_mtl_render_draw){.count = 3, .instance_count = 1,
            .viewport_enabled = mode != 2, .viewport = {0, 0, mode == 0 ? 0 : 8, 0, 0, 1},
            .scissor_enabled = true, .scissor = {3, 4, mode == 2 ? 0u : 8u, 9}};
        assert(fixture_render(&f.runtime, &f.command, &pass, &draw, 1, NULL) == DOE_MTL_OK);
        assert(f.draws == 1 && f.scissor.width == 0 && f.scissor.height == 0);
        assert(f.viewport.width > 0 && f.viewport.height > 0);
        assert(f.ends == 1 && f.pools == f.drains);
    }
}
static void changed_state(void) {
    fixture f;
    initialize(&f);
    doe_mtl_native_render_pass pass = pass_new(&f);
    fixture_draw draws[8] = {0};
    draws[0].pipeline = &f.pipeline;
    draws[0].vertex_sizes_slot = draws[0].fragment_sizes_slot = UINT32_MAX;
    draws[0].vertex.textures[127] = &f.texture;
    draws[0].fragment.samplers[15] = &f.sampler;
    draws[0].values = (doe_mtl_render_draw){.count=3,.instance_count=1,.viewport_enabled=true,
        .viewport={0,0,64,32,0,1},.scissor_enabled=true,.scissor={0,0,64,32}};
    for (size_t i = 1; i < 8; ++i) draws[i] = draws[0];
    draws[2].values.viewport_enabled = draws[2].values.scissor_enabled = false; /* Same effective state. */
    draws[3].values.count = 0; /* Cannot replace last emitted encoder state. */
    draws[3].values.cull = 2;
    draws[5].pipeline = &f.index_buffer;
    draws[5].depth_stencil = &f.depth_state;
    draws[5].values.viewport = (doe_mtl_viewport){1,2,8,9,0.1,0.9};
    draws[5].values.scissor = (doe_mtl_scissor){1,2,3,4};
    draws[5].values.winding = 1; draws[5].values.cull = 1;
    draws[5].values.unclipped_depth = true;
    draws[5].values.blend_constant[0] = 0.5f;
    draws[5].values.depth_bias = 1; draws[5].values.stencil_reference = 9;
    draws[6] = draws[5];
    assert(fixture_render(&f.runtime, &f.command, &pass, draws, 8, NULL) == DOE_MTL_OK);
    assert(f.draws == 7 && f.pipeline_sets == 3 && f.depth_sets == 3);
    assert(f.viewport_sets == 3 && f.scissor_sets == 3 && f.winding_sets == 3 && f.cull_sets == 3);
    assert(f.depth_clip_count == 3 && f.blend_sets == 3 && f.bias_sets == 3 && f.stencil_sets == 3);
    f.releases = f.drains = 0;
    assert(fixture_render(&f.runtime, &f.command, &pass, draws, 1, NULL) == DOE_MTL_OK);
    assert(f.pipeline_sets == 4 && f.depth_sets == 4 && f.viewport_sets == 4 && f.scissor_sets == 4);
}

static void changed_bindings(void) {
    fixture f;
    initialize(&f);
    doe_mtl_native_render_pass pass = pass_new(&f);
    fixture_draw draws[10] = {0};
    draws[0].pipeline = &f.pipeline;
    draws[0].vertex_sizes_slot = 30;
    draws[0].fragment_sizes_slot = UINT32_MAX;
    draws[0].vertex.buffers[4] = (doe_mtl_native_binding){&f.buffer, 12, 20};
    draws[0].vertex.textures[127] = &f.texture;
    draws[0].vertex.texture_count = 128;
    draws[0].fragment.samplers[15] = &f.sampler;
    draws[0].fragment.sampler_count = 16;
    draws[0].values.count = draws[0].values.instance_count = 1;
    draws[1] = draws[0];
    assert(fixture_render(&f.runtime, &f.command, &pass, draws, 2, NULL) == DOE_MTL_OK);
    assert(f.buffer_sets[0] == 1 && f.bytes_sets[0] == 1);
    assert(f.texture_sets[0] == 1 && f.sampler_sets[1] == 1 && f.draws == 2);
    /* A new native encoder starts unbound, even with the same draw snapshots. */
    f.releases = f.drains = 0;
    assert(fixture_render(&f.runtime, &f.command, &pass, draws, 2, NULL) == DOE_MTL_OK);
    assert(f.buffer_sets[0] == 2 && f.bytes_sets[0] == 2);
    assert(f.texture_sets[0] == 2 && f.sampler_sets[1] == 2);

    draws[2] = draws[0];
    draws[2].vertex.buffers[4].offset = 16;
    draws[2].vertex.buffers[4].size = 24;
    /* An empty direct draw must not become the cached native state. */
    draws[3].vertex_sizes_slot = draws[3].fragment_sizes_slot = UINT32_MAX;
    draws[4] = draws[2];
    draws[5] = draws[2];
    draws[5].vertex_sizes_slot = UINT32_MAX;
    draws[5].vertex.buffers[30] = (doe_mtl_native_binding){&f.index_buffer, 8, 12};
    draws[5].vertex.immediate_size = 4;
    draws[5].vertex.immediate_slot = 6;
    draws[5].vertex.immediate_words[0] = 7;
    draws[6] = draws[5];
    draws[6].vertex.immediate_words[0] = 8;
    draws[7] = draws[6];
    draws[7].vertex.immediate_size = 0;
    draws[7].vertex.buffers[6] = (doe_mtl_native_binding){&f.buffer, 0, 4};
    draws[8] = draws[7];
    draws[8].vertex_sizes_slot = 6;
    draws[8].vertex.buffers[6] = (doe_mtl_native_binding){0};
    draws[9] = draws[7];
    initialize(&f); pass = pass_new(&f);
    assert(fixture_render(&f.runtime, &f.command, &pass, draws, 10, NULL) == DOE_MTL_OK);
    assert(f.draws == 9 && f.buffer_sets[0] == 5 && f.bytes_sets[0] == 5);
    assert(f.texture_sets[0] == 1 && f.sampler_sets[1] == 1);
    assert(f.buffers[0][4].buffer == &f.buffer && f.buffers[0][4].offset == 16);
    assert(f.buffers[0][6].buffer == &f.buffer && !f.bytes_bound[0][6]);
    assert(f.buffers[0][30].buffer == &f.index_buffer && !f.bytes_bound[0][30]);
}
static void timestamp_attachments(void) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        fixture f;
        initialize(&f);
        doe_mtl_native_render_pass pass = pass_new(&f);
        pass.timestamp_buffer = &f.buffer;
        pass.timestamp_begin = mode == 2 ? UINT32_MAX : 1;
        pass.timestamp_end = mode == 1 ? UINT32_MAX : 3;
        f.timestamp_expected = true;
        f.no_timestamp_attachment = mode == 3;
        f.no_timestamp_selector = mode == 4;
        doe_mtl_status status = fixture_render(&f.runtime,&f.command,&pass,NULL,0,NULL);
        assert(status == (mode >= 3 ? DOE_MTL_NATIVE_FAILURE : DOE_MTL_OK));
        assert(f.begins == (mode >= 3 ? 0u : 1u));
        if (mode < 3) {
            assert(f.timestamp_begin == (mode == 2 ? SIZE_MAX : 1));
            assert(f.timestamp_end == (mode == 1 ? SIZE_MAX : 3));
            assert(f.ends == 1);
        }
        assert(f.releases == 1 && f.pools == f.drains);
    }
}
int main(void) {
    timestamp_attachments();
    changed_state();
    changed_bindings();
    empty_raster_state();
    resolves_and_visibility();
    encoding(); clear_and_failure();
    fixture f;
    initialize(&f);
    doe_mtl_native_render_pass pass=pass_new(&f);
    fixture_draw packets[2]={0};
    for (unsigned i=0;i<2;++i) {
        packets[i].pipeline=&f.pipeline;
        packets[i].vertex_sizes_slot=packets[i].fragment_sizes_slot=UINT32_MAX;
        packets[i].indirect_arguments=&f.buffer;
        packets[i].indirect_offset=(size_t)(i+1)*40;
        packets[i].values.topology=i ? 4 : 3;
    }
    packets[1].index_buffer=&f.index_buffer; packets[1].index_offset=2;
    /* Counts intentionally remain zero on the CPU: only GPU scratch owns the
     * actual counts, and both real indirect selectors must still be called. */
    assert(fixture_render(&f.runtime,&f.command,&pass,packets,2,NULL)==DOE_MTL_OK);
    assert(f.indirect_draws==1 && f.indexed_indirect_draws==1 && f.draws==0 && f.indexed_draws==0);
    puts("Metal typed render encoder controls: attachments, full per-stage state, indexed/instanced args and failure cleanup passed; no SDK/GPU");
    return 0;
}
