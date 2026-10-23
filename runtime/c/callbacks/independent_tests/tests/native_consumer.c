/* SPDX-License-Identifier: MIT
 * Independent actual exported native consumers. Linux Metal-unavailable path
 * owns a real native instance/payload; null handle cases are ownerless only. */
#define _POSIX_C_SOURCE 200809L
#include "webgpu.h"
#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Api {
 WGPUProcCreateInstance create;
 WGPUProcInstanceRelease release;
 WGPUProcInstanceProcessEvents pump;
 WGPUProcInstanceWaitAny wait;
 WGPUProcInstanceRequestAdapter adapter;
 WGPUProcAdapterRequestDevice device;
 WGPUProcBufferMapAsync map;
 WGPUProcDevicePopErrorScope scope;
 WGPUProcShaderModuleGetCompilationInfo info;
 WGPUProcDeviceCreateComputePipelineAsync compute;
 WGPUProcDeviceCreateRenderPipelineAsync render;
} Api;
static Api api;
static int userdata_identity;
typedef struct State { WGPUInstance instance; WGPUFuture future; unsigned count; int drop, inspect, spawn; struct State *child; } State;
static void symbol(void*l,const char*n,void*out,size_t z) { void*p=dlsym(l,n); assert(p && z==sizeof(p)); memcpy(out,&p,z); }
#define LOAD(field,name) symbol(l,name,&api.field,sizeof(api.field))
static void message(WGPUStringView m,const char *expected) { assert(m.data && m.length==strlen(expected) && memcmp(m.data,expected,m.length)==0); }
static State *state(void*p,void*u) { assert(u==&userdata_identity); return (State*)p; }
static void *other_wait(void*p) { State*s=(State*)p; WGPUFutureWaitInfo w={s->future,71}; assert(api.wait(s->instance,1,&w,0)==WGPUWaitStatus_TimedOut && w.completed==0); return NULL; }
static void adapter_cb(WGPURequestAdapterStatus status,WGPUAdapter a,WGPUStringView m,void*p,void*u);
static WGPUFuture request(State*s,unsigned mode) { WGPURequestAdapterCallbackInfo cb=WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT; cb.mode=(WGPUCallbackMode)mode; cb.callback=adapter_cb; cb.userdata1=s; cb.userdata2=&userdata_identity; return api.adapter(s->instance,NULL,cb); }
static void adapter_cb(WGPURequestAdapterStatus status,WGPUAdapter a,WGPUStringView m,void*p,void*u) { State*s=state(p,u); assert((unsigned)status==3 && a==NULL); message(m,"metal default device unavailable"); s->count++; if(s->inspect) { WGPUFutureWaitInfo w={s->future,72}; assert(api.wait(s->instance,1,&w,UINT64_MAX)==WGPUWaitStatus_Success && w.completed==1); pthread_t t; assert(pthread_create(&t,NULL,other_wait,s)==0); assert(pthread_join(t,NULL)==0); api.pump(s->instance); } if(s->spawn) { s->child->instance=s->instance; s->child->future=request(s->child,2); assert(s->child->count==0); } if(s->drop)api.release(s->instance); }
static void device_cb(WGPURequestDeviceStatus status,WGPUDevice d,WGPUStringView m,void*p,void*u) { State*s=state(p,u); assert((unsigned)status==3 && d==NULL); message(m,"invalid adapter handle"); s->count++; }
static void map_cb(WGPUMapAsyncStatus status,WGPUStringView m,void*p,void*u) { State*s=state(p,u); assert((unsigned)status==3); message(m,"invalid buffer mapping"); s->count++; }
static void scope_cb(WGPUPopErrorScopeStatus status,WGPUErrorType type,WGPUStringView m,void*p,void*u) { State*s=state(p,u); assert((unsigned)status==3 && (unsigned)type==1); message(m,"no error scope to pop"); s->count++; }
static void info_cb(WGPUCompilationInfoRequestStatus status,const WGPUCompilationInfo*i,void*p,void*u) { State*s=state(p,u); assert((unsigned)status==2 && i==NULL); s->count++; }
static void compute_cb(WGPUCreatePipelineAsyncStatus status,WGPUComputePipeline h,WGPUStringView m,void*p,void*u) { State*s=state(p,u); assert((unsigned)status==3 && h==NULL); message(m,"InvalidDescriptor"); s->count++; }
static void render_cb(WGPUCreatePipelineAsyncStatus status,WGPURenderPipeline h,WGPUStringView m,void*p,void*u) { State*s=state(p,u); assert((unsigned)status==3 && h==NULL); message(m,"InvalidDescriptor"); s->count++; }
static WGPUFuture ownerless(const char*family,unsigned mode,State*s,int null_callback) {
 if(strcmp(family,"device")==0) { WGPURequestDeviceCallbackInfo cb=WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT; cb.mode=(WGPUCallbackMode)mode; cb.callback=null_callback?NULL:device_cb; cb.userdata1=s; cb.userdata2=&userdata_identity; return api.device(NULL,NULL,cb); }
 if(strcmp(family,"map")==0) { WGPUBufferMapCallbackInfo cb=WGPU_BUFFER_MAP_CALLBACK_INFO_INIT; cb.mode=(WGPUCallbackMode)mode; cb.callback=null_callback?NULL:map_cb; cb.userdata1=s; cb.userdata2=&userdata_identity; return api.map(NULL,WGPUMapMode_Read,0,0,cb); }
 if(strcmp(family,"scope")==0) { WGPUPopErrorScopeCallbackInfo cb=WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT; cb.mode=(WGPUCallbackMode)mode; cb.callback=null_callback?NULL:scope_cb; cb.userdata1=s; cb.userdata2=&userdata_identity; return api.scope(NULL,cb); }
 if(strcmp(family,"info")==0) { WGPUCompilationInfoCallbackInfo cb=WGPU_COMPILATION_INFO_CALLBACK_INFO_INIT; cb.mode=(WGPUCallbackMode)mode; cb.callback=null_callback?NULL:info_cb; cb.userdata1=s; cb.userdata2=&userdata_identity; return api.info(NULL,cb); }
 if(strcmp(family,"compute")==0) { WGPUCreateComputePipelineAsyncCallbackInfo cb=WGPU_CREATE_COMPUTE_PIPELINE_ASYNC_CALLBACK_INFO_INIT; cb.mode=(WGPUCallbackMode)mode; cb.callback=null_callback?NULL:compute_cb; cb.userdata1=s; cb.userdata2=&userdata_identity; WGPUComputePipelineDescriptor d=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT; d.compute.constantCount=1; d.compute.constants=NULL; return api.compute(NULL,&d,cb); }
 assert(strcmp(family,"render")==0); WGPUCreateRenderPipelineAsyncCallbackInfo cb=WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT; cb.mode=(WGPUCallbackMode)mode; cb.callback=null_callback?NULL:render_cb; cb.userdata1=s; cb.userdata2=&userdata_identity; WGPURenderPipelineDescriptor d=WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT; d.vertex.constantCount=1; d.vertex.constants=NULL; return api.render(NULL,&d,cb);
}
static void owned_modes(void) { for(unsigned mode=0;mode<4;mode++) { State s; memset(&s,0,sizeof(s)); s.instance=api.create(NULL); assert(s.instance); s.future=request(&s,mode); assert(s.future.id && s.count==(unsigned)(mode==0||mode==3)); api.pump(s.instance); assert(s.count==(unsigned)(mode!=1)); WGPUFutureWaitInfo w[2]={{s.future,41},{s.future,42}}; assert(api.wait(s.instance,2,w,0)==WGPUWaitStatus_Success && w[0].completed==1 && w[1].completed==1); assert(api.wait(s.instance,2,w,0)==WGPUWaitStatus_Success && s.count==1); api.release(s.instance); } }
static void final_release(void) { for(unsigned mode=0;mode<4;mode++) { State s; memset(&s,0,sizeof(s)); s.instance=api.create(NULL); assert(s.instance); s.drop=1; s.future=request(&s,mode); if(mode==1) { WGPUFutureWaitInfo w={s.future,41}; assert(api.wait(s.instance,1,&w,0)==WGPUWaitStatus_Success && w.completed==1); } else if(mode==2)api.pump(s.instance); assert(s.count==1); } }
static void reentry(void) { State s,child; memset(&s,0,sizeof(s)); memset(&child,0,sizeof(child)); s.instance=api.create(NULL); assert(s.instance); s.child=&child; s.spawn=1; s.future=request(&s,2); api.pump(s.instance); assert(s.count==1 && child.count==0); api.pump(s.instance); assert(child.count==1 && child.future.id>s.future.id); api.release(s.instance);
 memset(&s,0,sizeof(s)); s.instance=api.create(NULL); assert(s.instance); s.inspect=1; s.future=request(&s,1); WGPUFutureWaitInfo w={s.future,0}; assert(api.wait(s.instance,1,&w,UINT64_MAX)==WGPUWaitStatus_Success && s.count==1); api.release(s.instance); }
