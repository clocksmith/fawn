/* SPDX-License-Identifier: MIT
 * Independent callback-owner evaluator, authored before candidate inspection. */
#define _POSIX_C_SOURCE 200809L
#include "doe_callbacks.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

_Static_assert(sizeof(doe_callback_allocator)==32 && _Alignof(doe_callback_allocator)==8,"allocator ABI");
_Static_assert(offsetof(doe_callback_allocator,state)==0 && offsetof(doe_callback_allocator,vtable)==8 && offsetof(doe_callback_allocator,allocate)==16 && offsetof(doe_callback_allocator,deallocate)==24,"allocator offsets");
_Static_assert(sizeof(doe_callback_owner)==32 && _Alignof(doe_callback_owner)==8,"owner ABI");
_Static_assert(offsetof(doe_callback_owner,queue)==0 && offsetof(doe_callback_owner,userdata)==8 && offsetof(doe_callback_owner,retain)==16 && offsetof(doe_callback_owner,release)==24,"owner offsets");
_Static_assert(sizeof(doe_callback_request)==88 && _Alignof(doe_callback_request)==8,"request ABI");
_Static_assert(offsetof(doe_callback_request,abi_version)==0 && offsetof(doe_callback_request,mode)==4 && offsetof(doe_callback_request,owner)==8 && offsetof(doe_callback_request,allocator)==16 && offsetof(doe_callback_request,source_context)==48 && offsetof(doe_callback_request,context_size)==56 && offsetof(doe_callback_request,context_alignment)==64 && offsetof(doe_callback_request,copy)==72 && offsetof(doe_callback_request,deliver)==80,"request offsets");
_Static_assert(DOE_CALLBACK_OK==0 && DOE_CALLBACK_INVALID_ARGUMENT==1 && DOE_CALLBACK_UNSUPPORTED_VERSION==2 && DOE_CALLBACK_UNSUPPORTED_MODE==3 && DOE_CALLBACK_INSTANCE_UNAVAILABLE==4 && DOE_CALLBACK_OUT_OF_MEMORY==5 && DOE_CALLBACK_LAYOUT_OVERFLOW==6,"statuses");

typedef struct Owner Owner;
typedef struct Stats {
    pthread_mutex_t lock;
    unsigned allocs, frees, copies, delivered, published, retained, released, destroyed;
    int fail_alloc, misalign, drop_caller, inspect_wait, nested_ownerless;
    void *allocation, *raw;
    size_t size, alignment;
    const void *vtable;
    Owner *owner;
    doe_callback_event *token;
    uint64_t id, nested_id;
    char trace[512]; size_t trace_len;
    int borrowed;
} Stats;
struct Owner { _Atomic unsigned refs; doe_future_queue *queue; void *storage; Stats *stats; };
typedef struct Context { Stats *stats; char inline_bytes[48]; int *borrowed; } Context;
static const int allocator_identity=119;
static const char text_value[]="owned inline diagnostic remains alive";
static _Thread_local Stats *active_trace;
static const char *fault;
static int post_fault;
static doe_future_status injected_status=DOE_FUTURE_SYSTEM_ERROR;
static void log_event(Stats *s,char c) { assert(pthread_mutex_lock(&s->lock)==0); assert(s->trace_len+1<sizeof(s->trace)); s->trace[s->trace_len++]=c; s->trace[s->trace_len]=0; assert(pthread_mutex_unlock(&s->lock)==0); }
static void init_stats(Stats *s) { memset(s,0,sizeof(*s)); assert(pthread_mutex_init(&s->lock,NULL)==0); s->borrowed=17; s->vtable=&allocator_identity; }
static void end_stats(Stats *s) { assert(pthread_mutex_destroy(&s->lock)==0); }
static int fail(const char *name) { return fault && strcmp(fault,name)==0; }

/* Real accepted queue boundaries remain responsible for all policy. Wrappers
 * only record exact retirement ordering and inject stated terminal statuses. */
