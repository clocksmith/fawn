/* SPDX-License-Identifier: MIT
 * Memory D3D12 zeroed at creation: actual resource owner at the typed native
 * seam. The zeroing itself is a D3D12 creation guarantee, checked on hardware
 * by the physical fixtures, never by this control. */
#include "mock_native.h"
#include <stdio.h>

int main(void)
{
    mock_reset();
    doe_dx_context *owner = NULL;
    assert(doe_dx_context_create(NULL, &owner, NULL) == DOE_DX_OK);
    doe_dx_buffer *readback = NULL, *filled = NULL, *storage = NULL, *target = NULL;
    doe_dx_buffer *known = NULL, *unknown = NULL;
    bool known_zero = false, unknown_zero = true;
    /* The zeroed READBACK constructor skips its CPU fill for zeroed memory and
     * is initialized either way. */
    mock.created_zeroed = true;
    assert(doe_dx_buffer_create_readback_zeroed(owner, 64, &readback, NULL) == DOE_DX_OK);
    assert(mock.readback_zeroes == 0u);
    /* DEFAULT initialization stays explicit even when the memory was zeroed. */
    assert(doe_dx_buffer_create(owner, 64, DOE_DX_DEFAULT, &storage, NULL) == DOE_DX_OK);
    /* The additive DEFAULT constructor publishes D3D12-zeroed memory initialized. */
    const uint64_t large = (UINT64_C(1) << 20) + 256u;
    assert(doe_dx_buffer_create_default_known_zero(owner, large, &known, &known_zero, NULL) == DOE_DX_OK);
    assert(known_zero);
    /* At most 1 MiB keeps the deferred copy even in zeroed memory. */
    doe_dx_buffer *small = NULL;
    bool small_zero = true;
    assert(doe_dx_buffer_create_default_known_zero(owner, UINT64_C(1) << 20, &small, &small_zero, NULL) == DOE_DX_OK);
    assert(!small_zero && !small->initialized);
    doe_dx_buffer_release(small);
    assert(doe_dx_buffer_create_default_known_zero(owner, 64, NULL, &known_zero, NULL) == DOE_DX_INVALID_ARGUMENT);
    mock.created_zeroed = false;
    assert(doe_dx_buffer_create_default_known_zero(owner, 64, &unknown, &unknown_zero, NULL) == DOE_DX_OK);
    assert(!unknown_zero);
    assert(doe_dx_buffer_create_readback_zeroed(owner, 64, &filled, NULL) == DOE_DX_OK);
    assert(mock.readback_zeroes == 1u);
    assert(doe_dx_buffer_create(owner, 64, DOE_DX_READBACK, &target, NULL) == DOE_DX_OK);
    unsigned char bytes[64];
    assert(doe_dx_buffer_read(readback, 0, bytes, sizeof(bytes), NULL) == DOE_DX_OK);
    assert(doe_dx_buffer_read(filled, 0, bytes, sizeof(bytes), NULL) == DOE_DX_OK);
    doe_dx_commands *work = NULL;
    assert(doe_dx_commands_create(owner, &work, NULL) == DOE_DX_OK);
    assert(doe_dx_commands_copy(work, storage, 0, target, 0, 64, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_copy(work, unknown, 0, target, 0, 64, NULL) == DOE_DX_UNINITIALIZED);
    assert(doe_dx_commands_copy(work, known, 0, target, 0, 64, NULL) == DOE_DX_OK);
    doe_dx_commands_release(work);
    doe_dx_buffer_release(known);
    doe_dx_buffer_release(unknown);
    doe_dx_buffer_release(readback);
    doe_dx_buffer_release(filled);
    doe_dx_buffer_release(storage);
    doe_dx_buffer_release(target);
    assert(doe_dx_context_destroy(owner) == DOE_DX_OK);
    mock_reset();
    puts("PASS D3D12 zeroed creation: the zeroed READBACK constructor skips its CPU fill only for D3D12-zeroed memory and is readable either way; plain DEFAULT initialization stays explicit while the known-zero DEFAULT constructor publishes only D3D12-zeroed memory above 1 MiB initialized; native-call seam only");
    return 0;
}
