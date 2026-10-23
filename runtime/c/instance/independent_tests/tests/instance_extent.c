/* SPDX-License-Identifier: MIT
 * Supplemental trace extent observation, not a private layout overlay. */
#define _POSIX_C_SOURCE 200112L
#include "doe_instance.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static size_t extent;
static void *allocate(void *s, const void *v, size_t n, size_t a) {
    (void)s; (void)v; void *p = NULL;
    if (a < sizeof(void *)) a = sizeof(void *);
    return posix_memalign(&p, a, n) == 0 ? p : NULL;
}
static void deallocate(void *s, const void *v, void *p, size_t n, size_t a) {
    (void)s; (void)v; (void)a; assert(n == extent); free(p);
}
static void observed(void *s, doe_instance *p, size_t n, size_t a) {
    (void)s; (void)p; assert(n > 0 && a > 0); extent = n;
}
int main(void) {
    doe_instance_config config = {0}; doe_instance *instance = NULL;
    config.abi_version = DOE_INSTANCE_ABI_VERSION;
    config.object_allocator.allocate = allocate;
    config.object_allocator.deallocate = deallocate;
    config.wait_allocator = config.object_allocator;
    config.allocated = observed;
    assert(doe_instance_create(&config, &instance) == DOE_INSTANCE_OK);
    doe_instance_release(instance);
    printf("%zu\n", extent);
}