doe_future_status __real_doe_future_completion_requirements(uint32_t,doe_future_storage_requirements*);
doe_future_status __wrap_doe_future_completion_requirements(uint32_t v,doe_future_storage_requirements *r) { if(fail("requirements"))return injected_status; return __real_doe_future_completion_requirements(v,r); }
doe_future_status __real_doe_future_new_id(doe_future_queue*,uint64_t*);
doe_future_status __wrap_doe_future_new_id(doe_future_queue*q,uint64_t*i) { if(active_trace)log_event(active_trace,'N'); if(fail("new_id"))return DOE_FUTURE_EXHAUSTED; return __real_doe_future_new_id(q,i); }
doe_future_status __real_doe_future_legacy_id(uint64_t*);
doe_future_status __wrap_doe_future_legacy_id(uint64_t*i) { if(fail("legacy_id"))return DOE_FUTURE_EXHAUSTED; return __real_doe_future_legacy_id(i); }
doe_future_status __real_doe_future_completion_init(uint32_t,void*,size_t,doe_future_mode,doe_future_dispatch,void*,doe_future_completion**);
doe_future_status __wrap_doe_future_completion_init(uint32_t v,void*p,size_t z,doe_future_mode m,doe_future_dispatch d,void*u,doe_future_completion **o) { if(active_trace)log_event(active_trace,'I'); if(fail("init"))return injected_status; return __real_doe_future_completion_init(v,p,z,m,d,u,o); }
doe_future_status __real_doe_future_register(doe_future_queue*,doe_future_completion*,uint64_t);
doe_future_status __wrap_doe_future_register(doe_future_queue*q,doe_future_completion*c,uint64_t id) { if(active_trace)log_event(active_trace,'G'); if(fail("register"))return injected_status; return __real_doe_future_register(q,c,id); }
doe_future_status __real_doe_future_mark_ready(doe_future_completion*);
doe_future_status __wrap_doe_future_mark_ready(doe_future_completion*c) { if(fail("mark_ready")&&!post_fault)return injected_status; doe_future_status st=__real_doe_future_mark_ready(c); if(fail("mark_ready")&&post_fault)return injected_status; return st; }
doe_future_status __real_doe_future_finish(doe_future_completion*);
doe_future_status __wrap_doe_future_finish(doe_future_completion*c) { if(active_trace)log_event(active_trace,'F'); if(fail("finish")&&!post_fault)return injected_status; doe_future_status st=__real_doe_future_finish(c); if(fail("finish")&&post_fault)return injected_status; return st; }
doe_future_status __real_doe_future_completion_deinit(doe_future_completion*);
doe_future_status __wrap_doe_future_completion_deinit(doe_future_completion*c) { if(active_trace)log_event(active_trace,'D'); if(fail("deinit"))return injected_status; return __real_doe_future_completion_deinit(c); }

