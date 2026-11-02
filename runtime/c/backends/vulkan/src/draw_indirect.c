/* SPDX-License-Identifier: MIT */
#include "draw_indirect_internal.h"
#include "../../../common/include/doe_draw_guard.h"

_Static_assert((size_t)DOE_DRAW_ARGUMENT_SOURCE_CAPACITY == (size_t)DOE_VK_DRAW_INDIRECT_SOURCE_CAPACITY, "draw source capacity");

doe_vk_status doe_vk_draw_validator_source(char *out, size_t capacity, size_t *out_length) {
    switch (doe_draw_argument_source(out, capacity, out_length)) {
    case DOE_DRAW_GUARD_SOURCE_OK: return DOE_VK_OK;
    case DOE_DRAW_GUARD_SOURCE_INVALID_ARGUMENT:
    case DOE_DRAW_GUARD_SOURCE_TOO_SMALL: return DOE_VK_INVALID_ARGUMENT;
    }
    return DOE_VK_INVALID_ARGUMENT;
}

doe_vk_status doe_vk_draw_validator_validate(const doe_vk_caps *caps) {
    const uint32_t one[3] = {1, 1, 1};
    doe_vk_status status = doe_vk_dispatch_validator_validate(caps, one);
    if (status != DOE_VK_OK) return status;
    if (!(caps->queue_flags & VK_QUEUE_GRAPHICS_BIT) || caps->max_storage_buffer_range < 32)
        return DOE_VK_UNSUPPORTED;
    return DOE_VK_OK;
}

static doe_vk_status create_validator(doe_vk_context *context,
    const uint32_t *spirv, size_t word_count, doe_vk_draw_validator **out, doe_vk_error *error, bool batch) {
    if (context == NULL || out == NULL) return doe_vk_fail(DOE_VK_INVALID_ARGUMENT, "draw validator", error);
    if (context->lost) return doe_vk_fail(DOE_VK_DEVICE_LOST, "draw validator context", error);
    doe_vk_status status = doe_vk_draw_validator_validate(&context->caps);
    if (status != DOE_VK_OK) return doe_vk_fail(status, "draw validator limits", error);
    doe_vk_draw_validator *validator = calloc(1, sizeof(*validator));
    if (validator == NULL) return doe_vk_fail(DOE_VK_OUT_OF_MEMORY, "draw validator owner", error);
    validator->references = 1;
    validator->batch = batch;
    const doe_vk_binding_layout layout = {0, 0, DOE_VK_BINDING_STORAGE};
    status = doe_vk_pipeline_create(context, spirv, word_count, "main", &layout, 1, &validator->pipeline, error);
    if (status != DOE_VK_OK) {
        doe_vk_draw_validator_release(validator);
        return status;
    }
    *out = validator;
    return doe_vk_fail(DOE_VK_OK, "draw validator ready", error);
}

doe_vk_status doe_vk_draw_validator_batch_source(char *out, size_t capacity, size_t *out_length) {
    switch (doe_draw_argument_source_batch(out,capacity,out_length)) {
    case DOE_DRAW_GUARD_SOURCE_OK: return DOE_VK_OK;
    case DOE_DRAW_GUARD_SOURCE_INVALID_ARGUMENT:
    case DOE_DRAW_GUARD_SOURCE_TOO_SMALL: return DOE_VK_INVALID_ARGUMENT;
    }
    return DOE_VK_INVALID_ARGUMENT;
}
doe_vk_status doe_vk_draw_validator_create(doe_vk_context *context, const uint32_t *spirv,
    size_t word_count, doe_vk_draw_validator **out, doe_vk_error *error) {
    return create_validator(context,spirv,word_count,out,error,false);
}
doe_vk_status doe_vk_draw_validator_create_batch(doe_vk_context *context, const uint32_t *spirv,
    size_t word_count, doe_vk_draw_validator **out, doe_vk_error *error) {
    return create_validator(context,spirv,word_count,out,error,true);
}

doe_vk_status doe_vk_draw_validator_retain(doe_vk_draw_validator *validator) {
    if (validator == NULL) return DOE_VK_INVALID_ARGUMENT;
    if (validator->references == UINT32_MAX) return DOE_VK_OUT_OF_MEMORY;
    ++validator->references;
    return DOE_VK_OK;
}
void doe_vk_draw_validator_release(doe_vk_draw_validator *validator) {
    if (validator == NULL || --validator->references != 0) return;
    doe_vk_pipeline_release(validator->pipeline);
    free(validator);
}

doe_vk_status doe_vk_draw_indirect_validate(doe_vk_commands *commands,
    const doe_vk_render_indirect *indirect, bool indexed) {
    if (indirect == NULL || indirect->validator == NULL ||
        indirect->validator->pipeline->context != commands->context || indirect->arguments == NULL ||
        indirect->arguments->context != commands->context || indirect->offset % 4 != 0 ||
        !(indirect->arguments->usage & DOE_VK_BUFFER_INDIRECT) ||
        !(indirect->arguments->usage & DOE_VK_BUFFER_COPY_SRC) ||
        !doe_vk_range(indirect->arguments->size, indirect->offset, indexed ? 20 : 16))
        return DOE_VK_INVALID_ARGUMENT;
    if (indirect->first_instance && !commands->context->caps.draw_indirect_first_instance) return DOE_VK_UNSUPPORTED;
    return indirect->arguments->content_status;
}

/* Keep each input offset and bound tuple distinct. GPU producers can change
 * argument bytes, so CPU contents never deduplicate records. */
