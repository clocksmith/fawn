/* SPDX-License-Identifier: MIT */
#include "objc_abi.h"

void doe_mtl_objc_load_archives(doe_mtl_objc *r, doe_objc_message message) {
    doe_mtl_objc_archives *a = &r->archives;
    a->error_call = (doe_objc_archive_error_call)message;
    a->directory_call = (doe_objc_directory_call)message;
    a->compute_call = (doe_objc_compute_descriptor_call)message;
    a->archive_class = r->get_class("MTLBinaryArchiveDescriptor");
    a->compute_class = r->get_class("MTLComputePipelineDescriptor");
    a->url_class = r->get_class("NSURL");
    a->array_class = r->get_class("NSArray");
    a->file_manager_class = r->get_class("NSFileManager");
    a->new_archive = r->selector("newBinaryArchiveWithDescriptor:error:");
    a->set_url = r->selector("setUrl:");
    a->file_url = r->selector("fileURLWithPath:");
    a->default_manager = r->selector("defaultManager");
    a->create_directory = r->selector("createDirectoryAtPath:withIntermediateDirectories:attributes:error:");
    a->array_with_object = r->selector("arrayWithObject:");
    a->set_archives = r->selector("setBinaryArchives:");
    a->add_compute = r->selector("addComputePipelineFunctionsWithDescriptor:error:");
    a->add_render = r->selector("addRenderPipelineFunctionsWithDescriptor:error:");
    a->serialize = r->selector("serializeToURL:error:");
    a->new_compute = r->selector("newComputePipelineStateWithDescriptor:options:reflection:error:");
    a->set_compute_function = r->selector("setComputeFunction:");
}

