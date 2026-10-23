/* SPDX-License-Identifier: MIT
 * Isolated-process platform injection, not an alternative completion owner. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static unsigned cleanup_count;
static int injected;
static int mode(const char *expected)
{
    const char *selected = getenv("DOE_FUTURES_FAULT");
    return selected != NULL && strcmp(selected, expected) == 0;
}
static void symbol(const char *name, void *out, size_t size)
{
    void *address = dlsym(RTLD_NEXT, name);
    if (address == NULL || size != sizeof(address)) abort();
    memcpy(out, &address, size);
}
unsigned doe_futures_fault_cleanup_count(void) { return cleanup_count; }
int pthread_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attributes)
{
    int (*real)(pthread_cond_t *, const pthread_condattr_t *);
    if (!injected && (mode("setup") || mode("init-cleanup"))) {
        injected = 1;
        return EAGAIN;
    }
    symbol("pthread_cond_init", &real, sizeof(real));
    return real(condition, attributes);
}
int pthread_condattr_destroy(pthread_condattr_t *attributes)
{
    int (*real)(pthread_condattr_t *);
    if (mode("init-cleanup") && injected) return EBUSY;
    symbol("pthread_condattr_destroy", &real, sizeof(real));
    cleanup_count++;
    return real(attributes);
}
int pthread_cond_destroy(pthread_cond_t *condition)
{
    int (*real)(pthread_cond_t *);
    if (mode("deinit-cleanup")) return EBUSY;
    symbol("pthread_cond_destroy", &real, sizeof(real));
    cleanup_count++;
    return real(condition);
}
int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    int (*real)(pthread_mutex_t *);
    symbol("pthread_mutex_destroy", &real, sizeof(real));
    cleanup_count++;
    return real(mutex);
}
