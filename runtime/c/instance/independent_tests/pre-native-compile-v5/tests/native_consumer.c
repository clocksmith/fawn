/* SPDX-License-Identifier: MIT
 * Independently authored actual native/public instance consumer. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "webgpu.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*NativeSurfaceCreate)(void *, void *, void *);
typedef void (*NativeSurfaceRelease)(void *);
typedef struct Api {
    WGPUProcCreateInstance create;
    WGPUProcInstanceAddRef retain;
    WGPUProcInstanceRelease release;
    WGPUProcInstanceWaitAny wait;
    WGPUProcInstanceProcessEvents pump;
    WGPUProcInstanceRequestAdapter request;
    WGPUProcInstanceCreateSurface surface;
    WGPUProcSurfaceRelease surface_release;
    WGPUProcGetInstanceFeatures features;
    WGPUProcHasInstanceFeature has;
    WGPUProcGetInstanceLimits limits;
    WGPUProcSupportedInstanceFeaturesFreeMembers features_free;
} Api;
static Api api;
static void *library;
static int selected_c_instance;
static WGPUProcGetProcAddress lookup;
static NativeSurfaceCreate native_surface;
static NativeSurfaceRelease native_surface_release;
static WGPUProcQueueOnSubmittedWorkDone work_done;
static unsigned work_done_count;
static void work_done_callback(WGPUQueueWorkDoneStatus status,WGPUStringView message,void *one,void *two) {
    assert(status == WGPUQueueWorkDoneStatus_Success && message.length == 0 && one == &work_done_count && two == NULL); ++work_done_count;
}
static int token;
static unsigned trace[32], trace_count;
typedef struct Request {
    WGPUInstance instance;
    WGPUFuture future;
    unsigned callbacks, label;
    int drop, self_wait, recursive;
    struct Request *child;
} Request;
static void load(const char *name, void *out, size_t n) {
    void *p = dlsym(library, name);
    assert(p && n == sizeof(p)); memcpy(out, &p, n);
}
static void proc(const char *name, const char *target, void *out, size_t n, int sentinel) {
    WGPUStringView text = {name, sentinel ? SIZE_MAX : strlen(name)};
    WGPUProc p = lookup(text); assert(p && n == sizeof(p)); memcpy(out, &p, n);
    void *raw = NULL; memcpy(&raw, &p, sizeof(raw));
    if (target) assert(raw == dlsym(library, target));
    else {
        Dl_info info, root;
        assert(dladdr(raw, &info) && dladdr(dlsym(library,"wgpuGetProcAddress"), &root));
        assert(info.dli_fbase == root.dli_fbase);
    }
}
static WGPUFuture request(Request *s, unsigned mode);
static void callback(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *u1, void *u2) {
    Request *s = (Request *)u1;
    static const char expected[] = "metal default device unavailable";
    assert(u2 == &token && (unsigned)status == 3 && !adapter);
    assert(message.length == sizeof(expected) - 1 && message.data && !memcmp(message.data, expected, sizeof(expected) - 1));
    ++s->callbacks; assert(trace_count < 32); trace[trace_count++] = s->label;
    if (s->self_wait) {
        WGPUFutureWaitInfo info = {s->future, 0x11};
        assert(api.wait(s->instance, 1, &info, UINT64_MAX) == WGPUWaitStatus_Success && info.completed == 1);
    }
    if (s->child) {
        s->child->instance = s->instance; s->child->future = request(s->child, 2);
        assert(!s->child->callbacks);
        if (s->recursive) { api.pump(s->instance); trace[trace_count++] = 9; }
    }
    if (s->drop) api.release(s->instance);
}
static WGPUFuture request(Request *s, unsigned mode) {
    WGPURequestAdapterCallbackInfo info = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    info.mode = (WGPUCallbackMode)mode; info.callback = callback; info.userdata1 = s; info.userdata2 = &token;
    return api.request(s->instance, NULL, info);
}
static void modes(void) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        Request s; memset(&s, 0, sizeof(s)); trace_count = 0; s.label = 1;
        WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
        s.instance = api.create(&descriptor); assert(s.instance);
        api.retain(s.instance); api.release(s.instance);
        s.future = request(&s, mode);
        assert(s.future.id && s.callbacks == (unsigned)(mode == 0 || mode == 3));
        api.pump(s.instance); assert(s.callbacks == (unsigned)(mode != 1));
        WGPUFutureWaitInfo infos[2] = {{s.future,0x11},{s.future,0x22}};
        assert(api.wait(s.instance, 2, infos, 0) == WGPUWaitStatus_Success && infos[0].completed == 1 && infos[1].completed == 1);
        assert(api.wait(s.instance, 2, infos, 17) == WGPUWaitStatus_Success && s.callbacks == 1);
        api.release(s.instance);
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        Request s; memset(&s, 0, sizeof(s)); trace_count = 0; s.drop = 1;
        s.instance = api.create(NULL); assert(s.instance); s.future = request(&s, mode);
        if (mode == 1) { WGPUFutureWaitInfo info = {s.future,0x11}; assert(api.wait(s.instance,1,&info,0) == WGPUWaitStatus_Success && info.completed == 1); }
        if (mode == 2) api.pump(s.instance);
        assert(s.callbacks == 1);
    }
}
static void reentry(void) {
    for (unsigned recursive = 0; recursive < 2; ++recursive) {
        Request a,b,c; memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b)); memset(&c,0,sizeof(c)); trace_count = 0;
        a.instance = api.create(NULL); assert(a.instance); b.instance = a.instance;
        a.label = 1; b.label = 2; c.label = 3; a.child = &c; a.recursive = (int)recursive;
        a.future = request(&a,2); b.future = request(&b,2); a.self_wait = 1;
        api.pump(a.instance);
        if (recursive) { const unsigned expected[] = {1,2,3,9}; assert(trace_count == 4 && !memcmp(trace,expected,sizeof(expected))); }
        else { assert(trace_count == 2 && trace[0] == 1 && trace[1] == 2 && c.callbacks == 0); api.pump(a.instance); assert(trace_count == 3 && trace[2] == 3); }
        assert(a.callbacks == 1 && b.callbacks == 1 && c.callbacks == 1); api.release(a.instance);
    }
    WGPUInstance p = api.create(NULL); assert(p);
    WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowProcessEvents; cb.callback = NULL;
    WGPUFuture f = api.request(p,NULL,cb); api.pump(p);
    WGPUFutureWaitInfo w = {f,0x11}; assert(api.wait(p,1,&w,0) == WGPUWaitStatus_Success && w.completed == 1); api.release(p);
}
static void invalid_and_capabilities(void) {
    uint64_t bad[8192] = {UINT64_C(0xDEADC0DE)};
    WGPUInstance handles[2] = {NULL,(WGPUInstance)(void *)bad};
    for (unsigned i = 0; i < 2; ++i) {
        api.retain(handles[i]); api.release(handles[i]);
        WGPUFutureWaitInfo w = {{0},0x11}; assert(api.wait(handles[i],1,&w,0) == WGPUWaitStatus_Error);
        const uint64_t times[3] = {0,17,UINT64_MAX};
        for (unsigned j = 0; j < 3; ++j) assert(api.wait(handles[i],0,NULL,times[j]) == WGPUWaitStatus_Success);
        WGPUQueueWorkDoneCallbackInfo cb = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        cb.mode = WGPUCallbackMode_AllowProcessEvents; cb.callback = work_done_callback; cb.userdata1 = &work_done_count;
        unsigned prior = work_done_count; (void)work_done(NULL,cb); assert(work_done_count == prior);
        api.pump(handles[i]); assert(work_done_count == prior + 1);
    }
    WGPUSupportedInstanceFeatures f = WGPU_SUPPORTED_INSTANCE_FEATURES_INIT;
    api.features(&f); assert(f.featureCount == 0 && f.features == NULL);
    const uint32_t names[] = {0,1,2,3,0x7fffffff};
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); ++i) assert(!api.has((WGPUInstanceFeatureName)names[i]));
    WGPUInstanceLimits l = WGPU_INSTANCE_LIMITS_INIT;
    assert(api.limits(&l) == WGPUStatus_Success && l.nextInChain == NULL && l.timedWaitAnyMaxCount == 0);
    api.features_free(f);
}
static void surface_lifetime(void) {
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    for (unsigned native_release = 0; native_release < 2; ++native_release) {
        Request s; memset(&s,0,sizeof(s)); trace_count = 0;
        s.instance = api.create(NULL); assert(s.instance);
        WGPUSurface surface = api.surface(s.instance,&descriptor); assert(surface);
        /* An actual live wrong-kind object, not a fabricated device. */
        if (selected_c_instance) {
            WGPUInstance wrong = (WGPUInstance)(void *)surface;
            api.retain(wrong); api.release(wrong);
            WGPUFutureWaitInfo invalid = {{0},0x11}; assert(api.wait(wrong,1,&invalid,0) == WGPUWaitStatus_Error);
        }
        api.release(s.instance); /* The real surface is now the only caller lease. */
        s.future = request(&s,2); api.pump(s.instance); assert(s.callbacks == 1);
        WGPUFutureWaitInfo info = {s.future,0x11}; assert(api.wait(s.instance,1,&info,0) == WGPUWaitStatus_Success && info.completed == 1);
        if (native_release) native_surface_release(surface); else api.surface_release(surface);
    }
    uint64_t bad[8192] = {UINT64_C(0xDEADC0DE)};
    void *native_inputs[2] = {NULL,bad};
    for (unsigned i = 0; i < 2; ++i) {
        void *surface = native_surface(native_inputs[i],&descriptor,NULL); assert(surface); native_surface_release(surface);
    }
    WGPUInstance p = api.create(NULL); assert(p);
    assert(!native_surface(p,&descriptor,bad)); /* Real post-retain bind rejection. */
    api.pump(p); api.release(p);
}
#define DIRECT(field,name) load(name,&api.field,sizeof(api.field))
#define PROC(field,name,target) proc(name,target,&api.field,sizeof(api.field),sentinel)
static void select_api(int selection) {
    if (selection < 0 || selection == 2) {
        DIRECT(create,"wgpuCreateInstance"); DIRECT(retain,"wgpuInstanceAddRef"); DIRECT(release,"wgpuInstanceRelease");
        DIRECT(wait,"wgpuInstanceWaitAny"); DIRECT(pump,"wgpuInstanceProcessEvents"); DIRECT(request,"wgpuInstanceRequestAdapter");
        DIRECT(surface,"wgpuInstanceCreateSurface"); DIRECT(surface_release,"wgpuSurfaceRelease");
        DIRECT(features,"wgpuGetInstanceFeatures"); DIRECT(has,"wgpuHasInstanceFeature"); DIRECT(limits,"wgpuGetInstanceLimits"); DIRECT(features_free,"wgpuSupportedInstanceFeaturesFreeMembers");
        if (selection == 2) {
            DIRECT(create,"doeNativeCreateInstance"); DIRECT(retain,"doeNativeInstanceAddRef"); DIRECT(release,"doeNativeInstanceRelease");
            DIRECT(wait,"doeNativeInstanceWaitAny"); DIRECT(pump,"doeNativeInstanceProcessEvents"); DIRECT(request,"doeNativeInstanceRequestAdapter");
        }
    } else {
        int sentinel = selection;
        PROC(create,"wgpuCreateInstance","wgpuCreateInstance"); PROC(retain,"wgpuInstanceAddRef","wgpuInstanceAddRef"); PROC(release,"wgpuInstanceRelease","wgpuInstanceRelease");
        PROC(wait,"wgpuInstanceWaitAny","wgpuInstanceWaitAny"); PROC(pump,"wgpuInstanceProcessEvents","wgpuInstanceProcessEvents"); PROC(request,"wgpuInstanceRequestAdapter","wgpuInstanceRequestAdapter");
        PROC(surface,"wgpuInstanceCreateSurface","wgpuInstanceCreateSurface"); PROC(surface_release,"wgpuSurfaceRelease","wgpuSurfaceRelease");
        PROC(features,"wgpuGetInstanceFeatures","wgpuGetInstanceFeatures"); PROC(has,"wgpuHasInstanceFeature","wgpuHasInstanceFeature");
        PROC(limits,"wgpuGetInstanceLimits","wgpuGetInstanceLimits"); PROC(features_free,"wgpuSupportedInstanceFeaturesFreeMembers","wgpuSupportedInstanceFeaturesFreeMembers");
    }
}
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3); assert(setenv("DOE_BACKEND","metal",1) == 0);
    library = dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!library) { fprintf(stderr,"%s\n",dlerror()); return 2; }
    selected_c_instance = dlsym(library,"doe_instance_from_handle") != NULL;
    if (argc == 3) assert(selected_c_instance == (strcmp(argv[2],"FCI") == 0));
    load("wgpuGetProcAddress",&lookup,sizeof(lookup));
    load("wgpuQueueOnSubmittedWorkDone",&work_done,sizeof(work_done));
    load("doeNativeInstanceCreateSurface",&native_surface,sizeof(native_surface));
    load("doeNativeSurfaceRelease",&native_surface_release,sizeof(native_surface_release));
    for (int selection = -1; selection < 3; ++selection) {
        select_api(selection); modes(); reentry(); invalid_and_capabilities(); surface_lifetime();
    }
    assert(dlclose(library) == 0);
    puts("PASS independent direct/proc instance modes/capabilities/reentry/actual unconfigured surface; no GPU qualification");
    return 0;
}