doe_mtl_status doe_mtl_objc_archive_flush(void *userdata, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    doe_mtl_objc_archives *a = &r->archives;
    if (!a->archive || !a->stats.dirty) return doe_mtl_fail(DOE_MTL_OK, "pipeline archive clean", error);
    void *pool = doe_mtl_objc_pool(r), *native_error = NULL;
    if (!pool) return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "pipeline archive autorelease pool", error);
    doe_mtl_status status = DOE_MTL_OK;
    if (a->error_call(a->archive, a->serialize, a->url, &native_error)) {
        ++a->stats.serializations;
        a->stats.dirty = false;
        status = doe_mtl_fail(DOE_MTL_OK, "pipeline archive serialize", error);
    } else {
        ++a->stats.serialization_failures;
        status = doe_mtl_objc_error(r, native_error, "serializeToURL:error:", error);
    }
    doe_mtl_objc_drain(r, pool);
    return status;
}
void doe_mtl_objc_archive_clear(doe_mtl_objc *r) {
    doe_mtl_objc_archives *a = &r->archives;
    if (a->archive) doe_mtl_objc_void(r, a->archive, r->release);
    if (a->url) doe_mtl_objc_void(r, a->url, r->release);
    a->archive = NULL; a->url = NULL;
    a->stats.enabled = false; a->stats.dirty = false;
}
void doe_mtl_objc_archive_stats(void *userdata, doe_mtl_pipeline_cache_stats *out) {
    *out = ((doe_mtl_objc *)userdata)->archives.stats;
}
static bool supported(doe_mtl_objc *r, void *device) {
    doe_mtl_objc_archives *a = &r->archives;
    return a->archive_class && a->compute_class && a->url_class && a->array_class && a->file_manager_class &&
        a->new_archive && a->set_url && a->file_url && a->default_manager && a->create_directory &&
        a->array_with_object && a->set_archives && a->add_compute && a->add_render && a->serialize &&
        a->new_compute && a->set_compute_function &&
        r->responds_call(device, r->responds, a->new_archive) &&
        r->responds_call(device, r->responds, a->new_compute);
}
doe_mtl_status doe_mtl_objc_archive_configure(void *userdata, void *device,
    const char *directory, size_t length, doe_mtl_error *error) {
    doe_mtl_objc *r = userdata;
    doe_mtl_objc_archives *a = &r->archives;
    if (!directory && !length) {
        /* Explicit disable does not write a cache the caller no longer wants. */
        doe_mtl_objc_archive_clear(r);
        return doe_mtl_fail(DOE_MTL_OK, "pipeline archive disabled", error);
    }
    if (!directory || !length || memchr(directory, 0, length))
        return doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "pipeline archive directory", error);
    if (!supported(r, device)) return doe_mtl_fail(DOE_MTL_UNSUPPORTED, "MTLBinaryArchive capability", error);
    static const char suffix[] = "/doe_pipeline_archive_v1.metallib";
    if (length > SIZE_MAX - sizeof(suffix)) return doe_mtl_fail(DOE_MTL_RANGE, "pipeline archive path", error);
    char *path = malloc(length + sizeof(suffix));
    if (!path) return doe_mtl_fail(DOE_MTL_OUT_OF_MEMORY, "pipeline archive path", error);
    memcpy(path, directory, length);
    memcpy(path + length, suffix, sizeof(suffix));
    void *pool = doe_mtl_objc_pool(r), *dir = NULL, *name = NULL, *url = NULL, *descriptor = NULL;
    void *archive = NULL, *native_error = NULL;
    doe_mtl_status status = DOE_MTL_NATIVE_FAILURE;
    if (!pool) {
        free(path);
        return doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "pipeline archive autorelease pool", error);
    }
    dir = doe_mtl_objc_string(r, directory, length);
    name = doe_mtl_objc_string(r, path, length + sizeof(suffix) - 1);
    free(path);
    if (!dir || !name) { status = doe_mtl_fail(DOE_MTL_INVALID_ARGUMENT, "pipeline archive UTF-8", error); goto cleanup; }
    void *manager = doe_mtl_objc_object(r, a->file_manager_class, a->default_manager);
    if (!manager || !a->directory_call(manager, a->create_directory, dir, (doe_objc_bool)true, NULL, &native_error)) {
        status = doe_mtl_objc_error(r, native_error, "pipeline archive directory creation", error); goto cleanup;
    }
    url = r->named_call(a->url_class, a->file_url, name); /* Autoreleased until publication. */
    descriptor = doe_mtl_objc_object(r, a->archive_class, r->new_object);
    if (!url || !descriptor) { status = doe_mtl_fail(DOE_MTL_NATIVE_FAILURE, "pipeline archive descriptor", error); goto cleanup; }
    r->set_object_call(descriptor, a->set_url, url);
    ++a->stats.load_attempts;
    archive = r->pipeline_call(device, a->new_archive, descriptor, &native_error);
    if (archive) ++a->stats.load_successes;
    else {
        /* Missing/incompatible/corrupt cache is not a shader failure. Native
         * code matching still owns every reuse; create an empty archive. */
        native_error = NULL;
        r->set_object_call(descriptor, a->set_url, NULL);
        archive = r->pipeline_call(device, a->new_archive, descriptor, &native_error);
    }
    if (!archive) { status = doe_mtl_objc_error(r, native_error, "newBinaryArchiveWithDescriptor:error:", error); goto cleanup; }
    if (!r->responds_call(archive, r->responds, a->serialize) ||
        !r->responds_call(archive, r->responds, a->add_compute) ||
        !r->responds_call(archive, r->responds, a->add_render)) {
        status = doe_mtl_fail(DOE_MTL_UNSUPPORTED, "binary archive operations", error); goto cleanup;
    }
    doe_mtl_objc_archive_clear(r);
    a->archive = archive; archive = NULL;
    a->url = doe_mtl_objc_object(r, url, r->retain);
    a->stats.enabled = true;
    status = doe_mtl_fail(DOE_MTL_OK, "pipeline archive configured", error);
cleanup:
    if (archive) doe_mtl_objc_void(r, archive, r->release);
    if (descriptor) doe_mtl_objc_void(r, descriptor, r->release);
    if (name) doe_mtl_objc_void(r, name, r->release);
    if (dir) doe_mtl_objc_void(r, dir, r->release);
    doe_mtl_objc_drain(r, pool);
    return status;
}
/* Called inside the pipeline constructor's existing autorelease pool. */
bool doe_mtl_objc_archive_attach(doe_mtl_objc *r, void *descriptor) {
    doe_mtl_objc_archives *a = &r->archives;
    if (!a->archive || !r->responds_call(descriptor, r->responds, a->set_archives)) return false;
    void *array = r->named_call(a->array_class, a->array_with_object, a->archive);
    if (!array) return false;
    r->set_object_call(descriptor, a->set_archives, array);
    return true;
}
void doe_mtl_objc_archive_add(doe_mtl_objc *r, void *descriptor, bool compute) {
    doe_mtl_objc_archives *a = &r->archives;
    void *native_error = NULL;
    if (!a->archive) return;
    if (a->error_call(a->archive, compute ? a->add_compute : a->add_render, descriptor, &native_error)) {
        ++a->stats.additions;
        a->stats.dirty = true;
    } else ++a->stats.addition_failures;
}