static void retain(void *p) { Owner *o=p; atomic_fetch_add(&o->refs,1); o->stats->retained++; log_event(o->stats,'R'); }
static void release(void *p) { Owner *o=p; Stats *s=o->stats; s->released++; log_event(s,'L'); if(atomic_fetch_sub(&o->refs,1)==1) { assert(doe_future_queue_deinit(o->queue)==DOE_FUTURE_OK); free(o->storage); s->destroyed++; log_event(s,'Q'); free(o); } }
static Owner *new_owner(Stats *s) { doe_future_storage_requirements r; assert(doe_future_queue_requirements(1,&r)==DOE_FUTURE_OK); Owner *o=calloc(1,sizeof(*o)); assert(o); assert(posix_memalign(&o->storage,r.alignment<sizeof(void*)?sizeof(void*):r.alignment,r.size)==0); assert(doe_future_queue_init(1,o->storage,r.size,&o->queue)==DOE_FUTURE_OK); atomic_init(&o->refs,1); o->stats=s; s->owner=o; return o; }
static doe_callback_owner binding(Owner *o) { doe_callback_owner b={o->queue,o,retain,release}; return b; }
static void *allocate(void *state,const void *vtable,size_t size,size_t alignment) { Stats*s=state; assert(vtable==s->vtable); s->allocs++; log_event(s,'A'); if(s->fail_alloc)return NULL; assert(!s->allocation); assert(posix_memalign(&s->raw,alignment<sizeof(void*)?sizeof(void*):alignment,size+1)==0); s->allocation=s->misalign?(char*)s->raw+1:s->raw; s->size=size; s->alignment=alignment; return s->allocation; }
static void deallocate(void *state,const void *vtable,void*p,size_t z,size_t a) { Stats*s=state; assert(vtable==s->vtable && p==s->allocation && z==s->size && a==s->alignment); assert(s->delivered==1); s->frees++; log_event(s,'M'); memset(p,0xa5,z); free(s->raw); s->allocation=NULL; }
static void copy_context(void*d,const void*p) { const Context*c=p; Stats*s=c->stats; assert((uintptr_t)d%_Alignof(Context)==0); s->copies++; log_event(s,'C'); *(Context*)d=*c; }
static doe_callback_request request(Stats*s,doe_callback_owner *o,Context*c,unsigned mode);
static void *other_wait(void*p) { Stats*s=p; doe_future_wait_info w={s->id,41}; assert(doe_future_wait_any(s->owner->queue,&w,1,0)==DOE_FUTURE_TIMED_OUT && w.completed==0); return NULL; }
static void deliver(void*p) { Context*c=p; Stats*s=c->stats; assert(memcmp(c->inline_bytes,text_value,sizeof(text_value))==0); assert(c->borrowed==&s->borrowed && *c->borrowed==s->borrowed); s->delivered++; log_event(s,'B'); if(s->inspect_wait) { doe_future_wait_info w={s->id,42}; assert(doe_future_wait_any(s->owner->queue,&w,1,UINT64_MAX)==DOE_FUTURE_OK && w.completed==1); pthread_t t; assert(pthread_create(&t,NULL,other_wait,s)==0); assert(pthread_join(t,NULL)==0); assert(doe_future_queue_deinit(s->owner->queue)==DOE_FUTURE_BUSY); }
    if(s->nested_ownerless) { Stats n; init_stats(&n); Context child={&n,{0},&n.borrowed}; memcpy(child.inline_bytes,text_value,sizeof(text_value)); doe_callback_request q=request(&n,NULL,&child,3); assert(doe_callback_ready(&q,&s->nested_id)==DOE_CALLBACK_OK); assert(n.delivered==1 && n.allocs==0); end_stats(&n); }
    if(s->drop_caller)release(s->owner);
    c->inline_bytes[0]='!';
}
static Context context(Stats*s) { Context c={s,{0},&s->borrowed}; memcpy(c.inline_bytes,text_value,sizeof(text_value)); return c; }
static doe_callback_request request(Stats*s,doe_callback_owner *o,Context*c,unsigned mode) { doe_callback_request r={1,mode,o,{s,&allocator_identity,allocate,deallocate},c,sizeof(*c),_Alignof(Context),copy_context,deliver}; return r; }
static void hold(doe_callback_event*e,void*p) { Stats*s=p; s->published++; s->token=e; log_event(s,'P'); }
static void immediate(doe_callback_event*e,void*p) { Stats*s=p; s->published++; log_event(s,'P'); doe_callback_mark_ready(e); assert(s->frees==1); if(s->drop_caller)assert(s->destroyed==1); }
static void *worker(void*p) { Stats*s=p; active_trace=s; doe_callback_event*e=s->token; s->token=NULL; doe_callback_mark_ready(e); return NULL; }
static void joined(doe_callback_event*e,void*p) { Stats*s=p; s->published++; s->token=e; log_event(s,'P'); pthread_t t; assert(pthread_create(&t,NULL,worker,s)==0); assert(pthread_join(t,NULL)==0); assert(s->frees==1 && s->destroyed==1); }
static void reject(doe_callback_request*r,doe_callback_publisher pub,doe_callback_status expected,Stats*s) { uint64_t id=UINT64_C(0xbadbad); unsigned a=s->allocs; assert(doe_callback_publish(r,pub,s,&id)==expected && id==UINT64_C(0xbadbad)); assert(s->delivered==0 && s->copies==0 && s->published==0 && s->retained==0 && s->released==0); assert(s->allocs==a+(expected==DOE_CALLBACK_OUT_OF_MEMORY)); }
static void validation(void) { Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,1); uint64_t before,after; assert(doe_future_new_id(o->queue,&before)==DOE_FUTURE_OK); reject(NULL,hold,DOE_CALLBACK_INVALID_ARGUMENT,&s); assert(doe_callback_publish(NULL,NULL,NULL,NULL)==DOE_CALLBACK_INVALID_ARGUMENT); r.abi_version=9; r.mode=99; reject(&r,NULL,DOE_CALLBACK_UNSUPPORTED_VERSION,&s); r.abi_version=1; reject(&r,NULL,DOE_CALLBACK_UNSUPPORTED_MODE,&s); r.owner=NULL; r.mode=1; r.source_context=NULL; r.deliver=NULL; r.context_alignment=0; reject(&r,NULL,DOE_CALLBACK_INSTANCE_UNAVAILABLE,&s); r.mode=2; reject(&r,NULL,DOE_CALLBACK_INSTANCE_UNAVAILABLE,&s);
    for(unsigned i=0;i<11;i++) { r=request(&s,&b,&c,1); doe_callback_owner bad=b; r.owner=&bad; switch(i) { case 0:r.source_context=NULL;break;case 1:r.deliver=NULL;break;case 2:r.context_alignment=0;break;case 3:r.context_alignment=3;break;case 4:bad.queue=NULL;break;case 5:bad.retain=NULL;break;case 6:bad.release=NULL;break;case 7:r.allocator.allocate=NULL;break;case 8:r.allocator.deallocate=NULL;break;case 9:r.copy=NULL;break;case 10:break; } reject(&r,i==10?NULL:hold,DOE_CALLBACK_INVALID_ARGUMENT,&s); }
    r=request(&s,&b,&c,1); r.context_size=SIZE_MAX; reject(&r,hold,DOE_CALLBACK_LAYOUT_OVERFLOW,&s); r.context_size=SIZE_MAX-8; r.context_alignment=4096; reject(&r,hold,DOE_CALLBACK_LAYOUT_OVERFLOW,&s); r=request(&s,&b,&c,1); s.fail_alloc=1; reject(&r,hold,DOE_CALLBACK_OUT_OF_MEMORY,&s); assert(memcmp(c.inline_bytes,text_value,sizeof(text_value))==0 && atomic_load(&o->refs)==1); assert(doe_future_new_id(o->queue,&after)==DOE_FUTURE_OK && after==before+1); release(o); assert(s.destroyed==1); end_stats(&s); }
