/* SPDX-License-Identifier: MIT */
#include "d3d12_draw_guard.h"
#include "d3d12_texture_desc.h"
#include "shared_memory_internal.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
static bool same_targets(const doe_resource_render_targets *a,const doe_resource_render_targets *b)
{
    if (a->color_count>DOE_DX_MAX_COLOR_TARGETS || a->color_count!=b->color_count ||
        a->depth_stencil!=b->depth_stencil || a->occlusion_queries!=b->occlusion_queries) return false;
    for (uint32_t i=0;i<a->color_count;++i)
        if (a->colors[i].view!=b->colors[i].view || a->colors[i].resolve_target!=b->colors[i].resolve_target ||
            a->colors[i].has_depth_slice!=b->colors[i].has_depth_slice ||
            (a->colors[i].has_depth_slice && a->colors[i].depth_slice!=b->colors[i].depth_slice)) return false;
    return true;
}
static native_texture *attachment_texture(doe_resource_view *view,doe_dx_texture_view_desc *out)
{
    if (!view) return NULL;
    const doe_resource_view_desc *span=doe_resource_view_description(view);
    doe_resource_texture *texture=doe_resource_view_texture(view);
    const doe_resource_texture_desc *desc=doe_resource_texture_description(texture);
    native_texture *native=doe_resource_texture_native(texture);
    if (!native_shared_texture_accessible(texture) ||
        !doe_resource_view_native(view) || !span || !desc || !native || !native->d3d12 ||
        (span->dimension!=WGPUTextureViewDimension_2D && span->dimension!=WGPUTextureViewDimension_2DArray &&
         span->dimension!=WGPUTextureViewDimension_3D) ||
        span->mip_count!=1 || span->layer_count!=1 || !native_d3d12_view_descriptor(span,desc,out)) return NULL;
    return native;
}
static native_failure color_attachment(const doe_resource_color_attachment *source,doe_dx_render_color *out)
{
    if (!source->view) return native_failure_status(source->resolve_target ? NATIVE_INVALID_ARGUMENT : NATIVE_OK);
    native_texture *texture=attachment_texture(source->view,&out->view);
    if (!texture) return native_failure_status(NATIVE_UNSUPPORTED);
    out->texture=texture->d3d12;
    out->depth_slice=source->has_depth_slice ? source->depth_slice : 0;
    if (source->resolve_target) {
        texture=attachment_texture(source->resolve_target,&out->resolve_view);
        if (!texture) return native_failure_status(NATIVE_UNSUPPORTED);
        out->resolve=texture->d3d12;
    }
    if (source->load==WGPULoadOp_Clear) {
        doe_vk_clear_color value;
        if (!native_render_clear(doe_resource_view_description(source->view)->format,source->clear,&value))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        out->clear=true; memcpy(out->value,value.float32,sizeof(out->value));
    } else if (source->load!=WGPULoadOp_Load) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    out->discard=source->store==WGPUStoreOp_Discard;
    if (!out->discard && source->store!=WGPUStoreOp_Store) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    return native_failure_status(NATIVE_OK);
}
/* Pass-local binding snapshots, one per distinct pipeline, bind groups and
 * dynamic offsets. Bind groups are immutable and the command record holds the
 * pipeline and groups through recording, so equal identities resolve to equal
 * native values: draws share one snapshot instead of resolving and copying
 * their bindings per draw, and the backend then prepares and describes the
 * shared arrays once. */
