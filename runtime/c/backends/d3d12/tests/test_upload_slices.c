/* SPDX-License-Identifier: MIT */
#include "mock_native.h"
#include <stdio.h>
int main(void)
{
    mock_reset(); doe_dx_context *context=NULL;
    assert(doe_dx_context_create(NULL,&context,NULL)==DOE_DX_OK);
    unsigned char values[32]={0}; doe_dx_upload_slice a={0},b={0},c={0};
    assert(doe_dx_upload_slice_write(context,values,12,4,&a,NULL)==DOE_DX_OK && a.offset==0);
    doe_dx_buffer *destination=NULL;
    assert(doe_dx_buffer_create(context,64,DOE_DX_DEFAULT,&destination,NULL)==DOE_DX_OK);
    doe_dx_commands *first=NULL;
    assert(doe_dx_commands_create(context,&first,NULL)==DOE_DX_OK);
    assert(doe_dx_commands_copy(first,a.buffer,a.offset,destination,0,a.size,NULL)==DOE_DX_OK);
    assert(doe_dx_upload_slice_write(context,values,16,256,&b,NULL)==DOE_DX_OK && b.buffer==a.buffer && b.offset==256);
    assert(doe_dx_commands_submit(first,NULL)==DOE_DX_OK);
    assert(doe_dx_upload_slice_write(context,values,32,4,&c,NULL)==DOE_DX_OK && c.buffer==a.buffer && c.offset==272);
    assert(a.buffer->upload_used==304 && mock.slice_offset==272 && mock.slice_size==32);
    assert(doe_dx_buffer_write(a.buffer,0,values,4,NULL)==DOE_DX_INVALID_STATE);
    doe_dx_upload_slice untouched={a.buffer,999,999};
    uint32_t references=a.buffer->references;
    mock.failure=MOCK_WRITE;
    assert(doe_dx_upload_slice_write(context,values,4,4,&untouched,NULL)==DOE_DX_NATIVE_FAILURE);
    assert(untouched.offset==999 && a.buffer->upload_used==304 && a.buffer->references==references);
    mock.failure=MOCK_NONE;
    assert(doe_dx_upload_slice_write(context,values,4,3,&untouched,NULL)==DOE_DX_INVALID_ARGUMENT);
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_PENDING && a.buffer->upload_used==304);
    doe_dx_buffer *page=a.buffer;
    doe_dx_buffer_release(a.buffer); doe_dx_buffer_release(b.buffer); doe_dx_buffer_release(c.buffer);
    assert(page->references>1); /* Pending first copy still owns the page. */
    doe_dx_commands_release(first); mock.completed=mock.last_signal;
    assert(doe_dx_context_poll(context,0,NULL)==DOE_DX_OK);
    size_t allocations=mock.allocation_attempts;
    assert(doe_dx_upload_slice_write(context,values,16,4,&a,NULL)==DOE_DX_OK && a.buffer==page && a.offset==0);
    assert(mock.allocation_attempts==allocations);
    doe_dx_buffer_release(a.buffer); doe_dx_buffer_release(destination);
    assert(doe_dx_context_destroy(context)==DOE_DX_OK); mock_reset();
    puts("PASS D3D12 upload slices: one page for recorded/pending disjoint suffixes, exact alignment, immutable prefixes, transactional failed snapshot, wrap only after retirement, zero allocations on reuse; host ownership only");
}
