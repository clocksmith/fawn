/* SPDX-License-Identifier: MIT */
#include "render_internal.h"
#include "../../../common/include/doe_draw_guard.h"

struct doe_mtl_draw_guard {
    doe_mtl_pipeline *normalize, *scan;
    uint32_t record_slot, scan_slots[3], max_groups_x;
};

static bool slots(const doe_mtl_pipeline_desc *pipeline,const uint32_t *values,size_t count)
{
    for (size_t i=0;i<count;++i) {
        if (values[i]>=DOE_MTL_BUFFER_SLOTS || values[i]==pipeline->sizes_slot) return false;
        for (size_t j=0;j<i;++j) if (values[i]==values[j]) return false;
    }
    return true;
}
doe_mtl_status doe_mtl_draw_guard_create(doe_mtl_context *context,const doe_mtl_draw_guard_desc *desc,
    doe_mtl_draw_guard **out,doe_mtl_error *error)
{
    if (!context || !desc || !out || desc->source_version!=DOE_MTL_DRAW_GUARD_SOURCE_VERSION || !desc->max_groups_x ||
        desc->normalize.workgroup.x!=1 || desc->normalize.workgroup.y!=1 || desc->normalize.workgroup.z!=1 ||
        desc->scan.workgroup.x!=64 || desc->scan.workgroup.y!=1 || desc->scan.workgroup.z!=1 ||
        !slots(&desc->normalize,&desc->record_slot,1) || !slots(&desc->scan,desc->scan_slots,3))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT,"draw guard descriptor",error);
    doe_mtl_draw_guard *guard=calloc(1,sizeof(*guard));
    if (!guard) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY,"draw guard allocation",error);
    doe_mtl_status status=doe_mtl_pipeline_create(context,&desc->normalize,&guard->normalize,error);
    if (status==DOE_MTL_OK) status=doe_mtl_pipeline_create(context,&desc->scan,&guard->scan,error);
    if (status!=DOE_MTL_OK) { doe_mtl_draw_guard_release(guard); return status; }
    guard->record_slot=desc->record_slot;
    memcpy(guard->scan_slots,desc->scan_slots,sizeof(guard->scan_slots));
    guard->max_groups_x=desc->max_groups_x>UINT32_MAX/64 ? UINT32_MAX/64 : desc->max_groups_x;
    *out=guard;
    return doe_mtl_fail(DOE_MTL_OK,"draw guard create",error);
}
void doe_mtl_draw_guard_release(doe_mtl_draw_guard *guard)
{
    if (!guard) return;
    doe_mtl_pipeline_release(guard->normalize);
    doe_mtl_pipeline_release(guard->scan);
    free(guard);
}

/* Native vertex formats/layouts are already admitted. Derive exact fetch
 * capacities from every actual attribute, never GPU index guesses or u32 caps. */
static doe_mtl_status capacities(const doe_mtl_render_draw *draw,uint64_t result[2])
{
    result[0]=result[1]=UINT64_MAX;
    const doe_mtl_render_pipeline_desc *pipeline=&draw->pipeline->desc;
    for (size_t i=0;i<pipeline->attribute_count;++i) {
        const doe_mtl_vertex_attribute *attribute=&pipeline->attributes[i];
        const doe_mtl_vertex_layout *layout=NULL;
        const doe_mtl_buffer_binding *buffer=NULL;
        for (size_t j=0;j<pipeline->layout_count;++j)
            if (pipeline->layouts[j].slot==attribute->slot) layout=&pipeline->layouts[j];
        for (size_t j=0;j<draw->vertex.buffer_count;++j)
            if (draw->vertex.buffers[j].slot==attribute->slot) buffer=&draw->vertex.buffers[j];
        if (!layout || !buffer) return DOE_MTL_INVALID_ARGUMENT;
        uint64_t end=(uint64_t)attribute->offset+doe_mtl_vertex_format_size(attribute->format);
        uint64_t count=end>buffer->size ? 0 : layout->stride ? (buffer->size-end)/layout->stride+1 : UINT64_MAX;
        if (layout->step==0 && count) continue;
        size_t axis=layout->step==2 ? 1u : 0u;
        if (count<result[axis]) result[axis]=count;
    }
    return DOE_MTL_OK;
}
doe_mtl_status doe_mtl_draw_guard_preflight(doe_mtl_commands *commands,const doe_mtl_render_draw *draw)
{
    if (!draw->guarded) return DOE_MTL_OK;
    const doe_mtl_guarded_draw *input=draw->guarded;
    if (!input->guard || input->guard->normalize->context!=commands->context ||
        input->guard->scan->context!=commands->context) return DOE_MTL_DEVICE_MISMATCH;
    if (input->arguments) {
        doe_mtl_status status=doe_mtl_buffer_preflight(commands,input->arguments,true);
        if (status!=DOE_MTL_OK) return status;
        if (input->argument_offset%4 || !doe_mtl_range(input->arguments->size,input->argument_offset,
            draw->index_buffer ? 20 : 16)) return DOE_MTL_RANGE;
    } else if (!draw->index_buffer || input->argument_offset) return DOE_MTL_INVALID_ARGUMENT;
    if (draw->index_buffer) {
        uint64_t element=draw->index_type==0 ? 2 : 4;
        if (draw->index_type>1 || draw->index_offset%element ||
            !doe_mtl_range(draw->index_buffer->size,draw->index_offset,input->index_size)) return DOE_MTL_RANGE;
        uint64_t end=draw->index_offset+input->index_size;
        if (end>UINT64_MAX-3 || ((end+3)&~UINT64_C(3))>draw->index_buffer->size) return DOE_MTL_RANGE;
        doe_mtl_status status=doe_mtl_buffer_preflight(commands,draw->index_buffer,true);
        if (status!=DOE_MTL_OK) return status;
    }
    uint64_t limits[2];
    return capacities(draw,limits);
}

