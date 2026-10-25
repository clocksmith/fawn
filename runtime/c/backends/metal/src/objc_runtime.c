/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

void *doe_mtl_objc_object(doe_mtl_objc *runtime, void *object, void *selector) {
    return runtime->object_call(object, selector);
}

void doe_mtl_objc_void(doe_mtl_objc *runtime, void *object, void *selector) {
    runtime->void_call(object, selector);
}

size_t doe_mtl_objc_size(doe_mtl_objc *runtime, void *object, void *selector) {
    return runtime->size_call(object, selector);
}

void *doe_mtl_objc_pool(doe_mtl_objc *runtime) {
    return doe_mtl_objc_object(runtime, runtime->pool_class, runtime->new_object);
}

void doe_mtl_objc_drain(doe_mtl_objc *runtime, void *pool) {
    if (pool != NULL) doe_mtl_objc_void(runtime, pool, runtime->drain);
}

void doe_mtl_objc_release(void *userdata, void *object) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    if (object != NULL) doe_mtl_objc_void(runtime, object, runtime->release);
    doe_mtl_objc_drain(runtime, pool);
}

void *doe_mtl_objc_string(doe_mtl_objc *runtime, const char *bytes, size_t length) {
    void *allocated = doe_mtl_objc_object(runtime, runtime->string_class, runtime->alloc);
    if (allocated == NULL) return NULL;
    /* NSString init consumes the allocated receiver even when it returns nil.
     * NSUTF8StringEncoding = 4, from Foundation's NSString encoding contract. */
    return runtime->string_init_call(allocated,
        runtime->string_init, bytes, length, 4);
}

void doe_mtl_objc_text(doe_mtl_objc *runtime, void *string, char *out, size_t capacity) {
    const char *text;
    size_t length;
    if (capacity == 0) return;
    text = string != NULL ?
        runtime->string_call(string, runtime->utf8) : NULL;
    if (text == NULL) {
        out[0] = '\0';
        return;
    }
    length = strlen(text);
    if (length >= capacity) length = capacity - 1;
    memcpy(out, text, length);
    out[length] = '\0';
}

doe_mtl_status doe_mtl_objc_error(doe_mtl_objc *runtime, void *native_error,
    const char *operation, doe_mtl_error *error) {
    doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, operation, error);
    if (error != NULL && native_error != NULL) {
        void *description = doe_mtl_objc_object(runtime, native_error,
            runtime->localized_description);
        error->native_code = runtime->integer_call(native_error,
            runtime->code);
        doe_mtl_objc_text(runtime, description, error->message, sizeof(error->message));
    }
    return DOE_MTL_NATIVE_FAILURE;
}

static doe_mtl_size device_dimensions(doe_mtl_objc *runtime, void *device) {
    doe_mtl_size dimensions = {0};
#if defined(__aarch64__) || defined(__arm64__)
    dimensions = runtime->dimensions_call(device, runtime->max_threads);
#else
    runtime->dimensions_stret_call(&dimensions,
        device, runtime->max_threads);
#endif
    return dimensions;
}

/* Read required device facts once. Caller policy may restrict these limits,
 * but cannot replace them with inferred aggregate/pipeline capabilities. */
bool doe_mtl_objc_limits(doe_mtl_objc *runtime, void *device, doe_mtl_caps *caps) {
    doe_mtl_size dimensions;
    size_t buffer_length, memory_length;
    if (!runtime->responds_call(device, runtime->responds, runtime->max_buffer_length) ||
        !runtime->responds_call(device, runtime->responds, runtime->max_threads) ||
        !runtime->responds_call(device, runtime->responds, runtime->max_threadgroup_memory))
        return false;
    dimensions = device_dimensions(runtime, device);
    buffer_length = doe_mtl_objc_size(runtime, device, runtime->max_buffer_length);
    memory_length = doe_mtl_objc_size(runtime, device, runtime->max_threadgroup_memory);
    if (buffer_length == 0 || memory_length == 0 || dimensions.width == 0 ||
        dimensions.height == 0 || dimensions.depth == 0) return false;
    caps->max_buffer_length = buffer_length;
    caps->max_threadgroup_memory_length = memory_length;
    caps->max_threads_per_dimension[0] = dimensions.width;
    caps->max_threads_per_dimension[1] = dimensions.height;
    caps->max_threads_per_dimension[2] = dimensions.depth;
    return true;
}