typedef struct pass_binding {
    doe_resource_render_pipeline *pipeline;
    doe_resource_bind_group *groups[DOE_RESOURCE_MAX_GROUPS];
    doe_resource_dispatch_offsets offsets; /* Rows beyond each count are unused. */
    native_d3d12_snapshot stages[DOE_DX_RENDER_STAGES];
} pass_binding;
typedef struct pass_bindings {
    pass_binding **slots; /* Open addressing; capacity is a power of two. */
    size_t capacity;
    pass_binding **ordered;
    size_t count, ordered_capacity;
    const pass_binding *last;
} pass_bindings;
static bool same_binding(const pass_binding *entry,const doe_resource_render_packet *packet)
{
    if (entry->pipeline!=packet->pipeline) return false;
    for (size_t i=0;i<DOE_RESOURCE_MAX_GROUPS;++i) {
        size_t count=packet->offsets.counts[i];
        if (entry->groups[i]!=packet->groups[i] || entry->offsets.counts[i]!=count ||
            count>DOE_RESOURCE_MAX_DYNAMIC_OFFSETS ||
            (count && memcmp(entry->offsets.values[i],packet->offsets.values[i],count*sizeof(uint32_t)))) return false;
    }
    return true;
}
static size_t binding_hash(const doe_resource_render_pipeline *pipeline,doe_resource_bind_group *const *groups,
    const doe_resource_dispatch_offsets *offsets)
{
    uint64_t hash=(uint64_t)(uintptr_t)pipeline;
    for (size_t i=0;i<DOE_RESOURCE_MAX_GROUPS;++i) {
        hash=(hash^(uint64_t)(uintptr_t)groups[i])*UINT64_C(0x9e3779b97f4a7c15);
        size_t count=offsets->counts[i]<=DOE_RESOURCE_MAX_DYNAMIC_OFFSETS ? offsets->counts[i] : 0;
        for (size_t j=0;j<count;++j) hash=(hash^offsets->values[i][j])*UINT64_C(0x9e3779b97f4a7c15);
    }
    return (size_t)(hash^(hash>>29));
}
static bool insert_binding(pass_bindings *table,pass_binding *entry)
{
    if (table->count==table->ordered_capacity) {
        size_t capacity=table->ordered_capacity ? table->ordered_capacity*2 : 16u;
        if (capacity>SIZE_MAX/sizeof(*table->ordered)) return false;
        pass_binding **ordered=realloc(table->ordered,capacity*sizeof(*ordered));
        if (!ordered) return false;
        table->ordered=ordered;
        table->ordered_capacity=capacity;
    }
    if ((table->count+1)*2>table->capacity) {
        size_t capacity=table->capacity ? table->capacity*2 : 32u;
        if (capacity>SIZE_MAX/sizeof(*table->slots)) return false;
        pass_binding **slots=calloc(capacity,sizeof(*slots));
        if (!slots) return false;
        for (size_t i=0;i<table->count;++i) {
            const pass_binding *moved=table->ordered[i];
            size_t at=binding_hash(moved->pipeline,moved->groups,&moved->offsets)&(capacity-1);
            while (slots[at]) at=(at+1)&(capacity-1);
            slots[at]=table->ordered[i];
        }
        free(table->slots);
        table->slots=slots;
        table->capacity=capacity;
    }
    size_t at=binding_hash(entry->pipeline,entry->groups,&entry->offsets)&(table->capacity-1);
    while (table->slots[at]) at=(at+1)&(table->capacity-1);
    table->slots[at]=entry;
    table->ordered[table->count++]=entry;
    return true;
}
static native_failure pass_binding_for(pass_bindings *table,const doe_resource_render_packet *packet,
    native_pipeline *pipeline,const pass_binding **out)
{
    if (table->last && same_binding(table->last,packet)) { *out=table->last; return native_failure_status(NATIVE_OK); }
    if (table->capacity)
        for (size_t at=binding_hash(packet->pipeline,packet->groups,&packet->offsets)&(table->capacity-1);
            table->slots[at];at=(at+1)&(table->capacity-1))
            if (same_binding(table->slots[at],packet)) {
                *out=table->last=table->slots[at];
                return native_failure_status(NATIVE_OK);
            }
    doe_resource_binding resolved[NATIVE_MAX_PIPELINE_BINDINGS];
    if (native_resolve_resources(pipeline,doe_resource_render_pipeline_get_layout(packet->pipeline),
        packet->groups,&packet->offsets,resolved)!=DOE_RESOURCE_OK) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    /* Binding values write every row below each count and the record header;
     * nothing reads the fixed-capacity rows beyond them. */
    pass_binding *entry=malloc(sizeof(*entry));
    if (!entry) return native_failure_status(NATIVE_OUT_OF_MEMORY);
    entry->pipeline=packet->pipeline;
    memcpy(entry->groups,packet->groups,sizeof(entry->groups));
    memcpy(entry->offsets.counts,packet->offsets.counts,sizeof(entry->offsets.counts));
    for (size_t i=0;i<DOE_RESOURCE_MAX_GROUPS;++i) {
        size_t count=packet->offsets.counts[i]<=DOE_RESOURCE_MAX_DYNAMIC_OFFSETS ? packet->offsets.counts[i] : 0;
        memcpy(entry->offsets.values[i],packet->offsets.values[i],count*sizeof(uint32_t));
    }
    for (size_t stage=0;stage<pipeline->d3d12_graphics->stage_count;++stage) {
        native_failure failure=native_d3d12_binding_values(pipeline,&pipeline->d3d12_graphics->stages[stage],
            resolved,&entry->stages[stage]);
        if (failure.status!=NATIVE_OK) { free(entry); return failure; }
    }
    if (!insert_binding(table,entry)) { free(entry); return native_failure_status(NATIVE_OUT_OF_MEMORY); }
    *out=table->last=entry;
    return native_failure_status(NATIVE_OK);
}
static void pass_bindings_free(pass_bindings *table)
{
    for (size_t i=0;i<table->count;++i) free(table->ordered[i]);
    free(table->ordered);
    free(table->slots);
}
/* Pass translation rows, kept by the device between submissions so a large
 * pass reuses committed memory instead of faulting in fresh zero pages each
 * frame. Rows are rewritten before use; at most RETAINED_ROWS stay allocated,
 * a larger pass takes temporary rows. */