static doe_mtl_status scan_window(doe_mtl_commands *commands,const doe_mtl_render_draw *draw,
    doe_mtl_buffer *scratch,uint64_t first,uint32_t count,uint64_t vertex_limit,doe_mtl_error *error)
{
    const doe_mtl_draw_guard *guard=draw->guarded->guard;
    uint64_t element=draw->index_type==0 ? 2 : 4;
    uint64_t offset=draw->index_offset+first*element;
    uint64_t aligned=offset&~UINT64_C(3);
    uint64_t bytes=((offset-aligned)+(uint64_t)count*element+3)&~UINT64_C(3);
    uint32_t groups=count/64+(count%64!=0);
    if (groups>guard->max_groups_x) groups=guard->max_groups_x;
    uint32_t params[9]={(uint32_t)first,(uint32_t)(first>>32),count,(uint32_t)((offset-aligned)/2),
        draw->index_type==0,(uint32_t)vertex_limit,(uint32_t)(vertex_limit>>32),
        draw->topology==2 || draw->topology==4,groups*64};
    doe_mtl_buffer *parameter=NULL;
    doe_mtl_status status=doe_mtl_buffer_create(commands->context,sizeof(params),DOE_MTL_SHARED,&parameter,error);
    if (status==DOE_MTL_OK) {
        status=doe_mtl_buffer_write(parameter,0,params,sizeof(params));
        if (status!=DOE_MTL_OK) (void)doe_mtl_fail(status,"draw scan parameters",error);
    }
    const doe_mtl_buffer_binding bindings[3]={{guard->scan_slots[0],scratch,0,DOE_DRAW_GUARD_RECORD_V2_BYTES},
        {guard->scan_slots[1],draw->index_buffer,aligned,bytes},{guard->scan_slots[2],parameter,0,sizeof(params)}};
    if (status==DOE_MTL_OK) status=doe_mtl_commands_dispatch(commands,guard->scan,bindings,3,
        (doe_mtl_dimensions){groups,1,1},1,error);
    doe_mtl_buffer_release(parameter);
    return status;
}

/* Only within one pass's consecutive preparation phase: no caller buffers are
 * written between these guards, and all guards precede the native render pass.
 * The same immutable pipeline and vertex spans imply the same fetch capacities.
 * Compare fields explicitly; descriptor padding and raster state are irrelevant. */
