/* SPDX-License-Identifier: MIT
 * Typed surface call ledger only; no window, presentation or GPU simulation. */
#include "mock_native.h"
#include "../src/surface_native.h"
#include <stdlib.h>
struct doe_dx_native_surface {doe_dx_native_context *context;doe_dx_surface_desc desc;};
doe_dx_status doe_dx_native_surface_capabilities(doe_dx_native_context *context,void *window,
    doe_dx_surface_caps *out,doe_dx_error *error) {
    assert(context&&window);*out=(doe_dx_surface_caps){.formats={DOE_DX_BGRA8_UNORM,DOE_DX_RGBA8_UNORM},
        .format_count=2,.usages=DOE_DX_TEXTURE_RENDER_ATTACHMENT|DOE_DX_TEXTURE_COPY_SRC|
        DOE_DX_TEXTURE_COPY_DST|DOE_DX_TEXTURE_SAMPLED,.immediate=true};
    return doe_dx_fail(DOE_DX_OK,0,"mock.surface_caps",error);
}
doe_dx_status doe_dx_native_surface_create(doe_dx_native_context *context,void *window,
    const doe_dx_surface_desc *desc,doe_dx_native_surface **out,doe_dx_error *error) {
    assert(context&&window);doe_dx_native_surface *surface=calloc(1,sizeof(*surface));assert(surface);
    surface->context=context;surface->desc=*desc;++mock.surfaces;*out=surface;
    return doe_dx_fail(DOE_DX_OK,0,"mock.surface_create",error);
}
doe_dx_status doe_dx_native_surface_reconfigure(doe_dx_native_surface *surface,
    const doe_dx_surface_desc *desc,doe_dx_error *error) {
    surface->desc=*desc;return doe_dx_fail(DOE_DX_OK,0,"mock.ResizeBuffers",error);
}
void doe_dx_native_surface_destroy(doe_dx_native_surface *surface) {
    assert(surface&&mock.surfaces);--mock.surfaces;free(surface);
}
doe_dx_status doe_dx_native_surface_acquire(doe_dx_native_surface *surface,
    doe_dx_native_texture **out,uint32_t *index,doe_dx_error *error) {
    doe_dx_texture_shape shape={.image={surface->desc.width,surface->desc.height,surface->desc.format,surface->desc.usage},
        .dimension=2,.depth_or_layers=1,.mip_levels=1,.samples=1};
    doe_dx_status status=doe_dx_native_texture_create(surface->context,&shape,out,error);
    if(status==DOE_DX_OK)*index=mock.surface_index;
    return status;
}
doe_dx_status doe_dx_native_surface_transition(doe_dx_native_commands *commands,
    doe_dx_native_texture *texture,bool acquire,doe_dx_error *error) {
    assert(commands&&texture);++mock.surface_transitions;mock.surface_acquire_transition=acquire;
    return doe_dx_fail(DOE_DX_OK,0,"mock.surface_transition",error);
}
doe_dx_status doe_dx_native_surface_present(doe_dx_native_surface *surface,uint32_t index,doe_dx_error *error) {
    assert(surface&&index==mock.surface_index);++mock.presents;
    mock.present_execute_count=mock.executes;mock.present_signal_count=mock.signals;
    return doe_dx_fail(mock.fail_present?DOE_DX_NATIVE_FAILURE:DOE_DX_OK,
        mock.fail_present?-811:0,"mock.Present",error);
}
