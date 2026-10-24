/* SPDX-License-Identifier: MIT */
#include "doe_dispatch_guard.h"
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    const uint32_t limits[3] = {1,65535,UINT32_MAX};
    char output[DOE_DISPATCH_GUARD_SOURCE_CAPACITY];
    memset(output,0xa5,sizeof(output));
    size_t size = 123;
    assert(doe_dispatch_guard_source(limits,output,sizeof(output),&size) == DOE_DISPATCH_GUARD_SOURCE_OK);
    assert(strlen(output) == size);
    assert(strstr(output,"counts[0] > 1u || counts[1] > 65535u || counts[2] > 4294967295u"));
    assert(strstr(output,"counts[0] = 0u;\n        counts[1] = 0u;\n        counts[2] = 0u;"));
    char prior[sizeof(output)];
    memcpy(prior,output,sizeof(output));
    size_t before = size;
    assert(doe_dispatch_guard_source(limits,output,size,&size) == DOE_DISPATCH_GUARD_SOURCE_TOO_SMALL);
    assert(size == before && !memcmp(prior,output,sizeof(output)));
    assert(doe_dispatch_guard_source(limits,output,before+1,&size) == DOE_DISPATCH_GUARD_SOURCE_OK);
    const uint32_t zero[3] = {1,0,1};
    assert(doe_dispatch_guard_source(zero,output,sizeof(output),&size) == DOE_DISPATCH_GUARD_SOURCE_INVALID_ARGUMENT);
    assert(size == before && !memcmp(prior,output,sizeof(output)));
    assert(doe_dispatch_guard_source(NULL,output,sizeof(output),&size) == DOE_DISPATCH_GUARD_SOURCE_INVALID_ARGUMENT);
    assert(doe_dispatch_guard_source(limits,NULL,sizeof(output),&size) == DOE_DISPATCH_GUARD_SOURCE_INVALID_ARGUMENT);
    assert(doe_dispatch_guard_source(limits,output,sizeof(output),NULL) == DOE_DISPATCH_GUARD_SOURCE_INVALID_ARGUMENT);
    puts("Shared indirect guard source control passed: independent XYZ bounds, complete zeroing, untouched failed outputs.");
    return 0;
}
