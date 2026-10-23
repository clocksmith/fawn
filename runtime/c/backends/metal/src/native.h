/* SPDX-License-Identifier: MIT */
#ifndef DOE_MTL_NATIVE_H
#define DOE_MTL_NATIVE_H
#include "../include/doe_metal.h"

/* Private actual-platform call boundary. Tests substitute these calls only;
 * the production constructor has no injectable/public fake-runtime option.
 * Native objects are opaque Objective-C id handles. Constructors return +1.
 * Native errors must fill the supplied diagnostic; successful calls clear it. */
typedef struct doe_mtl_native_binding {
    void *buffer;
    size_t offset;
    uint32_t size;
} doe_mtl_native_binding;

typedef struct doe_mtl_native {
    void *userdata;
    void (*close)(void *userdata);
    void (*release)(void *userdata, void *object);
    doe_mtl_status (*device)(void *userdata, void **out, doe_mtl_caps *caps,
        doe_mtl_error *error);
    doe_mtl_status (*queue)(void *userdata, void *device, void **out,
        doe_mtl_error *error);
    doe_mtl_status (*buffer)(void *userdata, void *device, size_t length,
        doe_mtl_memory memory, void **out, doe_mtl_error *error);
    void *(*contents)(void *userdata, void *buffer);
    doe_mtl_status (*pipeline)(void *userdata, void *device,
        const doe_mtl_pipeline_desc *desc, void **out, size_t *max_threads,
        doe_mtl_error *error);
    doe_mtl_status (*command)(void *userdata, void *queue, void **out,
        doe_mtl_error *error);
    doe_mtl_status (*copy)(void *userdata, void *command, void *source,
        size_t source_offset, void *destination, size_t destination_offset,
        size_t length, doe_mtl_error *error);
    doe_mtl_status (*fill)(void *userdata, void *command, void *buffer,
        size_t offset, size_t length, uint8_t value, doe_mtl_error *error);
    doe_mtl_status (*dispatch)(void *userdata, void *command, void *pipeline,
        const doe_mtl_native_binding *bindings, uint32_t sizes_slot,
        doe_mtl_dimensions groups, doe_mtl_dimensions workgroup,
        uint32_t repeat, doe_mtl_error *error);
    void (*commit)(void *userdata, void *command);
    /* Returns native MTLCommandBufferStatus, retaining exact NSError.code. */
    size_t (*observe)(void *userdata, void *command, bool wait, int64_t *code);
} doe_mtl_native;

doe_mtl_status doe_mtl_native_open(doe_mtl_native *out, doe_mtl_error *error);
/* Takes the native loader obligation on all paths, only used by owner/tests. */
doe_mtl_status doe_mtl_context_open(doe_mtl_native native,
    doe_mtl_context **out, doe_mtl_error *error);
doe_mtl_status doe_mtl_fail(doe_mtl_status status, const char *operation,
    doe_mtl_error *error);
#endif
