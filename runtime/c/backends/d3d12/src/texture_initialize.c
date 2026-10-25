/* SPDX-License-Identifier: MIT */
#include "internal.h"

doe_dx_status doe_dx_commands_initialize_texture(doe_dx_commands *commands,doe_dx_texture *texture,doe_dx_error *error)
{
    doe_dx_status status=doe_dx_recording(commands,error);
    if (status!=DOE_DX_OK) return status;
    status=doe_dx_texture_preflight(commands,texture,0,false);
    if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"texture.initialize",error);
    uint64_t bytes=doe_dx_native_texture_upload_bytes(texture->native);
    doe_dx_buffer *zero=NULL;
    if (bytes) {
        status=doe_dx_buffer_create(commands->context,bytes,DOE_DX_UPLOAD,&zero,error);
        if (status!=DOE_DX_OK) return status;
    }
    doe_dx_reservation needed={.buffers=zero ? 1u : 0u,.textures=doe_dx_find_texture(commands,texture) ? 0u : 1u};
    status=doe_dx_reserve_uses(commands,needed,error);
    if (status==DOE_DX_OK) status=doe_dx_native_texture_initialize(commands->native,texture->native,zero ? zero->native : NULL,error);
    if (status==DOE_DX_OK) {
        if (zero) doe_dx_use_buffer(commands,zero);
        doe_dx_use_texture(commands,texture);
        doe_dx_find_texture(commands,texture)->initialized=true;
    }
    doe_dx_buffer_release(zero);
    return doe_dx_context_result(commands->context,status,error);
}
