/* SPDX-License-Identifier: MIT
 * Actual Windows GPU copies with a complete independent byte-zero oracle. */
#include "doe_d3d12.h"
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#error This fixture requires the real Windows D3D12 backend.
#endif

static void require(doe_dx_status status, const doe_dx_error *error)
{
    if (status == DOE_DX_OK) return;
    fprintf(stderr,"D3D12 zero upload status=%u native=%ld operation=%s\n",
        (unsigned)status,(long)error->native_code,error->operation ? error->operation : "none");
    exit(1); /* No success claim; unresolved native obligations remain process-owned. */
}

int main(void)
{
    doe_dx_context *context = NULL;
    doe_dx_error error = {0};
    const doe_dx_options options = {.adapter_selection=DOE_DX_SELECT_HARDWARE};
    require(doe_dx_context_create(&options,&context,&error),&error);
    doe_dx_caps caps;
    require(doe_dx_context_caps(context,&caps),&error);
    if (caps.software_adapter) return 2;
    printf("adapter=%s vendor=%u device=%u software=%u\n",caps.name,caps.vendor_id,
        caps.device_id,(unsigned)caps.software_adapter);
    const uint64_t sizes[] = {16,256,1024u*1024u,1024u*1024u+4u};
    for (size_t i=0;i<sizeof(sizes)/sizeof(*sizes);++i) {
        uint64_t size = sizes[i];
        doe_dx_buffer *zero = NULL, *destination = NULL, *readback = NULL;
        doe_dx_commands *commands = NULL;
        require(doe_dx_zero_upload_acquire(context,size,&zero,&error),&error);
        if (doe_dx_buffer_size(zero) < size) return 3;
        unsigned char poison = 0xa5;
        if (doe_dx_buffer_write(zero,0,&poison,1,&error) != DOE_DX_UNSUPPORTED) return 4;
        require(doe_dx_buffer_create(context,size,DOE_DX_DEFAULT,&destination,&error),&error);
        require(doe_dx_buffer_create(context,size,DOE_DX_READBACK,&readback,&error),&error);
        require(doe_dx_commands_create(context,&commands,&error),&error);
        require(doe_dx_commands_copy(commands,zero,0,destination,0,size,&error),&error);
        require(doe_dx_commands_copy(commands,destination,0,readback,0,size,&error),&error);
        doe_dx_buffer_release(zero);
        doe_dx_buffer_release(destination);
        require(doe_dx_commands_submit(commands,&error),&error);
        doe_dx_commands_release(commands);
        /* Replacing the cached source cannot drop the prior GPU lease. */
        if (i+1<sizeof(sizes)/sizeof(*sizes)) {
            doe_dx_buffer *replacement = NULL;
            require(doe_dx_zero_upload_acquire(context,sizes[i+1],&replacement,&error),&error);
            doe_dx_buffer_release(replacement);
        }
        doe_dx_status status = DOE_DX_PENDING;
        for (unsigned attempt=0;attempt<300 && status==DOE_DX_PENDING;++attempt)
            status = doe_dx_context_poll(context,100,&error);
        require(status,&error);
        unsigned char *bytes = malloc((size_t)size);
        if (!bytes) return 5;
        require(doe_dx_buffer_read(readback,0,bytes,(size_t)size,&error),&error);
        for (size_t index=0;index<(size_t)size;++index)
            if (bytes[index] != 0) return 6;
        free(bytes);
        doe_dx_buffer_release(readback);
        printf("PASS complete GPU zero copy/readback bytes=%llu\n",(unsigned long long)size);
    }
    require(doe_dx_context_destroy(context),&error);
    puts("PASS physical D3D12 immutable zero upload: denied writes, growth with pending GPU leases, complete zeros including256-byte padding, bounded cache and uncached large source; not qualification");
    return 0;
}