static void ownerless(void) { for(unsigned mode=0;mode<=3;mode+=3) { Stats s; init_stats(&s); s.nested_ownerless=1; Context c=context(&s); doe_callback_request r=request(&s,NULL,&c,mode); r.allocator.allocate=NULL; r.allocator.deallocate=NULL; r.context_size=SIZE_MAX; r.copy=NULL; assert(doe_callback_publish(&r,NULL,NULL,&s.id)==DOE_CALLBACK_OK); assert(s.id==s.nested_id+1 && s.id>>63); assert(s.delivered==1 && s.allocs==0 && s.copies==0 && s.published==0 && c.inline_bytes[0]=='!'); end_stats(&s); } }
static void modes(void) { for(unsigned mode=0;mode<4;mode++) { Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,mode); active_trace=&s; assert(doe_callback_publish(&r,hold,&s,&s.id)==DOE_CALLBACK_OK); active_trace=NULL; assert(strcmp(s.trace,"ANICRGP")==0); assert(s.allocs==1 && s.copies==1 && s.published==1 && s.delivered==0 && atomic_load(&o->refs)==2); memset(&c,0xcc,sizeof(c)); memset(&b,0xcc,sizeof(b)); memset(&r,0xcc,sizeof(r)); s.borrowed=81;
    doe_future_wait_info w[2]={{s.id,41},{s.id,42}}; assert(doe_future_wait_any(o->queue,w,2,1000)==DOE_FUTURE_TIMED_OUT && w[0].completed==0 && w[1].completed==0); active_trace=&s; doe_callback_mark_ready(s.token); s.token=NULL; assert(s.delivered==(unsigned)(mode==0||mode==3)); assert(doe_future_process_events(o->queue)==DOE_FUTURE_OK); assert(s.delivered==(unsigned)(mode!=1)); assert(doe_future_wait_any(o->queue,w,2,0)==DOE_FUTURE_OK && w[0].completed==1 && w[1].completed==1); assert(doe_future_wait_any(o->queue,w,2,0)==DOE_FUTURE_OK); active_trace=NULL; assert(s.delivered==1 && s.frees==1 && atomic_load(&o->refs)==1); assert(strstr(s.trace,"BFDML")!=NULL); release(o); end_stats(&s); } }