int main(int argc,char**argv) { assert(argc==2||argc==4); assert(setenv("DOE_BACKEND","metal",1)==0); void*l=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if(!l) { fprintf(stderr,"%s\n",dlerror()); return 2; } LOAD(create,"wgpuCreateInstance"); LOAD(release,"wgpuInstanceRelease"); LOAD(pump,"wgpuInstanceProcessEvents"); LOAD(wait,"wgpuInstanceWaitAny"); LOAD(adapter,"wgpuInstanceRequestAdapter"); LOAD(device,"wgpuAdapterRequestDevice"); LOAD(map,"wgpuBufferMapAsync"); LOAD(scope,"wgpuDevicePopErrorScope"); LOAD(info,"wgpuShaderModuleGetCompilationInfo"); LOAD(compute,"wgpuDeviceCreateComputePipelineAsync"); LOAD(render,"wgpuDeviceCreateRenderPipelineAsync");
 if(argc==4) { State s; memset(&s,0,sizeof(s)); (void)ownerless(argv[2],(unsigned)strtoul(argv[3],NULL,10),&s,0); fputs("ERROR fatal native call returned\n",stderr); return 77; }
 owned_modes(); final_release(); reentry(); const char*families[]={"device","map","scope","info","compute","render"}; for(size_t i=0;i<sizeof(families)/sizeof(families[0]);i++) for(unsigned mode=0;mode<=3;mode+=3) for(int null_callback=0;null_callback<2;null_callback++) { State s; memset(&s,0,sizeof(s)); WGPUFuture f=ownerless(families[i],mode,&s,null_callback); assert(f.id>>63 && s.count==(unsigned)!null_callback); }
 assert(dlclose(l)==0); puts("PASS actual exported owned Metal-unavailable adapter modes/reentry/final-release and six ownerless API variants; no successful backend/GPU qualification"); return 0; }
