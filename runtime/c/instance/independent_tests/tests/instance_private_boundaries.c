/* SPDX-License-Identifier: MIT
 * Disclosed private binding of presealed C08, after GCC-core handoff. */
#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void *allocate(void *s,const void *v,size_t n,size_t a) {
    assert(!s&&!v);void *p=NULL;assert(posix_memalign(&p,a<sizeof(void*)?sizeof(void*):a,n)==0);return p;
}
static void deallocate(void *s,const void *v,void *p,size_t n,size_t a) {assert(!s&&!v&&p&&n&&a);puts("FREE");free(p);}
static void label(void *u,doe_instance *p){assert(!u&&p);puts("LABEL");}
int main(int argc,char **argv){
    assert(argc==2);setvbuf(stdout,NULL,_IONBF,0);
    doe_instance_config c={.abi_version=1,.object_allocator={NULL,NULL,allocate,deallocate},.wait_allocator={NULL,NULL,allocate,deallocate},.remove_label=label};
    doe_instance *p=NULL;assert(doe_instance_create(&c,&p)==DOE_INSTANCE_OK&&p);
    if(!strcmp(argv[1],"valid")){doe_instance_retain(p);doe_instance_retain(p);doe_instance_release(p);doe_instance_undo_retain(p);doe_instance_release(p);puts("PASS bounded count control");return 0;}
    if(!strcmp(argv[1],"maximum")){doe_instance_retain(p);doe_instance_retain(p);doe_instance_retain(p);}
    else if(!strcmp(argv[1],"production-maximum")){atomic_store_explicit(&p->references,UINT32_MAX,memory_order_relaxed);doe_instance_retain(p);}
    else if(!strcmp(argv[1],"undo-one"))doe_instance_undo_retain(p);
    else{
        atomic_store_explicit(&p->references,0,memory_order_relaxed);
        if(!strcmp(argv[1],"retain-zero"))doe_instance_retain(p);
        else if(!strcmp(argv[1],"release-zero"))doe_instance_release(p);
        else{assert(!strcmp(argv[1],"undo-zero"));doe_instance_undo_retain(p);}
    }
    fputs("UNEXPECTED RETURN\n",stderr);return 77;
}
