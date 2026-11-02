/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>
static void failure(bool unknown)
{
    mock_reset(); doe_dx_context *context=NULL;
    doe_dx_buffer *source=NULL,*destination=NULL; doe_dx_commands *commands=NULL;
    assert(doe_dx_context_create(NULL,&context,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&source,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&destination,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,source,0,destination,0,64,NULL)==DOE_DX_OK);
    mock.execute_status=DOE_DX_NATIVE_FAILURE;
    if (unknown) mock.failure=MOCK_SIGNAL;
    doe_dx_error error={0};
    assert(doe_dx_commands_submit(commands,&error)==(unknown ? DOE_DX_COMPLETION_UNKNOWN : DOE_DX_NATIVE_FAILURE));
    assert(error.native_code==-901 && !strcmp(error.operation,"mock.queue_wait"));
    assert(!mock.executes && !commands->work_executed && destination->owner==commands);
    assert(!destination->initialized && !destination->accepted_initialized);
    assert(doe_dx_commands_result(commands,NULL)==(unknown ? DOE_DX_COMPLETION_UNKNOWN : DOE_DX_PENDING));
    mock.failure=MOCK_NONE; mock.execute_status=DOE_DX_OK;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_PENDING);
    assert(destination->owner==commands && !destination->pending);
    mock.completed=mock.last_signal;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_result(commands,&error)==DOE_DX_NATIVE_FAILURE && error.native_code==-901);
    assert(!destination->initialized && !destination->owner && !destination->pending);
    doe_dx_commands_release(commands); doe_dx_buffer_release(source); doe_dx_buffer_release(destination);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK); mock_reset();
}
int main(void)
{
    failure(false); failure(true);
    puts("PASS D3D12 failed pre-execute queue waits: no work/init publication, resource leases retained through known/unknown signals and recovery, original failure preserved at retirement; host only");
}
