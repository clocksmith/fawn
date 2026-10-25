/* SPDX-License-Identifier: MIT */
#include "d3d12_queries.h"
#include "d3d12_draw_guard.h"
#include "../../common/include/doe_timestamp.h"

enum { TIMESTAMP_WORKGROUP=64 };
_Static_assert((unsigned)DOE_DX_MAX_QUERIES>=(unsigned)DOE_RESOURCE_MAX_QUERIES,"query owner covers public query capacity");
native_failure native_d3d12_timestamp_initialize(native_device *device)
{
    uint64_t frequency=device->backend.value.d3d12.caps.timestamp_frequency;
    if (!frequency) return native_failure_status(NATIVE_UNSUPPORTED);
    float period=(float)(1000000000.0/(double)frequency);
    char source[DOE_TIMESTAMP_SOURCE_CAPACITY]; size_t length=0;
    if (doe_timestamp_source(period,64,TIMESTAMP_WORKGROUP,source,sizeof(source),&length)!=DOE_TIMESTAMP_SOURCE_OK)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    native_d3d12_kernel_code *code=NULL;
    native_failure failure=native_d3d12_storage_compile(source,length,TIMESTAMP_WORKGROUP,1,&code);
    if (failure.status==NATIVE_OK) failure=native_d3d12_kernel_pipeline(device,code,&device->d3d12_timestamp_pipeline);
    native_d3d12_kernel_code_release(code);
    return failure;
}
void native_d3d12_timestamp_close(native_device *device)
{
#if defined(_WIN32)
    doe_dx_pipeline_release(device->d3d12_timestamp_pipeline);
#endif
    device->d3d12_timestamp_pipeline=NULL;
}
native_failure native_d3d12_queries_create(native_device *device,WGPUQueryType type,uint32_t count,doe_dx_queries **out)
{
#if defined(_WIN32)
    if (type!=WGPUQueryType_Timestamp && type!=WGPUQueryType_Occlusion) return native_failure_status(NATIVE_INVALID_ARGUMENT);
    doe_dx_error error={0};
    doe_dx_status status=doe_dx_queries_create(device->backend.value.d3d12.context,
        type==WGPUQueryType_Timestamp ? DOE_DX_QUERY_TIMESTAMP : DOE_DX_QUERY_OCCLUSION,count,out,&error);
    return native_failure_d3d12(status,&error);
#else
    (void)device; (void)type; (void)count; (void)out;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
void native_d3d12_queries_release(doe_dx_queries *queries)
{
#if defined(_WIN32)
    doe_dx_timestamps_release(queries);
#else
    (void)queries;
#endif
}
native_failure native_d3d12_record_query(native_command_handle *commands,const doe_resource_command *record)
{
#if defined(_WIN32)
    doe_dx_error error={0};
    if (record->kind==DOE_COMMAND_WRITE_TIMESTAMP) {
        native_queries *queries=doe_resource_query_set_native(record->value.timestamp.set);
        if (!queries || !queries->d3d12_queries) return native_failure_status(NATIVE_INVALID_ARGUMENT);
        doe_dx_status status=doe_dx_commands_timestamp(commands->value.d3d12.commands,
            queries->d3d12_queries,record->value.timestamp.index,&error);
        return native_failure_d3d12(status,&error);
    }
    if (record->kind!=DOE_COMMAND_RESOLVE_QUERY) return native_failure_status(NATIVE_UNSUPPORTED);
    uint32_t count=record->value.resolve.count,first=record->value.resolve.first;
    if (!count) return native_failure_status(NATIVE_OK);
    native_queries *queries=doe_resource_query_set_native(record->value.resolve.set);
    native_buffer *destination=doe_resource_buffer_native(record->value.resolve.destination);
    if (!queries || !queries->d3d12_queries || !destination || !destination->handle.d3d12)
        return native_failure_status(NATIVE_INVALID_ARGUMENT);
    bool timestamp=doe_resource_query_set_type(record->value.resolve.set)==WGPUQueryType_Timestamp;
    if (timestamp && !queries->device->d3d12_timestamp_pipeline) return native_failure_status(NATIVE_INVALID_STATE);
    uint64_t size=(uint64_t)count*8;
    doe_dx_buffer *zero=NULL,*scratch=NULL;
    doe_dx_status status=doe_dx_buffer_create(commands->value.d3d12.context,size,DOE_DX_UPLOAD,&zero,&error);
    if (status==DOE_DX_OK) status=doe_dx_buffer_create(commands->value.d3d12.context,size,DOE_DX_DEFAULT,&scratch,&error);
    if (status==DOE_DX_OK) status=doe_dx_commands_copy(commands->value.d3d12.commands,zero,0,scratch,0,size,&error);
    for (uint32_t i=0;status==DOE_DX_OK && i<count;) {
        bool available=false;
        status=doe_dx_queries_available(commands->value.d3d12.commands,queries->d3d12_queries,first+i,&available);
        uint32_t run=1;
        while (status==DOE_DX_OK && run<count-i) {
            bool next=false;
            status=doe_dx_queries_available(commands->value.d3d12.commands,queries->d3d12_queries,first+i+run,&next);
            if (status!=DOE_DX_OK || next!=available) break;
            ++run;
        }
        if (status==DOE_DX_OK && available) status=doe_dx_commands_resolve_queries(commands->value.d3d12.commands,
            queries->d3d12_queries,first+i,run,scratch,(uint64_t)i*8,&error);
        i+=run;
    }
    const doe_dx_buffer_binding binding={scratch,0,size};
    if (status==DOE_DX_OK && timestamp) status=doe_dx_commands_dispatch(commands->value.d3d12.commands,
        queries->device->d3d12_timestamp_pipeline,&binding,1,(count-1)/TIMESTAMP_WORKGROUP+1,1,1,&error);
    if (status==DOE_DX_OK) status=doe_dx_commands_copy(commands->value.d3d12.commands,scratch,0,
        destination->handle.d3d12->allocation,record->value.resolve.offset,size,&error);
    doe_dx_buffer_release(zero); doe_dx_buffer_release(scratch);
    return native_failure_d3d12(status,&error);
#else
    (void)commands; (void)record;
    return native_failure_status(NATIVE_UNAVAILABLE);
#endif
}
