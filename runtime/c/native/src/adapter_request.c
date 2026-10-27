/* SPDX-License-Identifier: MIT */
#include "adapter_request.h"

bool native_adapter_request_decode(WGPUBackendType kind,
    const WGPURequestAdapterOptions *options, native_adapter_selection *out)
{
    if (!out) return false;
    native_adapter_selection selection = {0};
    const WGPUChainedStruct *chain = options ? options->nextInChain : NULL;
    while (chain) {
        /* Exactly one supported requirement: duplicate/cyclic LUID or every
         * other node rejects. No caller-owned chain or tail is rewritten. */
        /* Pinned Dawn rejects forced fallback before LUID lookup/decoding;
         * never infer software identity or substitute another adapter. */
        if (kind != WGPUBackendType_D3D12 || selection.has_luid || options->forceFallbackAdapter ||
            (uint32_t)chain->sType != NATIVE_DAWN_REQUEST_ADAPTER_LUID) return false;
        const native_windows_adapter_luid *luid = (const native_windows_adapter_luid *)chain;
        selection = (native_adapter_selection){true,luid->luid_low,luid->luid_high};
        chain = chain->next;
    }
    *out = selection;
    return true;
}