#if !defined(_WIN32)
static doe_mtl_status create_device(void *userdata, void **out, doe_mtl_caps *caps,
    doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *device;
    doe_mtl_caps result = {0};
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    device = runtime->create_device(); /* Create-family +1, non-ARC C caller. */
    if (device == NULL) {
        doe_mtl_objc_drain(runtime, pool);
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "MTLCreateSystemDefaultDevice", error);
    }
    if (!doe_mtl_objc_limits(runtime, device, &result)) {
        doe_mtl_objc_void(runtime, device, runtime->release);
        doe_mtl_objc_drain(runtime, pool);
        return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "Metal device limits", error);
    }
    doe_mtl_objc_text(runtime, doe_mtl_objc_object(runtime, device, runtime->name),
        result.device_name, sizeof(result.device_name));
    doe_mtl_objc_image_caps(runtime, device, &result);
    doe_mtl_objc_query_caps(runtime, device, &result);
    *out = device;
    *caps = result;
    doe_mtl_objc_drain(runtime, pool);
    return doe_mtl_fail(DOE_MTL_OK, "MTLCreateSystemDefaultDevice", error);
}

static doe_mtl_status create_queue(void *userdata, void *device, void **out,
    doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *queue;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    queue = doe_mtl_objc_object(runtime, device, runtime->new_queue);
    doe_mtl_objc_drain(runtime, pool);
    if (queue == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newCommandQueue", error);
    *out = queue;
    return doe_mtl_fail(DOE_MTL_OK, "newCommandQueue", error);
}

static doe_mtl_status create_buffer(void *userdata, void *device, size_t length,
    doe_mtl_memory memory, void **out, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *buffer;
    const size_t options = memory == DOE_MTL_PRIVATE ? (size_t)2 << 4 : 0;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    buffer = runtime->buffer_call(device, runtime->new_buffer,
        length, options);
    doe_mtl_objc_drain(runtime, pool);
    if (buffer == NULL)
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newBufferWithLength:options:", error);
    *out = buffer;
    return doe_mtl_fail(DOE_MTL_OK, "newBufferWithLength:options:", error);
}

static void *buffer_contents(void *userdata, void *buffer) {
    doe_mtl_objc *runtime = userdata;
    return doe_mtl_objc_object(runtime, buffer, runtime->contents);
}

static void close_runtime(void *userdata) {
    doe_mtl_objc *runtime = userdata;
    if (runtime == NULL) return;
    (void)doe_mtl_objc_archive_flush(runtime, NULL);
    doe_mtl_objc_archive_clear(runtime);
    doe_mtl_objc_close_external(runtime);
    if (runtime->metal_library != NULL) dlclose(runtime->metal_library);
    if (runtime->foundation_library != NULL) dlclose(runtime->foundation_library);
    if (runtime->objc_library != NULL) dlclose(runtime->objc_library);
    free(runtime);
}

/* Function/data pointer conversion is confined to the POSIX dlsym boundary.
 * memcpy avoids a non-ISO-C object-pointer/function-pointer cast. */
static bool load_function(void *library, const char *name, void *out, size_t size) {
    void *symbol = dlsym(library, name);
    if (symbol == NULL || size != sizeof(symbol)) return false;
    memcpy(out, &symbol, size);
    return true;
}

static bool load_selectors(doe_mtl_objc *runtime) {
    runtime->pool_class = runtime->get_class("NSAutoreleasePool");
    runtime->string_class = runtime->get_class("NSString");
    runtime->responds = runtime->selector("respondsToSelector:");
    runtime->new_object = runtime->selector("new");
    runtime->alloc = runtime->selector("alloc");
    runtime->retain = runtime->selector("retain");
    runtime->release = runtime->selector("release");
    runtime->drain = runtime->selector("drain");
    runtime->new_queue = runtime->selector("newCommandQueue");
    runtime->new_buffer = runtime->selector("newBufferWithLength:options:");
    runtime->contents = runtime->selector("contents");
    runtime->max_buffer_length = runtime->selector("maxBufferLength");
    runtime->max_threads = runtime->selector("maxThreadsPerThreadgroup");
    runtime->max_threadgroup_memory = runtime->selector("maxThreadgroupMemoryLength");
    runtime->name = runtime->selector("name");
    runtime->utf8 = runtime->selector("UTF8String");
    runtime->string_init = runtime->selector("initWithBytes:length:encoding:");
    runtime->new_library = runtime->selector("newLibraryWithSource:options:error:");
    runtime->new_function = runtime->selector("newFunctionWithName:");
    runtime->new_pipeline = runtime->selector("newComputePipelineStateWithFunction:error:");
    runtime->pipeline_max_threads = runtime->selector("maxTotalThreadsPerThreadgroup");
    runtime->localized_description = runtime->selector("localizedDescription");
    runtime->code = runtime->selector("code");
    runtime->new_command = runtime->selector("commandBufferWithUnretainedReferences");
    runtime->blit_encoder = runtime->selector("blitCommandEncoder");
    runtime->compute_encoder = runtime->selector("computeCommandEncoder");
    runtime->copy_buffer = runtime->selector("copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:");
    runtime->fill_buffer = runtime->selector("fillBuffer:range:value:");
    runtime->end_encoding = runtime->selector("endEncoding");
    runtime->set_pipeline = runtime->selector("setComputePipelineState:");
    runtime->set_buffer = runtime->selector("setBuffer:offset:atIndex:");
    runtime->set_bytes = runtime->selector("setBytes:length:atIndex:");
    runtime->dispatch = runtime->selector("dispatchThreadgroups:threadsPerThreadgroup:");
    runtime->dispatch_indirect = runtime->selector(
        "dispatchThreadgroupsWithIndirectBuffer:indirectBufferOffset:threadsPerThreadgroup:");
    runtime->commit = runtime->selector("commit");
    runtime->wait = runtime->selector("waitUntilCompleted");
    runtime->status = runtime->selector("status");
    runtime->error = runtime->selector("error");
    return runtime->pool_class != NULL && runtime->string_class != NULL && runtime->responds != NULL &&
        runtime->new_object != NULL && runtime->alloc != NULL &&
        runtime->retain != NULL && runtime->release != NULL && runtime->drain != NULL &&
        runtime->new_queue != NULL && runtime->new_buffer != NULL &&
        runtime->contents != NULL && runtime->max_buffer_length != NULL &&
        runtime->max_threads != NULL && runtime->max_threadgroup_memory != NULL &&
        runtime->name != NULL && runtime->utf8 != NULL &&
        runtime->string_init != NULL && runtime->new_library != NULL &&
        runtime->new_function != NULL && runtime->new_pipeline != NULL &&
        runtime->pipeline_max_threads != NULL && runtime->localized_description != NULL &&
        runtime->code != NULL && runtime->new_command != NULL && runtime->blit_encoder != NULL &&
        runtime->compute_encoder != NULL && runtime->copy_buffer != NULL &&
        runtime->fill_buffer != NULL && runtime->end_encoding != NULL &&
        runtime->set_pipeline != NULL && runtime->set_buffer != NULL &&
        runtime->set_bytes != NULL && runtime->dispatch != NULL && runtime->dispatch_indirect != NULL &&
        runtime->commit != NULL &&
        runtime->wait != NULL && runtime->status != NULL && runtime->error != NULL;
}

static doe_mtl_status open_apple_runtime(doe_mtl_native *out, doe_mtl_error *error) {
    doe_mtl_objc *runtime = calloc(1, sizeof(*runtime));
    doe_mtl_native native = {0};
    doe_objc_message message = NULL;
    doe_objc_message message_stret = NULL;
    if (runtime == NULL)
        return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "Metal loader allocation", error);
    runtime->objc_library = dlopen("/usr/lib/libobjc.A.dylib", RTLD_NOW | RTLD_LOCAL);
    runtime->foundation_library = dlopen(
        "/System/Library/Frameworks/Foundation.framework/Foundation", RTLD_NOW | RTLD_LOCAL);
    runtime->metal_library = dlopen(
        "/System/Library/Frameworks/Metal.framework/Metal", RTLD_NOW | RTLD_LOCAL);
    if (runtime->objc_library == NULL || runtime->foundation_library == NULL ||
        runtime->metal_library == NULL) goto unavailable;
    if (!load_function(runtime->objc_library, "objc_getClass", &runtime->get_class,
            sizeof(runtime->get_class)) ||
        !load_function(runtime->objc_library, "sel_registerName", &runtime->selector,
            sizeof(runtime->selector)) ||
        !load_function(runtime->objc_library, "objc_msgSend", &message, sizeof(message)) ||
        !load_function(runtime->metal_library, "MTLCreateSystemDefaultDevice",
            &runtime->create_device, sizeof(runtime->create_device))) goto unavailable;
#if !defined(__aarch64__) && !defined(__arm64__)
    if (!load_function(runtime->objc_library, "objc_msgSend_stret",
        &message_stret, sizeof(message_stret))) goto unavailable;
#endif
    runtime->responds_call = (doe_objc_responds_call)message;
    runtime->object_call = (doe_objc_object_call)message;
    runtime->void_call = (doe_objc_void_call)message;
    runtime->size_call = (doe_objc_size_call)message;
    runtime->integer_call = (doe_objc_integer_call)message;
    runtime->string_call = (doe_objc_string_call)message;
    runtime->buffer_call = (doe_objc_buffer_call)message;
    runtime->string_init_call = (doe_objc_string_init_call)message;
    runtime->library_call = (doe_objc_library_call)message;
    runtime->named_call = (doe_objc_named_call)message;
    runtime->pipeline_call = (doe_objc_pipeline_call)message;
    runtime->set_object_call = (doe_objc_set_object_call)message;
    runtime->bind_buffer_call = (doe_objc_bind_buffer_call)message;
    runtime->bind_bytes_call = (doe_objc_bind_bytes_call)message;
    runtime->dispatch_call = (doe_objc_dispatch_call)message;
    runtime->dispatch_indirect_call = (doe_objc_indirect_dispatch_call)message;
    runtime->copy_call = (doe_objc_copy_call)message;
    runtime->fill_call = (doe_objc_fill_call)message;
    runtime->dimensions_call = (doe_objc_dimensions_call)message;
    runtime->dimensions_stret_call = (doe_objc_dimensions_stret_call)message_stret;
    runtime->set_size_call = (doe_objc_set_size_call)message;
    runtime->set_float_call = (doe_objc_set_float_call)message;
    runtime->set_bool_call = (doe_objc_set_bool_call)message;
    runtime->set_drawable_size_call = (doe_objc_set_drawable_size_call)message;
    runtime->bool_call = (doe_objc_bool_call)message;
    runtime->family_call = (doe_objc_family_call)message;
    runtime->samples_call = (doe_objc_samples_call)message;
    runtime->view_call = (doe_objc_view_call)message;
    runtime->bind_image_call = (doe_objc_bind_image_call)message;
    runtime->upload_image_call = (doe_objc_upload_image_call)message;
    runtime->read_image_call = (doe_objc_read_image_call)message;
    runtime->copy_image_call = (doe_objc_copy_image_call)message;
    if (!load_selectors(runtime) || !doe_mtl_objc_load_image_selectors(runtime) ||
        !doe_mtl_objc_load_render(runtime, message)) goto unavailable;
    doe_mtl_objc_load_queries(runtime, message);
    doe_mtl_objc_load_archives(runtime, message);
    doe_mtl_objc_load_external(runtime, message);
    native.userdata = runtime;
    native.close = close_runtime;
    native.release = doe_mtl_objc_release;
    native.external_texture = doe_mtl_objc_external_import;
    native.surface_supported = doe_mtl_objc_surface_supported;
    native.surface_configure = doe_mtl_objc_surface_configure;
    native.surface_acquire = doe_mtl_objc_surface_acquire;
    native.surface_present = doe_mtl_objc_surface_present;
    native.surface_observe = doe_mtl_objc_surface_observe;
    native.device = create_device;
    native.queue = create_queue;
    native.buffer = create_buffer;
    native.contents = buffer_contents;
    native.shader_validate = doe_mtl_objc_shader_validate;
    native.pipeline = doe_mtl_objc_pipeline;
    native.pipeline_cache = doe_mtl_objc_archive_configure;
    native.pipeline_cache_flush = doe_mtl_objc_archive_flush;
    native.pipeline_cache_stats = doe_mtl_objc_archive_stats;
    native.command = doe_mtl_objc_command;
    native.copy = doe_mtl_objc_copy;
    native.fill = doe_mtl_objc_fill;
    native.dispatch = doe_mtl_objc_dispatch;
    native.dispatch_indirect = doe_mtl_objc_dispatch_indirect;
    native.texture = doe_mtl_objc_texture;
    native.view = doe_mtl_objc_view;
    native.sampler = doe_mtl_objc_sampler;
    native.upload_texture = doe_mtl_objc_upload_image;
    native.read_texture = doe_mtl_objc_read_image;
    native.copy_texture = doe_mtl_objc_copy_image;
    native.render_pipeline = doe_mtl_objc_render_pipeline;
    native.render = doe_mtl_objc_render;
    native.timestamps = doe_mtl_objc_timestamps;
    native.timestamp = doe_mtl_objc_timestamp;
    native.timestamp_boundary = doe_mtl_objc_timestamp_boundary;
    native.resolve_timestamps = doe_mtl_objc_resolve_timestamps;
    native.timeline_create = doe_mtl_objc_timeline_create;
    native.timeline_signal = doe_mtl_objc_timeline_signal;
    native.timeline_value = doe_mtl_objc_timeline_value;
    native.commit = doe_mtl_objc_commit;
    native.observe = doe_mtl_objc_observe;
    *out = native;
    return doe_mtl_fail(DOE_MTL_OK, "Metal runtime load", error);
unavailable:
    close_runtime(runtime);
    return doe_mtl_fail(DOE_MTL_LOADER_UNAVAILABLE, "Metal runtime load", error);
}

#endif /* POSIX loader; no Objective-C runtime loader exists on Windows. */

doe_mtl_status doe_mtl_native_open(doe_mtl_native *out, doe_mtl_error *error) {
    if (out == NULL)
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "Metal runtime load", error);
#if defined(__APPLE__) && defined(__MACH__) && \
    defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && SIZE_MAX == UINT64_MAX && \
    (defined(__x86_64__) || defined(__aarch64__) || defined(__arm64__))
    return open_apple_runtime(out, error);
#else
    /* Keep the native ABI source compiled on portable hosts, but never load a
     * substitute implementation or manufacture a successful device there. */
#if !defined(_WIN32)
    (void)open_apple_runtime;
#endif
    return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "Metal requires 64-bit macOS", error);
#endif
}
