/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>
int main(void)
{
    mock_reset(); doe_dx_context *context=NULL;
    assert(doe_dx_context_create(NULL,&context,NULL)==DOE_DX_OK);
    doe_dx_buffer *upload=NULL,*destination=NULL;
    assert(doe_dx_buffer_create(context,64,DOE_DX_UPLOAD,&upload,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&destination,NULL)==DOE_DX_OK);
    void *bytes=NULL;
    assert(doe_dx_buffer_lend_write(upload,8,16,&bytes,NULL)==DOE_DX_OK && bytes && mock.lends==1);
    assert(upload->lent_write && upload->lend_offset==8 && upload->lend_size==16);
    doe_dx_commands *commands=NULL;
    assert(doe_dx_commands_create(context,&commands,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_copy(commands,upload,0,destination,0,64,NULL)==DOE_DX_INVALID_STATE);
    unsigned char value=7;
    assert(doe_dx_buffer_write(upload,0,&value,1,NULL)==DOE_DX_INVALID_STATE);
    void *canary=bytes;
    assert(doe_dx_buffer_lend_write(upload,0,8,&canary,NULL)==DOE_DX_INVALID_STATE && canary==bytes);
    doe_dx_buffer_return_read(upload); doe_dx_buffer_return_read(upload);
    assert(!upload->lent && !mock.lends);
    assert(doe_dx_commands_copy(commands,upload,0,destination,0,64,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_lend_write(upload,0,8,&canary,NULL)==DOE_DX_BUSY && canary==bytes);
    assert(doe_dx_commands_submit(commands,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_lend_write(upload,0,8,&canary,NULL)==DOE_DX_BUSY);
    doe_dx_commands_release(commands); mock.completed=mock.last_signal;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    mock.failure=MOCK_READ;
    assert(doe_dx_buffer_lend_write(upload,0,8,&canary,NULL)==DOE_DX_NATIVE_FAILURE && canary==bytes && !upload->lent);
    mock.failure=MOCK_NONE;
    assert(doe_dx_buffer_lend_write(upload,60,8,&canary,NULL)==DOE_DX_RANGE);
    assert(doe_dx_buffer_lend_write(destination,0,8,&canary,NULL)==DOE_DX_UNSUPPORTED);
    assert(doe_dx_buffer_lend_write(upload,0,8,&canary,NULL)==DOE_DX_OK);
    assert(doe_dx_buffer_evict(upload)==DOE_DX_OK && !mock.lends);
    assert(doe_dx_buffer_lend_write(upload,0,8,&canary,NULL)==DOE_DX_INVALID_STATE);
    doe_dx_buffer_release(upload); doe_dx_buffer_release(destination);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK); mock_reset();
    puts("PASS D3D12 writable UPLOAD lend: exact span, mutable lease, recording/pending exclusion, failed Map output, range/type checks, return/eviction; host ownership only");
}
