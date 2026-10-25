/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32
/* Reuse the real native buffer allocation/mapping owner, but retain its plain
 * payload on the existing descriptor-use node rather than creating a registry. */
doe_dx_status doe_dx_prepare_storage_sizes(doe_dx_native_commands *commands,
    const doe_dx_native_resources *resources,doe_dx_native_buffer **out,doe_dx_error *error)
{
    if (!resources->logical_size_count) return DOE_DX_OK;
    if (resources->logical_size_count>16384) return doe_dx_fail(DOE_DX_RANGE,0,"storage_sizes.count",error);
    size_t bytes=((size_t)resources->logical_size_count*4+255)&~(size_t)255;
    uint32_t *table=calloc(1,bytes);
    if (!table) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"storage_sizes.table",error);
    for (size_t i=0;i<resources->buffer_count;++i) {
        uint32_t index=resources->logical_size_indices[i];
        if (index==UINT32_MAX) continue;
        if (index>=resources->logical_size_count || resources->buffers[i].size>UINT32_MAX) {
            free(table);
            return doe_dx_fail(DOE_DX_RANGE,0,"storage_sizes.index",error);
        }
        table[index]=(uint32_t)resources->buffers[i].size;
    }
    doe_dx_native_buffer *buffer=NULL;
    doe_dx_status status=doe_dx_native_buffer_create(commands->context,bytes,DOE_DX_UPLOAD,&buffer,error);
    if (status==DOE_DX_OK) status=doe_dx_native_write(buffer,0,table,bytes,error);
    free(table);
    if (status!=DOE_DX_OK) { doe_dx_native_buffer_destroy(buffer); return status; }
    *out=buffer;
    return DOE_DX_OK;
}
#endif
