/* SPDX-License-Identifier: MIT */
#include "internal.h"

static bool available(doe_dx_buffer *page,uint64_t size,uint64_t alignment,uint64_t *offset)
{
    if (!page || !page->upload_slices || page->evict_requested || page->lent ||
        page->upload_used>UINT64_MAX-(alignment-1)) return false;
    uint64_t start=(page->upload_used+alignment-1)&~(alignment-1);
    if (!doe_dx_range(page->size,start,size)) return false;
    *offset=start; return true;
}
doe_dx_status doe_dx_upload_slice_write(doe_dx_context *context,const void *bytes,size_t size,uint64_t alignment,
    doe_dx_upload_slice *out,doe_dx_error *error)
{
    if (!context || !bytes || !size || !out || !alignment || (alignment & (alignment-1)))
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT,0,"upload_slice.arguments",error);
    if (context->lost) return doe_dx_fail(DOE_DX_DEVICE_LOST,0,"upload_slice.lost",error);
    doe_dx_buffer *page=NULL; uint64_t offset=0;
    for (size_t i=0;i<DOE_DX_UPLOAD_PAGES;++i) {
        doe_dx_buffer *candidate=i ? context->upload_pages[i-1] : context->upload_staging;
        /* No caller/command/event lease remains: only then may a prefix wrap. */
        if (candidate && candidate->references==1 && !candidate->owner && !candidate->pending) {
            candidate->upload_used=0; candidate->upload_slices=true;
        }
        if (available(candidate,size,alignment,&offset)) { page=candidate; break; }
    }
    doe_dx_status status;
    if (page) {
        status=doe_dx_buffer_retain(page);
        if (status!=DOE_DX_OK) return doe_dx_fail(status,0,"upload_slice.retain",error);
    } else {
        status=doe_dx_upload_acquire(context,size,&page,error);
        if (status!=DOE_DX_OK) return status;
        page->upload_slices=true; page->upload_used=0;
    }
    /* Only this append API bypasses whole-allocation CPU exclusion. Existing
     * consumers can read earlier disjoint ranges while this new suffix is written.
     * Failed snapshots do not publish the cursor or caller's output. */
    status=doe_dx_native_write_upload_slice(page->native,offset,bytes,size,error);
    if (status==DOE_DX_OK) {
        page->upload_used=offset+size;
        *out=(doe_dx_upload_slice){page,offset,size};
    } else doe_dx_buffer_release(page);
    return doe_dx_context_result(context,status,error);
}