bool doe_mtl_draw_guard_same_inputs(const doe_mtl_render_draw *left,const doe_mtl_render_draw *right)
{
    const doe_mtl_guarded_draw *a=left->guarded,*b=right->guarded;
    if (!a || !b || a->guard!=b->guard || a->arguments!=b->arguments ||
        a->argument_offset!=b->argument_offset || a->index_size!=b->index_size ||
        a->first_index!=b->first_index || a->first_instance_allowed!=b->first_instance_allowed ||
        left->pipeline!=right->pipeline || left->index_buffer!=right->index_buffer ||
        left->index_offset!=right->index_offset || left->index_type!=right->index_type ||
        left->topology!=right->topology || left->count!=right->count ||
        left->instance_count!=right->instance_count || left->first_vertex!=right->first_vertex ||
        left->base_vertex!=right->base_vertex || left->first_instance!=right->first_instance ||
        left->vertex.buffer_count!=right->vertex.buffer_count) return false;
    for (size_t i=0;i<left->vertex.buffer_count;++i) {
        const doe_mtl_buffer_binding *x=&left->vertex.buffers[i],*y=&right->vertex.buffers[i];
        if (x->slot!=y->slot || x->buffer!=y->buffer || x->offset!=y->offset || x->size!=y->size)
            return false;
    }
    return true;
}

doe_mtl_status doe_mtl_draw_guard_prepare(doe_mtl_commands *commands,const doe_mtl_render_draw *draw,
    doe_mtl_native_render_draw *native,doe_mtl_error *error)
{
    if (!draw->guarded) return DOE_MTL_OK;
    const doe_mtl_guarded_draw *input=draw->guarded;
    const doe_mtl_draw_guard *guard=input->guard;
    uint64_t limits[2];
    doe_mtl_status status=capacities(draw,limits);
    if (status!=DOE_MTL_OK) return doe_mtl_fail(status,"draw guard capacities",error);
    uint64_t count_limit=draw->index_buffer ? input->index_size/(draw->index_type==0 ? 2u : 4u) : limits[0];
    uint32_t words[10]={draw->count,draw->instance_count,draw->index_buffer ? input->first_index : draw->first_vertex,
        draw->index_buffer ? (uint32_t)draw->base_vertex : draw->first_instance,0,0,0,
        (draw->index_buffer ? 1u : 0u)|(input->first_instance_allowed ? 2u : 0u),
        (uint32_t)(count_limit>>32),(uint32_t)(limits[1]>>32)};
    if (draw->index_buffer) { words[4]=draw->first_instance; words[5]=(uint32_t)count_limit; words[6]=(uint32_t)limits[1]; }
    else { words[4]=(uint32_t)count_limit; words[5]=(uint32_t)limits[1]; }
    doe_mtl_buffer *scratch=NULL;
    if (status==DOE_MTL_OK) status=doe_mtl_buffer_create(commands->context,sizeof(words),DOE_MTL_SHARED,&scratch,error);
    if (status==DOE_MTL_OK) {
        status=doe_mtl_buffer_write(scratch,0,words,sizeof(words));
        if (status!=DOE_MTL_OK) (void)doe_mtl_fail(status,"draw guard record",error);
    }
    if (status==DOE_MTL_OK && input->arguments) status=doe_mtl_commands_copy(commands,input->arguments,
        input->argument_offset,scratch,0,draw->index_buffer ? 20 : 16,error);
    const doe_mtl_buffer_binding binding={guard->record_slot,scratch,0,sizeof(words)};
    if (status==DOE_MTL_OK) status=doe_mtl_commands_dispatch(commands,guard->normalize,&binding,1,
        (doe_mtl_dimensions){1,1,1},1,error);
    /* API firstIndex/count are u32, so no invocation can address an ordinal
     * beyond twice UINT32_MAX - 1 even if the allocation itself is larger. */
    uint64_t scan_count=count_limit;
    if (scan_count>UINT64_C(2)*UINT32_MAX) scan_count=UINT64_C(2)*UINT32_MAX;
    uint64_t first=0;
    if (draw->index_buffer && !input->arguments) {
        first=input->first_index;
        uint64_t end=first+draw->count;
        if (end<scan_count) scan_count=end;
    }
    while (status==DOE_MTL_OK && draw->index_buffer && limits[0]!=UINT64_MAX && first<scan_count) {
        uint64_t element=draw->index_type==0 ? 2 : 4;
        uint64_t offset=draw->index_offset+first*element;
        uint64_t maximum=((UINT64_C(0xfffffffc)-(offset%4))/element);
        uint64_t remaining=scan_count-first;
        uint32_t count=(uint32_t)(remaining<maximum ? remaining : maximum);
        status=scan_window(commands,draw,scratch,first,count,limits[0],error);
        first+=count;
    }
    if (status==DOE_MTL_OK) native->indirect_arguments=scratch->native;
    /* Dispatch acquired scratch/index/parameter/PSO leases. Partial failure is
     * discarded by the whole-pass owner; unknown retains the same objects. */
    doe_mtl_buffer_release(scratch);
    return status;
}
