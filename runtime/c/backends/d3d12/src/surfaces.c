/* SPDX-License-Identifier: MIT */
#include "internal.h"
#include "surface_native.h"
struct doe_dx_surface {
    doe_dx_context *context;
    doe_dx_native_surface *native;
    doe_dx_surface_desc desc;
    void *window;
    doe_dx_texture *current; /* Borrowed from an ordinary texture lease. */
    doe_dx_commands *present; /* Borrowed from the existing command owner. */
    uint32_t references;
    bool failed;
};
doe_dx_status doe_dx_surface_capabilities(doe_dx_context *context,void *hwnd,
    doe_dx_surface_caps *out,doe_dx_error *error) {
    if (!context || !hwnd || !out) return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"surface.capabilities",error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"surface.device",error);
    doe_dx_error detail={0};doe_dx_surface_caps caps={0};
    doe_dx_status status=doe_dx_native_surface_capabilities(context->native,hwnd,&caps,&detail);
    if (status==DOE_DX_OK) *out=caps;
    if (error) *error=detail;
    return doe_dx_context_result(context,status,&detail);
}
doe_dx_status doe_dx_surface_create(doe_dx_context *context,void *hwnd,
    const doe_dx_surface_desc *desc,doe_dx_surface **out,doe_dx_error *error) {
    if (!context || !hwnd || !desc || !out || !desc->width || !desc->height ||
        desc->width>DOE_DX_MAX_TEXTURE_DIMENSION_2D || desc->height>DOE_DX_MAX_TEXTURE_DIMENSION_2D ||
        !(desc->usage&DOE_DX_TEXTURE_RENDER_ATTACHMENT))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"surface.create",error);
    doe_dx_surface_caps caps={0};doe_dx_status status=doe_dx_surface_capabilities(context,hwnd,&caps,error);
    if (status!=DOE_DX_OK) return status;
    bool format=false;
    for (uint32_t i=0;i<caps.format_count;++i) if (caps.formats[i]==desc->format) format=true;
    if (!format || (desc->usage&~caps.usages) || (desc->immediate&&!caps.immediate))
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"surface.configuration",error);
    if (context->objects==SIZE_MAX) return doe_dx_fail(DOE_DX_RANGE,0,"surface.objects",error);
    doe_dx_surface *surface=calloc(1,sizeof(*surface));
    if (!surface) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"surface.allocate",error);
    doe_dx_error detail={0};status=doe_dx_native_surface_create(context->native,hwnd,desc,&surface->native,&detail);
    if (status!=DOE_DX_OK) {free(surface);if(error)*error=detail;return doe_dx_context_result(context,status,&detail);}
    surface->context=context;surface->window=hwnd;surface->desc=*desc;surface->references=1;context->objects++;
    *out=surface;return doe_dx_fail(DOE_DX_OK,0,"surface.create",error);
}
doe_dx_status doe_dx_surface_retain(doe_dx_surface *surface) {
    if (!surface) return DOE_DX_INVALID_ARGUMENT;
    if (surface->references==UINT32_MAX) return DOE_DX_RANGE;
    ++surface->references;return DOE_DX_OK;
}
void doe_dx_surface_release(doe_dx_surface *surface) {
    if (!surface || --surface->references) return;
    if (surface->current || surface->present) abort();
    doe_dx_native_surface_destroy(surface->native);surface->context->objects--;free(surface);
}
void doe_dx_surface_texture_release(doe_dx_surface *surface,doe_dx_texture *texture) {
    if (surface->current==texture) {
        if (texture->surface_copy_dest) surface->failed=true;
        surface->current=NULL;
    }
    doe_dx_surface_release(surface);
}
void doe_dx_surface_acquisition_complete(doe_dx_texture *texture) {
    if (texture->surface && texture->surface->current==texture) texture->surface_copy_dest=true;
}
bool doe_dx_surface_texture_recordable(const doe_dx_texture *texture) {
    return !texture->surface || (texture->surface->current==texture && !texture->surface->present && !texture->surface->failed);
}
doe_dx_status doe_dx_surface_acquire(doe_dx_surface *surface,doe_dx_commands *commands,
    doe_dx_texture **out,doe_dx_error *error) {
    if (!surface || !commands || !out || surface->context!=commands->context)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"surface.acquire",error);
    doe_dx_status status=doe_dx_recording(commands,error);if(status!=DOE_DX_OK)return status;
    if (surface->failed) return doe_dx_fail(DOE_DX_INVALID_STATE,0,"surface.failed",error);
    if (surface->current || surface->present) return doe_dx_fail(DOE_DX_BUSY,0,"surface.frame",error);
    if (surface->references==UINT32_MAX || surface->context->objects==SIZE_MAX)
        return doe_dx_fail(DOE_DX_RANGE,0,"surface.frame_budget",error);
    doe_dx_texture *texture=calloc(1,sizeof(*texture));
    if (!texture) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY,0,"surface.frame_allocate",error);
    status=doe_dx_reserve_uses(commands,(doe_dx_reservation){.textures=1},error);
    if(status!=DOE_DX_OK){free(texture);return status;}
    doe_dx_error detail={0};
    status=doe_dx_native_surface_acquire(surface->native,&texture->native,&texture->surface_index,&detail);
    if(status!=DOE_DX_OK){free(texture);if(error)*error=detail;return doe_dx_context_result(surface->context,status,&detail);}
    texture->context=surface->context;texture->references=1;texture->samples=1;
    texture->desc=(doe_dx_texture_desc){surface->desc.width,surface->desc.height,surface->desc.format,surface->desc.usage};
    texture->shape=(doe_dx_texture_shape){.image=texture->desc,.dimension=2,.depth_or_layers=1,.mip_levels=1,.samples=1};
    texture->surface=surface;surface->references++;surface->context->objects++;surface->current=texture;
    status=doe_dx_native_surface_transition(commands->native,texture->native,true,error);
    if(status!=DOE_DX_OK){doe_dx_texture_release(texture);return status;}
    doe_dx_use_texture(commands,texture);
    doe_dx_find_texture(commands,texture)->surface_acquire=true;
    *out=texture;
    return doe_dx_fail(DOE_DX_OK,0,"surface.acquire",error);
}
static doe_dx_status prepare_present(doe_dx_commands *commands,doe_dx_surface *surface,
    doe_dx_texture *texture,bool discard,doe_dx_error *error) {
    if (!commands || !surface || !texture || surface->context!=commands->context ||
        surface->current!=texture || texture->surface!=surface)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"surface.present",error);
    doe_dx_status status=doe_dx_recording(commands,error);if(status!=DOE_DX_OK)return status;
    if (commands->present_surface || surface->present || surface->failed)
        return doe_dx_fail(DOE_DX_INVALID_STATE,0,"surface.present_state",error);
    status=doe_dx_texture_preflight(commands,texture,DOE_DX_TEXTURE_RENDER_ATTACHMENT,!discard);
    if(status==DOE_DX_OK)status=doe_dx_reserve_uses(commands,(doe_dx_reservation){.textures=1},error);
    if(status!=DOE_DX_OK)return doe_dx_fail(status,0,"surface.present_texture",error);
    if(surface->references==UINT32_MAX)return doe_dx_fail(DOE_DX_RANGE,0,"surface.present_lease",error);
    status=doe_dx_native_surface_transition(commands->native,texture->native,false,error);
    if(status!=DOE_DX_OK)return status;
    doe_dx_use_texture(commands,texture);surface->references++;
    commands->present_discard=discard;
    commands->present_surface=surface;surface->present=commands;
    return doe_dx_fail(DOE_DX_OK,0,"surface.present_record",error);
}
doe_dx_status doe_dx_commands_present(doe_dx_commands *commands,doe_dx_surface *surface,
    doe_dx_texture *texture,doe_dx_error *error) {return prepare_present(commands,surface,texture,false,error);}