static void final_release(void) { for(unsigned mode=0;mode<4;mode++) { Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,mode); s.drop_caller=1; active_trace=&s; assert(doe_callback_ready(&r,&s.id)==DOE_CALLBACK_OK && s.id!=0); if(mode==1||mode==2) { retain(o); if(mode==1) { doe_future_wait_info w={s.id,9}; assert(doe_future_wait_any(o->queue,&w,1,0)==DOE_FUTURE_OK); } else assert(doe_future_process_events(o->queue)==DOE_FUTURE_OK); release(o); } active_trace=NULL; assert(s.delivered==1 && s.frees==1 && s.destroyed==1); assert(strstr(s.trace,"BLFDMLLQ")!=NULL); end_stats(&s); }
    for(int use_worker=0;use_worker<2;use_worker++) { Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,3); s.drop_caller=1; active_trace=&s; assert(doe_callback_publish(&r,use_worker?joined:immediate,&s,&s.id)==DOE_CALLBACK_OK && s.id!=0); active_trace=NULL; assert(s.destroyed==1 && s.frees==1); end_stats(&s); }
    Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,3); assert(doe_callback_publish(&r,hold,&s,&s.id)==DOE_CALLBACK_OK); release(o); pthread_t t; assert(pthread_create(&t,NULL,worker,&s)==0); assert(pthread_join(t,NULL)==0); assert(s.destroyed==1 && s.frees==1); end_stats(&s);
}
static void self_wait(void) { Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,2); s.inspect_wait=1; assert(doe_callback_ready(&r,&s.id)==DOE_CALLBACK_OK); active_trace=&s; assert(doe_future_process_events(o->queue)==DOE_FUTURE_OK); active_trace=NULL; assert(s.delivered==1); release(o); end_stats(&s); }
static void copy_zero(void*d,const void*p) { (void)p; assert(d); }
static void deliver_zero(void*p) { assert(p && (uintptr_t)p%4096==0); }
static void deallocate_zero(void*state,const void*v,void*p,size_t z,size_t a) { Stats*s=state; s->delivered=1; deallocate(state,v,p,z,a); }
static void layout(void) { Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,3); r.context_alignment=4096; assert(doe_callback_ready(&r,&s.id)==DOE_CALLBACK_OK); assert(s.alignment==4096 && s.size>=sizeof(c) && s.allocs==1 && s.frees==1); release(o); end_stats(&s);
    init_stats(&s); o=new_owner(&s); b=binding(o); c=context(&s); r=request(&s,&b,&c,3); r.context_size=0; r.context_alignment=4096; r.copy=copy_zero; r.deliver=deliver_zero; r.allocator.deallocate=deallocate_zero; assert(doe_callback_ready(&r,&s.id)==DOE_CALLBACK_OK); assert(s.alignment==4096 && s.size%4096==1 && s.frees==1); release(o); end_stats(&s); }
static void fatal_case(const char*name,int post) { Stats s; init_stats(&s); Owner*o=new_owner(&s); doe_callback_owner b=binding(o); Context c=context(&s); doe_callback_request r=request(&s,&b,&c,3); fault=name; post_fault=post; if(strcmp(name,"legacy_id")==0)r.owner=NULL; if(strcmp(name,"misaligned")==0)s.misalign=1; active_trace=&s; (void)doe_callback_ready(&r,&s.id); fputs("ERROR fatal path returned\n",stderr); exit(77); }
int main(int argc,char**argv) { if(argc>1) { fatal_case(argv[1],argc>2); return 77; } validation(); ownerless(); modes(); final_release(); self_wait(); layout(); puts("PASS independent callback public C ABI, validation, ownerless order, modes, typed storage, leases, final destruction and threads"); return 0; }