typedef struct native_d3d12_render_rows native_d3d12_render_rows;
struct native_d3d12_render_rows {
    doe_dx_render_draw *draws;
    native_d3d12_graphics_snapshot *storage;
    size_t capacity;
};
enum { RETAINED_ROWS = 16384 };
static bool take_rows(native_backend *backend,size_t count,native_d3d12_render_rows *out)
{
    native_d3d12_render_rows *kept=backend->value.d3d12.render_rows;
    if (kept && count<=kept->capacity) { *out=*kept; return true; }
    /* Grow geometrically up to the retained bound; a larger pass is temporary. */
    size_t capacity=count;
    if (count<=RETAINED_ROWS && kept && kept->capacity>count/2)
        capacity=kept->capacity*2<RETAINED_ROWS ? kept->capacity*2 : RETAINED_ROWS;
    native_d3d12_render_rows rows={calloc(capacity,sizeof(*rows.draws)),calloc(capacity,sizeof(*rows.storage)),capacity};
    if (!rows.draws || !rows.storage) { free(rows.draws); free(rows.storage); return false; }
    if (capacity<=RETAINED_ROWS) {
        if (!kept) kept=calloc(1,sizeof(*kept));
        if (kept) {
            free(kept->draws);
            free(kept->storage);
            *kept=rows;
            backend->value.d3d12.render_rows=kept;
        }
    }
    *out=rows;
    return true;
}
static void return_rows(native_backend *backend,const native_d3d12_render_rows *rows)
{
    const native_d3d12_render_rows *kept=backend->value.d3d12.render_rows;
    if (kept && kept->draws==rows->draws) return;
    free(rows->draws);
    free(rows->storage);
}
static native_failure prepare_draw(pass_bindings *bindings,const doe_resource_render_packet *packet,
    doe_dx_render_draw *out,native_d3d12_graphics_snapshot *storage)
{
    native_pipeline *pipeline=doe_resource_render_pipeline_native(packet->pipeline);
    if (!pipeline || !pipeline->d3d12_graphics )
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const pass_binding *binding=NULL;
    native_failure resolved=pass_binding_for(bindings,packet,pipeline,&binding);
    if (resolved.status!=NATIVE_OK) return resolved;
    *out=(doe_dx_render_draw){.pipeline=pipeline->handle.d3d12,.vertex_count=packet->count,
        .instance_count=packet->instances,.first_vertex=packet->first,.first_instance=packet->first_instance};
    for (size_t stage=0;stage<pipeline->d3d12_graphics->stage_count;++stage) {
        out->stages[stage]=binding->stages[stage].values;
        memcpy(out->stages[stage].immediate_words,packet->immediates.words,sizeof(out->stages[stage].immediate_words));
        out->stages[stage].immediate_initialized_bytes=packet->immediates.initialized_bytes;
    }
    out->vertices=storage->vertices;
    out->vertex_buffer_count=pipeline->vertex_count;
    for (size_t i=0;i<pipeline->vertex_count;++i) {
        native_buffer *buffer=doe_resource_buffer_native(packet->vertices[i].buffer);
        if (!buffer) {
            if (doe_resource_render_pipeline_vertex_required(packet->pipeline,i))
                return native_failure_status(NATIVE_INVALID_STATE);
            continue;
        }
        native_d3d12_buffer *native=buffer->handle.d3d12;
        uint64_t offset=packet->vertices[i].offset,size=packet->vertices[i].size;
        if (!native || offset>native->allocation_size || size>native->allocation_size-offset)
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        storage->vertices[i]=(doe_dx_buffer_binding){native->allocation,offset,size};
    }
    doe_vk_clear_color blend;
    if (!native_render_clear(WGPUTextureFormat_RGBA32Float,packet->state.blend,&blend))
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    out->stencil_reference=packet->state.stencil_reference;
    memcpy(out->blend_constant,blend.float32,sizeof(out->blend_constant));
    memcpy(out->viewport,packet->state.viewport,sizeof(out->viewport));
    memcpy(out->scissor,packet->state.scissor,sizeof(out->scissor));
    return native_failure_status(NATIVE_OK);
}
#endif

