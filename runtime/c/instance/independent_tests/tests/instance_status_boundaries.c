/* SPDX-License-Identifier: MIT
 * Post-handoff binding of presealed C07/C12/C13 error/order observations. */
#define _POSIX_C_SOURCE 200809L
#include "doe_instance.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *mode;
static unsigned wait_status;
static void *node_memory;
static doe_future_completion *node;
static doe_instance *instance;
static unsigned order[3], count;
static int is(const char *s){return !strcmp(mode,s);}
static void *allocate(void *s,const void *v,size_t n,size_t a){assert(!s&&!v);void *p=NULL;assert(posix_memalign(&p,a<sizeof(void*)?sizeof(void*):a,n)==0);return p;}
static void deallocate(void *s,const void *v,void *p,size_t n,size_t a){assert(!s&&!v&&p&&n&&a);puts("FREE");free(p);}
static void label(void *u,doe_instance *p){assert(!u&&p);puts("LABEL");}
doe_future_status __real_doe_future_queue_requirements(uint32_t,doe_future_storage_requirements*);
doe_future_status __wrap_doe_future_queue_requirements(uint32_t v,doe_future_storage_requirements*r){if(is("requirements"))return DOE_FUTURE_SYSTEM_ERROR;return __real_doe_future_queue_requirements(v,r);}
doe_future_status __real_doe_future_process_events(doe_future_queue*);
doe_future_status __wrap_doe_future_process_events(doe_future_queue*q){doe_future_status s=__real_doe_future_process_events(q);assert(s==DOE_FUTURE_OK);return is("process-error")?DOE_FUTURE_SYSTEM_ERROR:s;}
doe_future_status __real_doe_future_wait_any(doe_future_queue*,doe_future_wait_info*,size_t,uint64_t);
doe_future_status __wrap_doe_future_wait_any(doe_future_queue*q,doe_future_wait_info*w,size_t n,uint64_t t){doe_future_status s=__real_doe_future_wait_any(q,w,n,t);assert(s==DOE_FUTURE_OK);return is("wait-map")?(doe_future_status)wait_status:s;}
static void active_callback(doe_future_completion *p,void*u){assert(!u);assert(doe_future_finish(p)==DOE_FUTURE_OK);assert(doe_future_completion_deinit(p)==DOE_FUTURE_OK);free(node_memory);node_memory=NULL;puts("CALLBACK_RETIRED");doe_instance_release(instance);}
static void ordered_callback(doe_future_completion*p,void*u){assert(count<3);order[count++]=*(unsigned*)u;assert(doe_future_finish(p)==DOE_FUTURE_OK);}
int main(int argc,char**argv){
    assert(argc==2);mode=argv[1];setvbuf(stdout,NULL,_IONBF,0);
    doe_instance_config c={.abi_version=1,.object_allocator={NULL,NULL,allocate,deallocate},.wait_allocator={NULL,NULL,allocate,deallocate},.remove_label=label};
    assert(doe_instance_create(&c,&instance)==DOE_INSTANCE_OK&&instance);
    doe_instance_queue_lease l=doe_instance_borrow_queue(instance);
    if(is("process-error"))doe_instance_process_events(instance);
    else if(is("active")){
        doe_future_storage_requirements r;uint64_t id;assert(doe_future_completion_requirements(1,&r)==DOE_FUTURE_OK);
        node_memory=allocate(NULL,NULL,r.size,r.alignment);
        assert(doe_future_completion_init(1,node_memory,r.size,DOE_FUTURE_MODE_ALLOW_PROCESS_EVENTS,active_callback,NULL,&node)==DOE_FUTURE_OK);
        assert(doe_future_new_id(l.queue,&id)==DOE_FUTURE_OK);assert(doe_future_register(l.queue,node,id)==DOE_FUTURE_OK);assert(doe_future_mark_ready(node)==DOE_FUTURE_OK);
        /* Intentionally no full-call instance lease, to exercise real BUSY finalization. */
        (void)doe_future_process_events(l.queue);
    }else if(is("wait-map")){
        uint64_t id;assert(doe_future_new_id(l.queue,&id)==DOE_FUTURE_OK);
        for(wait_status=0;wait_status<=13;++wait_status){WGPUFutureWaitInfo w={{id},0x44};WGPUWaitStatus expected=wait_status==0?WGPUWaitStatus_Success:wait_status==1?WGPUWaitStatus_TimedOut:WGPUWaitStatus_Error;assert(doe_instance_wait_any(instance,1,&w,0)==expected&&w.completed==1);}
        doe_instance_release(instance);puts("PASS all wait status mapping and exact real-queue output copy");return 0;
    }else if(is("wait-order")){
        doe_future_storage_requirements r;doe_future_completion*nodes[3];void*storage[3];uint64_t ids[3];unsigned labels[3]={1,2,3};assert(doe_future_completion_requirements(1,&r)==DOE_FUTURE_OK);
        for(unsigned i=0;i<3;++i){storage[i]=allocate(NULL,NULL,r.size,r.alignment);assert(doe_future_completion_init(1,storage[i],r.size,DOE_FUTURE_MODE_WAIT_ANY_ONLY,ordered_callback,&labels[i],&nodes[i])==DOE_FUTURE_OK);assert(doe_future_new_id(l.queue,&ids[i])==DOE_FUTURE_OK);assert(doe_future_register(l.queue,nodes[i],ids[i])==DOE_FUTURE_OK);}
        assert(doe_future_mark_ready(nodes[2])==DOE_FUTURE_OK);assert(doe_future_mark_ready(nodes[0])==DOE_FUTURE_OK);assert(doe_future_mark_ready(nodes[1])==DOE_FUTURE_OK);
        WGPUFutureWaitInfo w[3]={{{ids[2]},11},{{ids[0]},22},{{ids[1]},33}};assert(doe_instance_wait_any(instance,3,w,0)==WGPUWaitStatus_Success);assert(count==3&&order[0]==1&&order[1]==2&&order[2]==3);
        for(unsigned i=0;i<3;++i){assert(w[i].completed==1);assert(doe_future_completion_deinit(nodes[i])==DOE_FUTURE_OK);free(storage[i]);}
        doe_instance_release(instance);puts("PASS wait records C,A,B still dispatch A,B,C");return 0;
    }
    fputs("UNEXPECTED RETURN\n",stderr);return 77;
}
