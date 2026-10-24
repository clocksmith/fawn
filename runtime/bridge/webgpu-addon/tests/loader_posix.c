/* SPDX-License-Identifier: MIT
 * Actual host loader compatibility only; no provider procedure/device is invoked. */
#include "../doe_napi_library.h"
#include <assert.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    assert(argc == 3);
    doe_library_error error = {0};
    assert(!doe_library_open("",0,&error) && !strcmp(error.operation,"provider.path"));
    const char embedded[] = {'a',0,'b'};
    assert(!doe_library_open(embedded,sizeof(embedded),&error) && !strcmp(error.operation,"provider.path"));
    const char missing[] = "/no-such-doe-loader-control/provider";
    assert(!doe_library_open(missing,sizeof(missing)-1,&error));
    assert(!strcmp(error.operation,"dlopen.provider") && error.message[0]);
    for (int i = 1; i <= 2; ++i) {
        memset(&error,0xa5,sizeof(error));
        doe_library_error untouched = error;
        void *library = doe_library_open(argv[i],strlen(argv[i]),&error);
        assert(library && !memcmp(&error,&untouched,sizeof(error)));
        assert(dlsym(library,"wgpuCreateInstance"));
        bool zig = dlsym(library,"doeWgpuDropinBuildInfoSchemaVersion") != NULL;
        assert(zig == (i == 2));
        assert(dlclose(library) == 0);
    }
    puts("PASS actual POSIX loader: selected C and retained Zig provider symbols, embedded-NUL/missing-file diagnostics, unchanged success outputs; no device/GPU");
}
