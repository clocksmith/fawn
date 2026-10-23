/* SPDX-License-Identifier: MIT */
#ifndef DOE_SHADER_H
#define DOE_SHADER_H
#include "doe_compiler_stages.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct doe_shader doe_shader;
typedef enum doe_shader_status {
    DOE_SHADER_OK, DOE_SHADER_INVALID_ARGUMENT, DOE_SHADER_OUT_OF_MEMORY,
    DOE_SHADER_OVERFLOW, DOE_SHADER_COMPILE_FAILED
} doe_shader_status;
/* Fresh malloc-compatible storage, aligned as requested. Hooks and userdata
 * outlive every shader. Deallocate receives the original extent/alignment.
 * Hooks may be called on any owning thread; no implicit serialization. */
typedef struct doe_shader_allocator {
    void *userdata;
    void *(*allocate)(void *userdata, size_t bytes, size_t alignment);
    void (*deallocate)(void *userdata, void *memory, size_t bytes, size_t alignment);
} doe_shader_allocator;
typedef struct doe_shader_options {
    doe_shader_allocator allocator;
    DoeCompilerModuleCapacities capacities;
    DoeCompilerEntrySelectionV5 selection;
    /* Exact caller-supplied identity of loaded compiler/build and semantic
     * options. Not a self-certified hash or a cross-build compatibility claim. */
    const void *compiler_identity;
    size_t compiler_identity_size;
    /* Explicit additional typed stage pools; zero preserves compute-only admission. */
    size_t interface_attributes, stage_interfaces;
} doe_shader_options;
typedef struct doe_shader_error {
    doe_shader_status status;
    DoeCompilerStatus compiler_status;
    DoeCompilerDiagnostic diagnostic;
} doe_shader_error;
typedef struct doe_shader_view {
    DoeCompilerModuleV5 module;
    DoeCompilerModuleOutputCounts counts;
    const uint32_t *words;
    const DoeCompilerTypeV4 *types;
    const DoeCompilerMemberV4 *members;
    const DoeCompilerResourceV4 *resources;
    const DoeCompilerWorkgroupV5 *workgroups;
    const DoeCompilerEntryV5 *entries;
    const uint32_t *entry_references;
    const char *names;
    const DoeCompilerHandleV6 *handles;
    size_t handle_count;
    const DoeCompilerStageInterfaceV7 *interfaces;
    size_t interface_count;
} doe_shader_view;
/* Compiles with the one public C pipeline, owns a source/identity copy and exact
 * target/reflection spans. Outputs remain untouched on failure; error may change.
 * No source pointer survives. Workspace is freed before success publication.
 * All declared entries and each entry's static resource closure remain distinct.
 * Texture/sampler descriptors are sparse, indexed into the resource span, never
 * fabricated buffer layouts. Graphics IO is a separate typed leaf span, never
 * fabricated compute invocation inputs. This internal static-library view requires callers
 * to rebuild with this header; it is not a frozen independently loaded ABI.
 * A view borrows immutable data while the caller owns a shader reference.
 * This owns compiled data, not device pipelines, GPU work or a cache policy.
 * Runtime dispatch/texture preconditions and Metal size metadata are NOT inferred
 * from standalone reflection; that production TranslationInfo seam is unfinished. */
doe_shader_status doe_shader_create(const doe_shader_options *options,
    const char *source, size_t source_size, doe_shader **out, doe_shader_error *error);
doe_shader_status doe_shader_retain(doe_shader *shader);
void doe_shader_release(doe_shader *shader);
doe_shader_status doe_shader_borrow(const doe_shader *shader, doe_shader_view *out);
/* Exact byte equality only. This helper does not validate a compiler identity
 * supplied by the caller, hash source or equate different entry selections. */
bool doe_shader_matches(const doe_shader *shader, const char *source, size_t size,
    const void *identity, size_t identity_size,
    const DoeCompilerEntrySelectionV5 *selection);
#ifdef __cplusplus
}
#endif
#endif
