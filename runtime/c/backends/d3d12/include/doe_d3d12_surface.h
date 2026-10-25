/* SPDX-License-Identifier: MIT */
#ifndef DOE_D3D12_SURFACE_H
#define DOE_D3D12_SURFACE_H
#include "doe_d3d12_texture.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct doe_dx_surface doe_dx_surface;
typedef struct doe_dx_surface_caps {
    doe_dx_format formats[4];
    uint32_t format_count, usages;
    bool immediate; /* Actual DXGI tearing support; FIFO is always available. */
} doe_dx_surface_caps;
typedef struct doe_dx_surface_desc {
    uint32_t width, height, usage;
    doe_dx_format format;
    bool immediate;
} doe_dx_surface_desc;
/* HWND is borrowed and must outlive this surface and unresolved presentation.
 * All calls share ordinary context serialization. No implicit adapter fallback. */
doe_dx_status doe_dx_surface_capabilities(doe_dx_context *,void *hwnd,doe_dx_surface_caps *,doe_dx_error *);
doe_dx_status doe_dx_surface_create(doe_dx_context *,void *hwnd,const doe_dx_surface_desc *,doe_dx_surface **,doe_dx_error *);
/* Resize only with no outstanding frame/view/command leases; BUSY preserves config. */
doe_dx_status doe_dx_surface_reconfigure(doe_dx_surface *,const doe_dx_surface_desc *,doe_dx_error *);
doe_dx_status doe_dx_surface_retain(doe_dx_surface *);
void doe_dx_surface_release(doe_dx_surface *);
/* Import actual current backbuffer and record PRESENT->COPY_DEST in the existing
 * command. Caller must initialize it and submit that same command before use.
 * Output owns one ordinary texture reference; no separate frame registry. */
doe_dx_status doe_dx_surface_acquire(doe_dx_surface *,doe_dx_commands *,doe_dx_texture **,doe_dx_error *);
/* Records COPY_DEST->PRESENT and a retained post-execute presentation obligation.
 * Present occurs only between ExecuteCommandLists and its fence Signal. */
doe_dx_status doe_dx_commands_present(doe_dx_commands *,doe_dx_surface *,doe_dx_texture *,doe_dx_error *);
/* Abandon a fully acquired frame without displaying it, through the same fence owner. */
doe_dx_status doe_dx_commands_discard_surface(doe_dx_commands *,doe_dx_surface *,doe_dx_texture *,doe_dx_error *);
#ifdef __cplusplus
}
#endif
#endif
