/* SPDX-License-Identifier: MIT */
#include "doe_metal.h"
#include <stdio.h>

int main(void) {
    doe_mtl_context *context = NULL;
    doe_mtl_error error = {0};
    doe_mtl_status status = doe_mtl_context_create(&context, &error);
    if (status != DOE_MTL_OK) {
        printf("Metal probe: %s (%s); no device or execution claimed\n",
            doe_mtl_status_name(status), error.operation != NULL ? error.operation : "unknown");
#if defined(__APPLE__) && defined(__MACH__)
        return 1;
#else
        return status == DOE_MTL_UNSUPPORTED && context == NULL ? 0 : 1;
#endif
    }
    doe_mtl_caps caps;
    if (doe_mtl_context_caps(context, &caps) != DOE_MTL_OK) return 1;
    printf("Metal device: %s, max buffer %llu; discovery only\n", caps.device_name,
        (unsigned long long)caps.max_buffer_length);
    doe_mtl_context_release(context);
    return 0;
}
