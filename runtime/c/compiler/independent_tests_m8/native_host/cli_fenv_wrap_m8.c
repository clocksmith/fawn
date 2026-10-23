/* SPDX-License-Identifier: MIT
 * Additional CLI-only fenv hooks; unchanged historical ownership hooks coexist. */
#define _POSIX_C_SOURCE 200809L
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int __real_fegetenv(fenv_t *p); int __real_feholdexcept(fenv_t *p);
int __real_fesetenv(const fenv_t *p); int __real_feupdateenv(const fenv_t *p);
int __real_fesetround(int mode);
int __wrap_fegetenv(fenv_t *p); int __wrap_feholdexcept(fenv_t *p);
int __wrap_fesetenv(const fenv_t *p); int __wrap_feupdateenv(const fenv_t *p);
int __wrap_fesetround(int mode);
static unsigned counts[5];
static const char *const names[5]={"fegetenv","feholdexcept","fesetenv","feupdateenv","fesetround"};
static int fault(unsigned which) {
    unsigned number=++counts[which];
    const char *operation=getenv("M8_ENV_FAULT_OPERATION"),*call=getenv("M8_ENV_FAULT_CALL");
    fprintf(stderr,"M8_ENV_CALL %s %u\n",names[which],number);
    if (operation!=NULL && call!=NULL && strcmp(operation,names[which])==0 && number==(unsigned)strtoul(call,NULL,10)) {
        fprintf(stderr,"M8_ENV_FAULT %s %u\n",names[which],number);
        return 1;
    }
    return 0;
}
int __wrap_fegetenv(fenv_t *p) { return fault(0u)?1:__real_fegetenv(p); }
int __wrap_feholdexcept(fenv_t *p) { return fault(1u)?1:__real_feholdexcept(p); }
int __wrap_fesetenv(const fenv_t *p) { return fault(2u)?1:__real_fesetenv(p); }
int __wrap_feupdateenv(const fenv_t *p) { return fault(3u)?1:__real_feupdateenv(p); }
int __wrap_fesetround(int mode) { return fault(4u)?1:__real_fesetround(mode); }
