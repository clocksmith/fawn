/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

doe_mtl_status doe_mtl_objc_pipeline(void *userdata, void *device,
    const doe_mtl_pipeline_desc *desc, void **out, size_t *max_threads,
    doe_mtl_error *error) {
    doe_mtl_objc *runtime = userdata;
    void *pool = doe_mtl_objc_pool(runtime);
    void *source = NULL, *name = NULL, *library = NULL, *function = NULL;
    void *pipeline = NULL, *native_error = NULL;
    doe_mtl_status status;
    if (pool == NULL) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "autorelease pool", error);
    source = doe_mtl_objc_string(runtime, desc->msl, desc->msl_length);
    name = doe_mtl_objc_string(runtime, desc->entrypoint, strlen(desc->entrypoint));
    if (source == NULL || name == NULL) {
        status = doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "MSL UTF-8 encoding", error);
        goto cleanup;
    }
    library = runtime->library_call(device,
        runtime->new_library, source, NULL, &native_error);
    if (library == NULL) {
        status = doe_mtl_objc_error(runtime, native_error, "newLibraryWithSource:options:error:", error);
        goto cleanup;
    }
    function = runtime->named_call(library, runtime->new_function, name);
    if (function == NULL) {
        status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "newFunctionWithName:", error);
        goto cleanup;
    }
    native_error = NULL;
    pipeline = runtime->pipeline_call(device, runtime->new_pipeline,
        function, &native_error);
    if (pipeline == NULL) {
        status = doe_mtl_objc_error(runtime, native_error,
            "newComputePipelineStateWithFunction:error:", error);
        goto cleanup;
    }
    *max_threads = doe_mtl_objc_size(runtime, pipeline, runtime->pipeline_max_threads);
    *out = pipeline;
    status = doe_mtl_fail(DOE_MTL_OK, "compute pipeline create", error);
cleanup:
    if (function != NULL) doe_mtl_objc_void(runtime, function, runtime->release);
    if (library != NULL) doe_mtl_objc_void(runtime, library, runtime->release);
    if (name != NULL) doe_mtl_objc_void(runtime, name, runtime->release);
    if (source != NULL) doe_mtl_objc_void(runtime, source, runtime->release);
    doe_mtl_objc_drain(runtime, pool);
    return status;
}
