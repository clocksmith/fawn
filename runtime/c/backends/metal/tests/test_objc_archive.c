/* SPDX-License-Identifier: MIT
 * Typed archive calls and ownership only; no SDK, files, binary reuse or GPU execution. */
#include "../src/objc_abi.h"
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>

typedef enum kind { POOL, STRING, URL, DESCRIPTOR, ARCHIVE, ARRAY, LIBRARY, FUNCTION, PIPELINE } kind;
typedef struct object {
    kind type;
    unsigned references;
    struct object *url, *function, *archives;
    char text[512];
} object;
typedef struct fixture {
    doe_mtl_objc runtime;
    object objects[256], device, manager, error, description;
    unsigned used, live, libraries, ordinary_pipelines, cached_pipelines, creates, adds, writes;
    bool unavailable, fail_directory, fail_load, fail_empty, fail_add, fail_write, fail_cached, fail_pipeline;
    char serialized[512];
} fixture;
static fixture *active;
static bool selector(void *value, const char *text) { return !strcmp(value, text); }
static object *make(kind type, bool owned) {
    assert(active->used < 256);
    object *result = &active->objects[active->used++];
    result->type = type;
    result->references = owned ? 1u : 0u;
    active->live += result->references;
    return result;
}
static void *name(const char *value) { return (void *)value; }
static void *object_call(void *receiver, void *sel) {
    if (selector(sel, "retain")) {
        object *o = receiver; ++o->references; ++active->live; return receiver;
    }
    if (selector(sel, "new")) {
        if (!strcmp(receiver, "Pool")) return make(POOL, true);
        assert(!strcmp(receiver, "MTLBinaryArchiveDescriptor") || !strcmp(receiver, "MTLComputePipelineDescriptor"));
        return make(DESCRIPTOR, true);
    }
    if (selector(sel, "alloc")) { assert(!strcmp(receiver, "NSString")); return make(STRING, true); }
    if (selector(sel, "defaultManager")) return &active->manager;
    assert(selector(sel, "localizedDescription") && receiver == &active->error);
    return &active->description;
}
static void void_call(void *receiver, void *sel) {
    object *o = receiver;
    assert(selector(sel, "release") || (selector(sel, "drain") && o->type == POOL));
    assert(o->references && active->live);
    --o->references; --active->live;
}
static void *string_init(void *receiver, void *sel, const void *bytes, size_t length, size_t encoding) {
    object *o = receiver;
    assert(selector(sel, "initWithBytes:length:encoding:") && encoding == 4 && length < sizeof(o->text));
    memcpy(o->text, bytes, length); o->text[length] = 0;
    return o;
}
static void *named(void *receiver, void *sel, void *argument) {
    object *value = argument;
    if (selector(sel, "fileURLWithPath:")) {
        assert(!strcmp(receiver, "NSURL") && value->type == STRING);
        object *url = make(URL, false); strcpy(url->text, value->text); return url;
    }
    if (selector(sel, "arrayWithObject:")) {
        assert(!strcmp(receiver, "NSArray") && value->type == ARCHIVE);
        object *array = make(ARRAY, false); array->archives = value; return array;
    }
    assert(selector(sel, "newFunctionWithName:") && ((object *)receiver)->type == LIBRARY);
    assert(!strcmp(value->text, "k"));
    object *function = make(FUNCTION, true); strcpy(function->text, ((object *)receiver)->text); return function;
}
static void set_object(void *receiver, void *sel, void *value) {
    object *o = receiver;
    assert(o->type == DESCRIPTOR);
    if (selector(sel, "setUrl:")) o->url = value;
    else if (selector(sel, "setComputeFunction:")) o->function = value;
    else { assert(selector(sel, "setBinaryArchives:")); o->archives = value; }
}
static doe_objc_bool responds(void *receiver, void *sel, void *method) {
    (void)receiver; (void)method;
    assert(selector(sel, "respondsToSelector:"));
    return (doe_objc_bool)!active->unavailable;
}
static doe_objc_bool directory(void *receiver, void *sel, void *path, doe_objc_bool intermediate,
    void *attributes, void **error) {
    assert(receiver == &active->manager && selector(sel, "createDirectoryAtPath:withIntermediateDirectories:attributes:error:"));
    assert(((object *)path)->type == STRING && intermediate && !attributes);
    if (active->fail_directory) { *error = &active->error; return false; }
    return true;
}
static void *pipeline_call(void *receiver, void *sel, void *argument, void **error) {
    assert(receiver == &active->device);
    object *o = argument;
    if (selector(sel, "newBinaryArchiveWithDescriptor:error:")) {
        assert(o->type == DESCRIPTOR); ++active->creates;
        if ((o->url && active->fail_load) || (!o->url && active->fail_empty)) {
            *error = &active->error; return NULL;
        }
        return make(ARCHIVE, true);
    }
    assert(selector(sel, "newComputePipelineStateWithFunction:error:") && o->type == FUNCTION);
    ++active->ordinary_pipelines;
    if (active->fail_pipeline) { *error = &active->error; return NULL; }
    return make(PIPELINE, true);
}
static void *library(void *receiver, void *sel, void *source, void *options, void **error) {
    (void)error;
    assert(receiver == &active->device && selector(sel, "newLibraryWithSource:options:error:") && !options);
    object *result = make(LIBRARY, true); strcpy(result->text, ((object *)source)->text);
    ++active->libraries; return result;
}
static void *compute(void *receiver, void *sel, void *descriptor, size_t options, void **reflection, void **error) {
    object *d = descriptor;
    assert(receiver == &active->device && selector(sel, "newComputePipelineStateWithDescriptor:options:reflection:error:"));
    assert(options == 0 && !reflection && d->function && d->function->references);
    assert(d->archives && d->archives->archives == active->runtime.archives.archive);
    ++active->cached_pipelines;
    if (active->fail_cached) { *error = &active->error; return NULL; }
    return make(PIPELINE, true);
}
static doe_objc_bool archive_error(void *receiver, void *sel, void *value, void **error) {
    assert(receiver == active->runtime.archives.archive);
    object *o = value;
    if (selector(sel, "serializeToURL:error:")) {
        assert(o->type == URL); ++active->writes;
        if (active->fail_write) { *error = &active->error; return false; }
        strcpy(active->serialized, o->text); return true;
    }
    assert(o->type == DESCRIPTOR);
    if (selector(sel, "addComputePipelineFunctionsWithDescriptor:error:")) assert(o->function && o->function->references);
    else assert(selector(sel, "addRenderPipelineFunctionsWithDescriptor:error:"));
    ++active->adds;
    if (active->fail_add) { *error = &active->error; return false; }
    return true;
}
static size_t size_call(void *receiver, void *sel) {
    assert(((object *)receiver)->type == PIPELINE && selector(sel, "maxTotalThreadsPerThreadgroup")); return 64;
}
static intptr_t integer(void *receiver, void *sel) {
    assert(receiver == &active->error && selector(sel, "code")); return -77;
}
static const char *string(void *receiver, void *sel) {
    assert(receiver == &active->description && selector(sel, "UTF8String")); return "owned archive fault";
}
static void initialize(fixture *f) {
    memset(f, 0, sizeof(*f)); active = f;
    doe_mtl_objc *r = &f->runtime;
    r->get_class = name; r->selector = name;
    r->pool_class = name("Pool"); r->string_class = name("NSString");
    r->new_object = name("new"); r->alloc = name("alloc"); r->retain = name("retain");
    r->release = name("release"); r->drain = name("drain"); r->responds = name("respondsToSelector:");
    r->string_init = name("initWithBytes:length:encoding:"); r->utf8 = name("UTF8String");
    r->localized_description = name("localizedDescription"); r->code = name("code");
    r->new_library = name("newLibraryWithSource:options:error:"); r->new_function = name("newFunctionWithName:");
    r->new_pipeline = name("newComputePipelineStateWithFunction:error:");
    r->pipeline_max_threads = name("maxTotalThreadsPerThreadgroup");
    r->object_call = object_call; r->void_call = void_call; r->string_init_call = string_init;
    r->named_call = named; r->set_object_call = set_object; r->responds_call = responds;
    r->pipeline_call = pipeline_call; r->library_call = library; r->size_call = size_call;
    r->integer_call = integer; r->string_call = string;
    doe_mtl_objc_load_archives(r, NULL);
    r->archives.directory_call = directory; r->archives.error_call = archive_error; r->archives.compute_call = compute;
}
static void compile(fixture *f, const char *source) {
    doe_mtl_pipeline_desc desc = {.msl=source,.msl_length=strlen(source),.entrypoint="k"};
    void *pipeline = NULL; size_t threads = 0;
    assert(doe_mtl_objc_pipeline(&f->runtime, &f->device, &desc, &pipeline, &threads, NULL) == DOE_MTL_OK);
    assert(pipeline && threads == 64); void_call(pipeline, name("release"));
}
int main(void) {
    fixture f; initialize(&f);
    doe_mtl_context context = {.device=&f.device,.native={.userdata=&f.runtime,
        .pipeline_cache=doe_mtl_objc_archive_configure,.pipeline_cache_flush=doe_mtl_objc_archive_flush,
        .pipeline_cache_stats=doe_mtl_objc_archive_stats}};
    doe_mtl_pipeline_cache_stats stats;
    doe_mtl_error error;
    assert(doe_mtl_context_pipeline_cache(NULL, "a", 1, &error) == DOE_MTL_INVALID_ARGUMENT);
    assert(doe_mtl_context_pipeline_cache(&context, "a\0b", 3, &error) == DOE_MTL_INVALID_ARGUMENT);
    f.unavailable = true;
    assert(doe_mtl_context_pipeline_cache(&context, "cache", 5, &error) == DOE_MTL_UNSUPPORTED && !f.live);
    f.unavailable = false; f.fail_load = true;
    assert(doe_mtl_context_pipeline_cache(&context, "cache", 5, &error) == DOE_MTL_OK && f.live == 2);
    assert(f.creates == 2 && f.runtime.archives.stats.load_attempts == 1 && !f.runtime.archives.stats.load_successes);
    compile(&f, "kernel void k(){/*first*/}");
    compile(&f, "kernel void k(){/*different source, same entry*/}");
    assert(f.libraries == 2 && f.cached_pipelines == 2 && !f.ordinary_pipelines && f.adds == 2 && f.live == 2);
    assert(doe_mtl_context_pipeline_cache_flush(&context, &error) == DOE_MTL_OK);
    assert(!strcmp(f.serialized, "cache/doe_pipeline_archive_v1.metallib"));
    assert(doe_mtl_context_pipeline_cache_stats(&context, &stats) == DOE_MTL_OK && stats.serializations == 1 && !stats.dirty);
    f.fail_cached = true;
    compile(&f, "kernel void k(){/*ordinary fallback*/}");
    assert(f.ordinary_pipelines == 1 && f.cached_pipelines == 3 && f.libraries == 3);
    object *render = make(DESCRIPTOR, true);
    assert(doe_mtl_objc_archive_attach(&f.runtime, render));
    doe_mtl_objc_archive_add(&f.runtime, render, false); void_call(render, name("release"));
    f.fail_write = true;
    assert(doe_mtl_context_pipeline_cache_flush(&context, &error) == DOE_MTL_NATIVE_FAILURE);
    assert(error.native_code == -77 && !strcmp(error.message, "owned archive fault") && f.runtime.archives.stats.dirty);
    void *saved = f.runtime.archives.archive;
    f.fail_directory = true;
    assert(doe_mtl_context_pipeline_cache(&context, "other", 5, &error) == DOE_MTL_NATIVE_FAILURE);
    assert(f.runtime.archives.archive == saved && f.live == 2);
    f.fail_directory = false; f.fail_empty = true;
    assert(doe_mtl_context_pipeline_cache(&context, "other", 5, &error) == DOE_MTL_NATIVE_FAILURE);
    assert(f.runtime.archives.archive == saved && f.live == 2);
    f.fail_empty = false; f.fail_add = true;
    compile(&f, "kernel void k(){/*archive add fails, pipeline valid*/}");
    assert(f.runtime.archives.stats.addition_failures == 1 && f.live == 2);
    f.fail_load = false; f.fail_write = false; f.fail_add = false;
    assert(doe_mtl_context_pipeline_cache(&context, "other", 5, &error) == DOE_MTL_OK);
    assert(f.runtime.archives.archive != saved && f.live == 2 && f.runtime.archives.stats.load_successes == 1);
    compile(&f, "kernel void k(){/*loaded archive*/}");
    assert(doe_mtl_context_pipeline_cache_flush(&context, &error) == DOE_MTL_OK);
    assert(!strcmp(f.serialized, "other/doe_pipeline_archive_v1.metallib"));
    unsigned writes = f.writes;
    assert(doe_mtl_context_pipeline_cache(&context, NULL, 0, &error) == DOE_MTL_OK && !f.live && f.writes == writes);
    compile(&f, "kernel void k(){/*disabled*/}");
    assert(!f.live && f.libraries == 6 && f.ordinary_pipelines == 4);
    assert(doe_mtl_context_pipeline_cache_stats(&context, &stats) == DOE_MTL_OK && !stats.enabled && !stats.dirty);
    puts("PASS Metal archive typed configuration/load-miss/add/flush/disable, exact live function descriptors, cache failure fallback and owned cleanup; no files/SDK/GPU or cache-hit qualification");
    return 0;
}
