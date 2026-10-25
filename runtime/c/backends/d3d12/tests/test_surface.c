/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>
static doe_dx_commands *commands(doe_dx_context *context) {
    doe_dx_commands *out=NULL;assert(doe_dx_commands_create(context,&out,NULL)==DOE_DX_OK);return out;
}
static void complete(doe_dx_context *context,doe_dx_commands *work) {
    assert(doe_dx_commands_submit(work,NULL)==DOE_DX_OK);
    mock.completed=mock.last_signal;assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    doe_dx_commands_release(work);
}
static void lifecycle(bool discard,bool fail) {
    doe_dx_context *context=NULL;assert(doe_dx_context_create(NULL,&context,NULL)==DOE_DX_OK);
    doe_dx_surface_desc desc={.width=4,.height=4,.format=DOE_DX_BGRA8_UNORM,
        .usage=DOE_DX_TEXTURE_RENDER_ATTACHMENT|DOE_DX_TEXTURE_COPY_SRC};
    doe_dx_surface *surface=NULL;
    assert(doe_dx_surface_create(context,context,&desc,&surface,NULL)==DOE_DX_OK);
    doe_dx_commands *work=commands(context);doe_dx_texture *frame=NULL;
    mock.surface_index=1;
    assert(doe_dx_surface_acquire(surface,work,&frame,NULL)==DOE_DX_OK);
    assert(frame->surface_index==1&&mock.surface_acquire_transition);
    assert(doe_dx_commands_present(work,surface,frame,NULL)==DOE_DX_UNINITIALIZED);
    assert(doe_dx_surface_reconfigure(surface,&desc,NULL)==DOE_DX_BUSY);
    assert(doe_dx_commands_initialize_texture(work,frame,NULL)==DOE_DX_OK);
    complete(context,work);assert(frame->surface_copy_dest);
    work=commands(context);
    assert((discard?doe_dx_commands_discard_surface(work,surface,frame,NULL):
        doe_dx_commands_present(work,surface,frame,NULL))==DOE_DX_OK);
    assert(!mock.presents&&!mock.surface_acquire_transition);
    mock.fail_present=fail;mock.failure=fail?MOCK_SIGNAL:MOCK_NONE;
    doe_dx_error error={0};
    assert(doe_dx_commands_submit(work,&error)==(fail?DOE_DX_COMPLETION_UNKNOWN:DOE_DX_OK));
    if(!discard){assert(mock.presents==1&&mock.present_execute_count==2&&mock.present_signal_count==1);}
    else assert(!mock.presents);
    if(fail){assert(error.native_code==-811);assert(doe_dx_commands_result(work,NULL)==DOE_DX_COMPLETION_UNKNOWN);}
    assert(doe_dx_surface_reconfigure(surface,&desc,NULL)==DOE_DX_BUSY);
    mock.failure=MOCK_NONE;mock.completed=2;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_result(work,&error)==(fail?DOE_DX_NATIVE_FAILURE:DOE_DX_OK));
    if(fail)assert(error.native_code==-811);
    assert(!frame->surface_copy_dest&&!doe_dx_surface_texture_recordable(frame));
    doe_dx_commands_release(work);
    assert(doe_dx_surface_reconfigure(surface,&desc,NULL)==DOE_DX_BUSY);
    doe_dx_texture_release(frame);desc.width=8;
    assert(doe_dx_surface_reconfigure(surface,&desc,NULL)==DOE_DX_OK);
    work=commands(context);frame=NULL;
    assert(doe_dx_surface_acquire(surface,work,&frame,NULL)==DOE_DX_OK);
    assert(frame->desc.width==8);
    doe_dx_texture_release(frame);doe_dx_commands_release(work);
    doe_dx_surface_release(surface);assert(doe_dx_context_destroy(context)==DOE_DX_OK);mock_reset();
}
static void abandoned_acquisition(void) {
    doe_dx_context *context=NULL;assert(doe_dx_context_create(NULL,&context,NULL)==DOE_DX_OK);
    doe_dx_surface_desc desc={.width=1,.height=1,.format=DOE_DX_RGBA8_UNORM,.usage=DOE_DX_TEXTURE_RENDER_ATTACHMENT};
    doe_dx_surface *surface=NULL;assert(doe_dx_surface_create(context,context,&desc,&surface,NULL)==DOE_DX_OK);
    doe_dx_commands *work=commands(context);doe_dx_texture *frame=NULL;
    assert(doe_dx_surface_acquire(surface,work,&frame,NULL)==DOE_DX_OK);
    complete(context,work);doe_dx_texture_release(frame);frame=NULL;
    work=commands(context);
    assert(doe_dx_surface_acquire(surface,work,&frame,NULL)==DOE_DX_INVALID_STATE&&!frame);
    doe_dx_commands_release(work);doe_dx_surface_release(surface);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK);mock_reset();
}
int main(void) {mock_reset();lifecycle(false,false);lifecycle(true,false);lifecycle(false,true);
    abandoned_acquisition();puts("D3D12 surface owner: ordering, leases, discard, resize and first-error controls passed");return 0;}
