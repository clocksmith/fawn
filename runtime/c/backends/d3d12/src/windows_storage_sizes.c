/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32
/* Root-CBV slots of 256 bytes a size table occupies; zero without a table.
 * Counts above 16384 exceed one ring arena and are rejected by the taker. */
UINT doe_dx_storage_size_slots(const doe_dx_native_resources *resources)
{
    return (UINT)(((size_t)resources->logical_size_count*4+255)/256);
}
/* Writes one uint per canonical resource: storage rows carry the exact bound
 * range size, every other row zero. The table lives in the descriptor use's
 * ring constant slots, so it shares that use's retirement-only lifetime. */
doe_dx_status doe_dx_write_storage_sizes(const doe_dx_native_resources *resources,
    uint8_t *table,size_t bytes,doe_dx_error *error)
{
    if (resources->logical_size_count>16384 || (size_t)resources->logical_size_count*4>bytes)
        return doe_dx_fail(DOE_DX_RANGE,0,"storage_sizes.count",error);
    memset(table,0,bytes); /* Write-only: the mapped UPLOAD slots are never read back. */
    for (size_t i=0;i<resources->buffer_count;++i) {
        uint32_t index=resources->logical_size_indices[i];
        if (index==UINT32_MAX) continue;
        if (index>=resources->logical_size_count || resources->buffers[i].size>UINT32_MAX)
            return doe_dx_fail(DOE_DX_RANGE,0,"storage_sizes.index",error);
        const uint32_t size=(uint32_t)resources->buffers[i].size;
        memcpy(table+(size_t)index*4,&size,4);
    }
    return DOE_DX_OK;
}
#endif
