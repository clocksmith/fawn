/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

/* Caller owns an autorelease pool. The function owns +1 on success; strings and
 * temporary library ownership never escape this shared stage constructor. */
doe_mtl_status doe_mtl_objc_function(doe_mtl_objc *runtime, void *device,
    const char *msl, size_t length, const char *entrypoint, void **out, doe_mtl_error *error) {
    void *source = NULL, *name = NULL, *library = NULL, *function = NULL, *native_error = NULL;
    doe_mtl_status status;
    source = doe_mtl_objc_string(runtime, msl, length);
    name = doe_mtl_objc_string(runtime, entrypoint, strlen(entrypoint));
    if (source == NULL || name == NULL) {
        status = doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "MSL UTF-8 encoding", error);
        goto cleanup;
    }
    library = runtime->library_call(device, runtime->new_library, source, NULL, &native_error);
    if (library == NULL) {
        status = doe_mtl_objc_error(runtime, native_error, "newLibraryWithSource:options:error:", error);
        goto cleanup;
    }
    function = runtime->named_call(library, runtime->new_function, name);
    if (function == NULL) {
        status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newFunctionWithName:", error);
        goto cleanup;
    }
    *out = function;
    status = DOE_MTL_OK;
cleanup:
    if (library != NULL) doe_mtl_objc_void(runtime, library, runtime->release);
    if (name != NULL) doe_mtl_objc_void(runtime, name, runtime->release);
    if (source != NULL) doe_mtl_objc_void(runtime, source, runtime->release);
    return status;
}

doe_mtl_status doe_mtl_objc_pipeline(void *userdata, void *device,
    const doe_mtl_pipeline_desc *desc, void **out, size_t *max_threads, doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime), *function = NULL, *pipeline = NULL, *native_error = NULL;
    void *descriptor = NULL;
    doe_mtl_status status;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    status = doe_mtl_objc_function(runtime, device, desc->msl, desc->msl_length, desc->entrypoint, &function, error);
    if (status != DOE_MTL_OK) goto cleanup;
    bool archived = false;
    if (runtime->archives.archive) {
        descriptor = doe_mtl_objc_object(runtime, runtime->archives.compute_class, runtime->new_object);
        if (descriptor) {
            runtime->set_object_call(descriptor, runtime->archives.set_compute_function, function);
            archived = doe_mtl_objc_archive_attach(runtime, descriptor);
            if (archived) pipeline = runtime->archives.compute_call(device, runtime->archives.new_compute,
                descriptor, 0, NULL, &native_error);
        }
    }
    /* Optional cache lookup cannot turn a valid ordinary pipeline into a failure. */
    if (!pipeline) {
        native_error = NULL;
        pipeline = runtime->pipeline_call(device, runtime->new_pipeline, function, &native_error);
    }
    if (pipeline == NULL) {
        status = doe_mtl_objc_error(runtime, native_error, "newComputePipelineStateWithFunction:error:", error);
        goto cleanup;
    }
    if (archived) doe_mtl_objc_archive_add(runtime, descriptor, true);
    *max_threads = doe_mtl_objc_size(runtime, pipeline, runtime->pipeline_max_threads);
    *out = pipeline;
    status = doe_mtl_fail(DOE_MTL_OK, "compute pipeline create", error);
cleanup:
    if (descriptor != NULL) doe_mtl_objc_void(runtime, descriptor, runtime->release);
    if (function != NULL) doe_mtl_objc_void(runtime, function, runtime->release);
    doe_mtl_objc_drain(runtime, pool);
    return status;
}