doe_vk_status doe_vk_draw_indirect_prepare_batch(doe_vk_commands *commands,
    doe_vk_draw_indirect_request *requests, size_t count) {
    if (!count) return DOE_VK_OK;
    if (!requests || count>UINT32_MAX || count>SIZE_MAX/sizeof(VkBufferCopy)) return DOE_VK_INVALID_ARGUMENT;
    const doe_vk_caps *caps=&commands->context->caps;
    uint64_t alignment=caps->storage_offset_alignment;
    if (!alignment || (alignment & (alignment-1u))) return DOE_VK_INVALID_ARGUMENT;
    if (alignment<4) alignment=4;
    uint64_t cursor=0;
    for (size_t i=0;i<count;) {
        const doe_vk_draw_validator *validator=requests[i].indirect ? requests[i].indirect->validator : NULL;
        doe_vk_status status=doe_vk_draw_indirect_validate(commands,requests[i].indirect,requests[i].indexed);
        if (status!=DOE_VK_OK) return status;
        uint64_t maximum=caps->max_storage_buffer_range/32u;
        if (maximum>caps->max_workgroup_count[0]) maximum=caps->max_workgroup_count[0];
        if (!maximum) return DOE_VK_UNSUPPORTED;
        size_t group=1;
        if (validator->batch) while (i+group<count && group<maximum && requests[i+group].indirect &&
            requests[i+group].indirect->validator==validator) {
            status=doe_vk_draw_indirect_validate(commands,requests[i+group].indirect,requests[i+group].indexed);
            if (status!=DOE_VK_OK) return status;
            ++group;
        }
        if (cursor>UINT64_MAX-(alignment-1u)) return DOE_VK_OUT_OF_MEMORY;
        cursor=(cursor+alignment-1u)&~(alignment-1u);
        if (group>(UINT64_MAX-cursor)/32u) return DOE_VK_OUT_OF_MEMORY;
        for (size_t j=0;j<group;++j) {
            requests[i+j].offset=cursor+32u*j;
            requests[i+j].group_count=j ? 0u : (uint32_t)group;
        }
        cursor+=32u*group; i+=group;
    }
    doe_vk_buffer *scratch=NULL;
    doe_vk_status status=doe_vk_buffer_create(commands->context,cursor,
        DOE_VK_BUFFER_STORAGE|DOE_VK_BUFFER_COPY_DST|DOE_VK_BUFFER_INDIRECT,DOE_VK_MEMORY_HOST,&scratch,NULL);
    if (status!=DOE_VK_OK) return status;
    VkBufferCopy *copies=calloc(count,sizeof(*copies));
    if (!copies) { doe_vk_buffer_release(scratch); return DOE_VK_OUT_OF_MEMORY; }
    for (size_t i=0;i<count && status==DOE_VK_OK;++i) {
        const uint64_t copied=requests[i].indexed ? 20u : 16u;
        const uint32_t mode=(requests[i].indexed ? 1u : 0u)|(requests[i].indirect->first_instance ? 2u : 0u);
        const uint32_t tail[4]={requests[i].limits[0],requests[i].limits[1],requests[i].indexed ? mode : 0u,mode};
        status=doe_vk_buffer_write(scratch,requests[i].offset+copied,tail,(size_t)(32u-copied));
        copies[i]=(VkBufferCopy){requests[i].indirect->offset,requests[i].offset,copied};
    }
    if (status==DOE_VK_OK) status=doe_vk_use_buffer(commands,scratch);
    for (size_t i=0;i<count && status==DOE_VK_OK;++i)
        status=doe_vk_use_buffer(commands,requests[i].indirect->arguments);
    if (status==DOE_VK_OK) {
        doe_vk_barrier(commands);
        for (size_t i=0;i<count;) {
            size_t end=i+1u;
            while (end<count && requests[end].indirect->arguments==requests[i].indirect->arguments) ++end;
            commands->context->vk.vkCmdCopyBuffer(commands->command,requests[i].indirect->arguments->buffer,
                scratch->buffer,(uint32_t)(end-i),copies+i);
            i=end;
        }
        doe_vk_barrier(commands);
        for (size_t i=0;i<count && status==DOE_VK_OK;) {
            const doe_vk_draw_validator *validator=requests[i].indirect->validator;
            const uint32_t group=requests[i].group_count;
            const doe_vk_binding_resource binding={.kind=DOE_VK_BINDING_STORAGE,
                .value.buffer={scratch,requests[i].offset,(uint64_t)group*32u}};
            doe_vk_dispatch_use *use=NULL;
            status=doe_vk_prepare_resources_unsynchronized(commands,validator->pipeline,&binding,1,&use);
            if (status==DOE_VK_OK) {
                doe_vk_bind_prepared_resources(commands,use);
                commands->context->vk.vkCmdDispatch(commands->command,group,1,1);
            }
            i+=group;
        }
    }
    if (status==DOE_VK_OK) for (size_t i=0;i<count;++i) requests[i].buffer=scratch->buffer;
    free(copies);
    doe_vk_buffer_release(scratch);
    return status;
}
doe_vk_status doe_vk_draw_indirect_prepare(doe_vk_commands *commands,
    const doe_vk_render_indirect *indirect, const uint32_t limits[2], bool indexed, VkBuffer *out) {
    doe_vk_draw_indirect_request request={.indirect=indirect,.limits={limits[0],limits[1]},.indexed=indexed};
    doe_vk_status status=doe_vk_draw_indirect_prepare_batch(commands,&request,1);
    if (status==DOE_VK_OK) { doe_vk_barrier(commands); *out=request.buffer; }
    return status;
}