doe_dx_status doe_dx_commands_discard_surface(doe_dx_commands *commands,doe_dx_surface *surface,
    doe_dx_texture *texture,doe_dx_error *error) {return prepare_present(commands,surface,texture,true,error);}
doe_dx_status doe_dx_surface_execute_present(doe_dx_commands *commands,doe_dx_error *error) {
    doe_dx_surface *surface=commands->present_surface;
    if(!surface || commands->present_discard)return DOE_DX_OK;
    return doe_dx_native_surface_present(surface->native,surface->current->surface_index,error);
}
void doe_dx_surface_retire_present(doe_dx_commands *commands,bool submitted,bool success) {
    doe_dx_surface *surface=commands->present_surface;if(!surface)return;
    if(surface->present!=commands)abort();
    surface->present=NULL;
    if(submitted){
        if(surface->current)surface->current->surface_copy_dest=false;
        surface->current=NULL;if(!success)surface->failed=true;
    }
    commands->present_surface=NULL;doe_dx_surface_release(surface);
}

doe_dx_status doe_dx_surface_reconfigure(doe_dx_surface *surface,const doe_dx_surface_desc *desc,doe_dx_error *error) {
    if(!surface||!desc||!desc->width||!desc->height||desc->width>DOE_DX_MAX_TEXTURE_DIMENSION_2D||
        desc->height>DOE_DX_MAX_TEXTURE_DIMENSION_2D||!(desc->usage&DOE_DX_TEXTURE_RENDER_ATTACHMENT))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"surface.reconfigure",error);
    if(surface->references!=1||surface->current||surface->present)
        return doe_dx_fail(DOE_DX_BUSY,0,"surface.retained_frames",error);
    doe_dx_surface_caps caps={0};doe_dx_status status=doe_dx_surface_capabilities(surface->context,surface->window,&caps,error);
    if(status!=DOE_DX_OK)return status;
    bool format=false;for(uint32_t i=0;i<caps.format_count;++i)if(caps.formats[i]==desc->format)format=true;
    if(!format||(desc->usage&~caps.usages)||(desc->immediate&&!caps.immediate)||desc->usage!=surface->desc.usage)
        return doe_dx_fail(DOE_DX_UNSUPPORTED,0,"surface.reconfigure_contract",error);
    doe_dx_error detail={0};status=doe_dx_native_surface_reconfigure(surface->native,desc,&detail);
    if(status==DOE_DX_OK){surface->desc=*desc;surface->failed=false;}
    if(error)*error=detail;
    return doe_dx_context_result(surface->context,status,&detail);
}
