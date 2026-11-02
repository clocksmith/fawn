/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_SHARED_H
#define DOE_D3D12_SHARED_H
#include "doe_d3d12_texture.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct doe_dx_shared_fence doe_dx_shared_fence;
/* DXGI NT handles are borrowed inputs; imports retain their own native leases.
 * Admission follows Dawn 758e5e6a's D3D12 simultaneous-access 2D subset. */
doe_dx_status doe_dx_shared_texture_import(doe_dx_context *,void *,bool,
    doe_dx_texture **,doe_dx_error *);
/* Caller serializes these with the texture's ordinary command owner. Acquire
 * may block on a keyed mutex; invoke outside the frontend device mutex. */
doe_dx_status doe_dx_shared_texture_acquire(doe_dx_texture *,doe_dx_error *);
doe_dx_status doe_dx_shared_texture_begin(doe_dx_texture *,bool,bool,doe_dx_error *);
doe_dx_status doe_dx_shared_texture_end(doe_dx_texture *,doe_dx_error *);
doe_dx_commands *doe_dx_shared_texture_owner(const doe_dx_texture *);
bool doe_dx_shared_texture_keyed(const doe_dx_texture *);
doe_dx_status doe_dx_shared_texture_release_point(doe_dx_texture *,
    doe_dx_shared_fence **,uint64_t *,bool *,doe_dx_error *);
doe_dx_status doe_dx_shared_fence_import(doe_dx_context *,void *,doe_dx_shared_fence **,doe_dx_error *);
doe_dx_status doe_dx_shared_fence_wait(doe_dx_shared_fence *,uint64_t,uint32_t,doe_dx_error *);
void *doe_dx_shared_fence_handle(const doe_dx_shared_fence *);
void doe_dx_shared_fence_release(doe_dx_shared_fence *);
#ifdef __cplusplus
}
#endif
#endif
