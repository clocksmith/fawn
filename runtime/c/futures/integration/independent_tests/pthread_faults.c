/* SPDX-License-Identifier: MIT
 * Independent test-only platform failure injection. No queue/refcount models. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int enabled;
void eval_set_enabled(int value) { enabled = value; }

static int failure(const char *point)
{
    const char *requested = getenv("EVAL_PTHREAD_FAILURE");
    if (!enabled || requested == NULL) return 0;
    while (*requested != '\0') {
        const char *comma = strchr(requested, ',');
        size_t length = comma == NULL ? strlen(requested) : (size_t)(comma - requested);
        if (length == strlen(point) && memcmp(requested, point, length) == 0) return 1;
        if (comma == NULL) break;
        requested = comma + 1;
    }
    return 0;
}

static int report(const char *operation, int result)
{
    if (enabled) fprintf(stderr, "EVAL pthread %s result=%d\n", operation, result);
    return result;
}

#define LOAD_REAL(name) \
    __typeof__(&name) real; \
    do { void *symbol = dlsym(RTLD_NEXT, #name); \
         if (symbol == NULL) _Exit(95); \
         _Static_assert(sizeof(real) == sizeof(symbol), "function address size"); \
         memcpy(&real, &symbol, sizeof(real)); } while (0)

int pthread_mutex_init(pthread_mutex_t *object, const pthread_mutexattr_t *attr)
{
    LOAD_REAL(pthread_mutex_init);
    return report("mutex-init", failure("mutex-init") ? EAGAIN : real(object, attr));
}
int pthread_condattr_init(pthread_condattr_t *object)
{
    LOAD_REAL(pthread_condattr_init);
    return report("attr-init", failure("attr-init") ? EAGAIN : real(object));
}
int pthread_condattr_setclock(pthread_condattr_t *object, clockid_t clock)
{
    LOAD_REAL(pthread_condattr_setclock);
    return report("attr-clock", failure("attr-clock") ? EINVAL : real(object, clock));
}
int pthread_cond_init(pthread_cond_t *object, const pthread_condattr_t *attr)
{
    LOAD_REAL(pthread_cond_init);
    return report("cond-init", failure("cond-init") ? EAGAIN : real(object, attr));
}
int pthread_condattr_destroy(pthread_condattr_t *object)
{
    LOAD_REAL(pthread_condattr_destroy);
    return report("attr-destroy", failure("attr-destroy") ? EBUSY : real(object));
}
int pthread_cond_destroy(pthread_cond_t *object)
{
    LOAD_REAL(pthread_cond_destroy);
    return report("cond-destroy", failure("cond-destroy") ? EBUSY : real(object));
}
int pthread_mutex_destroy(pthread_mutex_t *object)
{
    LOAD_REAL(pthread_mutex_destroy);
    return report("mutex-destroy", failure("mutex-destroy") ? EBUSY : real(object));
}