void native_d3d12_render_rows_close(native_backend *backend)
{
#if defined(_WIN32)
    native_d3d12_render_rows *kept=backend->value.d3d12.render_rows;
    if (!kept) return;
    free(kept->draws);
    free(kept->storage);
    free(kept);
    backend->value.d3d12.render_rows=NULL;
#else
    (void)backend;
#endif
}

native_failure native_d3d12_render(native_backend *backend,native_command_handle *commands,
    const doe_resource_command *records,size_t count,size_t *consumed)
{
#if defined(_WIN32)
    if (!backend || !commands || !records || !count || !consumed || records[0].kind!=DOE_COMMAND_RENDER)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    const doe_resource_render_packet *begin=doe_resource_render_record_borrow(records[0].value.render);
    if (!begin || !begin->boundary_only || !begin->first_in_pass || begin->last_in_pass ||
        !same_targets(&begin->targets,&begin->targets)) return native_failure_status(NATIVE_UNSUPPORTED);
    size_t end=0,draws_count=0;
    for (size_t i=1;i<count;++i) {
        if (records[i].kind==DOE_COMMAND_BEGIN_OCCLUSION || records[i].kind==DOE_COMMAND_END_OCCLUSION) {
            if (records[i].value.timestamp.set!=begin->targets.occlusion_queries) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            if (draws_count==SIZE_MAX) return native_failure_status(NATIVE_OUT_OF_MEMORY);
            ++draws_count;
            continue;
        }
        if (records[i].kind!=DOE_COMMAND_RENDER) return native_failure_status(NATIVE_UNSUPPORTED);
        const doe_resource_render_packet *packet=doe_resource_render_record_borrow(records[i].value.render);
        if (!packet || packet->first_in_pass || !same_targets(&begin->targets,&packet->targets))
            return native_failure_status(NATIVE_INVALID_ARGUMENT);
        if (packet->boundary_only) {
            if (!packet->last_in_pass) return native_failure_status(NATIVE_INVALID_ARGUMENT);
            end=i;
            break;
        }
        if (packet->last_in_pass) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        size_t draws=doe_resource_render_record_draw_count(records[i].value.render);
        if (draws>SIZE_MAX-draws_count) return native_failure_status(NATIVE_OUT_OF_MEMORY);
        draws_count+=draws;
    }
    if (!end) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_dx_render_color colors[DOE_DX_MAX_COLOR_TARGETS]={0};
    for (uint32_t i=0;i<begin->targets.color_count;++i) {
        native_failure result=color_attachment(&begin->targets.colors[i],&colors[i]);
        if (result.status!=NATIVE_OK) return result;
    }
    if (draws_count>SIZE_MAX/sizeof(native_d3d12_graphics_snapshot) || draws_count>SIZE_MAX/sizeof(doe_dx_render_draw))
        return native_failure_status(NATIVE_OUT_OF_MEMORY);
    pass_bindings bindings={0};
    native_d3d12_render_rows rows={0};
    native_failure failure=native_failure_status(draws_count && !take_rows(backend,draws_count,&rows) ?
        NATIVE_OUT_OF_MEMORY : NATIVE_OK);
    doe_dx_render_draw *draws=rows.draws;
    native_d3d12_graphics_snapshot *storage=rows.storage;
    size_t draw_at=0;
    for (size_t i=1;failure.status==NATIVE_OK && i<end;++i) {
        const doe_resource_command *record=&records[i];
        if (record->kind==DOE_COMMAND_BEGIN_OCCLUSION || record->kind==DOE_COMMAND_END_OCCLUSION) {
            native_queries *queries=doe_resource_query_set_native(record->value.timestamp.set);
            if (!queries || !queries->d3d12_queries) { failure=native_failure_status(NATIVE_INVALID_ARGUMENT); break; }
            storage[draw_at]=(native_d3d12_graphics_snapshot){0};
            draws[draw_at]=(doe_dx_render_draw){.query=queries->d3d12_queries,.query_index=record->value.timestamp.index,
                .query_begin=record->kind==DOE_COMMAND_BEGIN_OCCLUSION};
            ++draw_at;
            continue;
        }
        for (size_t j=0;failure.status==NATIVE_OK && j<doe_resource_render_record_draw_count(record->value.render);++j) {
            doe_resource_render_packet packet;
            if (!doe_resource_render_record_draw(record->value.render,j,&packet)) {
                failure=native_failure_status(NATIVE_INVALID_ARGUMENT); break;
            }
            storage[draw_at]=(native_d3d12_graphics_snapshot){0};
            failure=prepare_draw(&bindings,&packet,&draws[draw_at],&storage[draw_at]);
            if (failure.status==NATIVE_OK)
                failure=native_d3d12_draw_prepare(commands,&packet,&draws[draw_at],&storage[draw_at]);
            ++draw_at;
        }
    }
    doe_dx_render_pass pass={.colors_explicit=true,.colors=colors,.color_count=begin->targets.color_count,
        .draws=draws,.draw_count=draws_count};
    if (failure.status==NATIVE_OK && begin->targets.depth_stencil) {
        doe_resource_view *depth_view=begin->targets.depth_stencil;
        const doe_resource_view_desc *depth_span=doe_resource_view_description(depth_view);
        native_texture *depth=doe_resource_texture_native(doe_resource_view_texture(depth_view));
        if (!native_shared_texture_accessible(doe_resource_view_texture(depth_view)) ||
            !depth_span || !depth || !depth->d3d12 ||
            (depth_span->dimension!=WGPUTextureViewDimension_2D && depth_span->dimension!=WGPUTextureViewDimension_2DArray) ||
            depth_span->mip_count!=1 || depth_span->layer_count!=1 ||
            !native_d3d12_view_descriptor(depth_span,doe_resource_texture_description(doe_resource_view_texture(depth_view)),&pass.depth_view))
            failure=native_failure_status(NATIVE_UNSUPPORTED);
        else {
            pass.depth=depth->d3d12;
            pass.clear_depth=begin->targets.depth_load==WGPULoadOp_Clear;
            pass.depth_clear=begin->targets.clear_depth;
            pass.discard_depth=begin->targets.depth_store==WGPUStoreOp_Discard;
            pass.depth_read_only=begin->targets.depth_read_only;
            pass.stencil_read_only=begin->targets.stencil_read_only;
            pass.clear_stencil=begin->targets.stencil_load==WGPULoadOp_Clear;
            pass.discard_stencil=begin->targets.stencil_store==WGPUStoreOp_Discard;
            pass.stencil_clear=begin->targets.clear_stencil;
            /* Canonical pass admission already resolves absent-aspect load/store
             * and read-only contracts; backend independently rejects writes. */
        }
    }
    if (failure.status==NATIVE_OK) {
        doe_dx_error detail={0};
        doe_dx_status status=doe_dx_commands_render(commands->value.d3d12.commands,&pass,&detail);
        failure=native_failure_d3d12(status,&detail);
    }
    for (size_t i=0;i<draw_at;++i) {
        doe_dx_buffer_release(storage[i].arguments.buffer);
        storage[i].arguments.buffer=NULL;
    }
    if (draws_count) return_rows(backend,&rows);
    pass_bindings_free(&bindings);
    if (failure.status==NATIVE_OK) *consumed=end+1;
    return failure;
#else
    (void)backend; (void)commands; (void)records; (void)count; (void)consumed;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
