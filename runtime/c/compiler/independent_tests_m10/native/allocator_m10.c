/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdlib.h>
#include <stdatomic.h>
_Thread_local unsigned native_inside;
atomic_uint native_allocations;
#ifdef WRAP_ALLOCATORS
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);void __real_free(void *);
void *__real_aligned_alloc(size_t,size_t);int __real_posix_memalign(void **,size_t,size_t);
static void hit(void){if(native_inside)(void)atomic_fetch_add(&native_allocations,1u);}
void *__wrap_malloc(size_t n){hit();return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t z){hit();return __real_calloc(n,z);}
void *__wrap_realloc(void *p,size_t n){hit();return __real_realloc(p,n);}
void __wrap_free(void *p){hit();__real_free(p);}
void *__wrap_aligned_alloc(size_t a,size_t n){hit();return __real_aligned_alloc(a,n);}
int __wrap_posix_memalign(void **p,size_t a,size_t n){hit();return __real_posix_memalign(p,a,n);}
#endif

int native_allocator_control(void){
#ifdef WRAP_ALLOCATORS
 static void *volatile probe;atomic_store(&native_allocations,0u);native_inside=1u;probe=malloc(16u);if(!probe){native_inside=0u;return 1;}free(probe);probe=NULL;native_inside=0u;unsigned count=atomic_load(&native_allocations);atomic_store(&native_allocations,0u);return count!=2u;
#else
 return 0;
#endif
}
