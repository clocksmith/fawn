/* SPDX-License-Identifier: MIT */
#ifndef DOE_DX_SURFACE_NATIVE_H
#define DOE_DX_SURFACE_NATIVE_H
#include "native.h"
#include "../include/doe_d3d12_surface.h"
typedef struct doe_dx_native_surface doe_dx_native_surface;
doe_dx_status doe_dx_native_surface_capabilities(doe_dx_native_context *,void *,doe_dx_surface_caps *,doe_dx_error *);
doe_dx_status doe_dx_native_surface_create(doe_dx_native_context *,void *,const doe_dx_surface_desc *,doe_dx_native_surface **,doe_dx_error *);
doe_dx_status doe_dx_native_surface_reconfigure(doe_dx_native_surface *,const doe_dx_surface_desc *,doe_dx_error *);
void doe_dx_native_surface_destroy(doe_dx_native_surface *);
doe_dx_status doe_dx_native_surface_acquire(doe_dx_native_surface *,doe_dx_native_texture **,uint32_t *,doe_dx_error *);
doe_dx_status doe_dx_native_surface_transition(doe_dx_native_commands *,doe_dx_native_texture *,bool,doe_dx_error *);
doe_dx_status doe_dx_native_surface_present(doe_dx_native_surface *,uint32_t,doe_dx_error *);
#endif
