/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_OBJC_ARCHIVE_TYPES_H
#define DOE_MTL_OBJC_ARCHIVE_TYPES_H
/* Included after native BOOL in objc_abi.h; all selectors retain their exact ABI. */
typedef doe_objc_bool (*doe_objc_archive_error_call)(void *, void *, void *, void **);
typedef doe_objc_bool (*doe_objc_directory_call)(void *, void *, void *, doe_objc_bool, void *, void **);
typedef void *(*doe_objc_compute_descriptor_call)(void *, void *, void *, size_t, void **, void **);
typedef struct doe_mtl_objc_archives {
    doe_objc_archive_error_call error_call;
    doe_objc_directory_call directory_call;
    doe_objc_compute_descriptor_call compute_call;
    void *archive_class, *compute_class, *url_class, *array_class, *file_manager_class;
    void *new_archive, *set_url, *file_url, *default_manager, *create_directory;
    void *array_with_object, *set_archives, *add_compute, *add_render, *serialize;
    void *new_compute, *set_compute_function;
    void *archive, *url;
    doe_mtl_pipeline_cache_stats stats;
} doe_mtl_objc_archives;
#endif
