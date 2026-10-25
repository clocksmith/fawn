/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

doe_dx_status doe_dx_native_timestamps_create(doe_dx_native_context *context, doe_dx_query_kind kind,uint32_t count,
    doe_dx_native_timestamps **out, doe_dx_error *error) {
    doe_dx_native_timestamps *timestamps = calloc(1, sizeof(*timestamps));
    if (!timestamps) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "native.timestamps.allocate", error);
    D3D12_QUERY_HEAP_DESC desc = {0};
    desc.Type = kind==DOE_DX_QUERY_TIMESTAMP ? D3D12_QUERY_HEAP_TYPE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    timestamps->kind=kind==DOE_DX_QUERY_TIMESTAMP ? D3D12_QUERY_TYPE_TIMESTAMP : D3D12_QUERY_TYPE_OCCLUSION;
    desc.Count = count;
    HRESULT result = ID3D12Device_CreateQueryHeap(context->device, &desc,
        &IID_ID3D12QueryHeap, (void **)&timestamps->heap);
    if (FAILED(result)) {
        doe_dx_native_timestamps_destroy(timestamps);
        return doe_dx_hresult(result, "CreateQueryHeap.timestamp", error);
    }
    *out = timestamps;
    return doe_dx_fail(DOE_DX_OK, 0, "native.timestamps.create", error);
}

void doe_dx_native_timestamps_destroy(doe_dx_native_timestamps *timestamps) {
    if (!timestamps) return;
    if (timestamps->heap) ID3D12QueryHeap_Release(timestamps->heap);
    free(timestamps);
}

doe_dx_status doe_dx_native_timestamp_frequency(doe_dx_native_context *context,
    uint64_t *out, doe_dx_error *error) {
    UINT64 frequency = 0;
    HRESULT result = ID3D12CommandQueue_GetTimestampFrequency(context->queue, &frequency);
    if (FAILED(result)) return doe_dx_hresult(result, "GetTimestampFrequency", error);
    if (!frequency) return doe_dx_fail(DOE_DX_UNAVAILABLE, 0, "GetTimestampFrequency.zero", error);
    *out = frequency;
    return doe_dx_fail(DOE_DX_OK, 0, "GetTimestampFrequency", error);
}

void doe_dx_native_timestamp(doe_dx_native_commands *commands,
    doe_dx_native_timestamps *timestamps, uint32_t index) {
    ID3D12GraphicsCommandList_EndQuery(commands->list, timestamps->heap, D3D12_QUERY_TYPE_TIMESTAMP, index);
}

void doe_dx_native_resolve_timestamps(doe_dx_native_commands *commands,
    doe_dx_native_timestamps *timestamps, uint32_t first, uint32_t count,
    doe_dx_native_buffer *destination, uint64_t offset) {
    ID3D12GraphicsCommandList_ResolveQueryData(commands->list, timestamps->heap,
        timestamps->kind, first, count, destination->resource, offset);
}
#endif
