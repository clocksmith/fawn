/* SPDX-License-Identifier: MIT */
#ifndef DOE_SHADER_H
#define DOE_SHADER_H
#include "doe_compiler_diagnostics.h"
#include "doe_compiler_hlsl.h"
#include "doe_compiler_metal.h"
#include "doe_compiler_specialization.h"
#include "doe_compiler_metal_pipeline.h"
#include "doe_compiler_translation.h"
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
typedef enum doe_shader_target {
    DOE_SHADER_SPIRV = 0, DOE_SHADER_METAL = 1, DOE_SHADER_HLSL = 2
} doe_shader_target;
typedef struct doe_shader_options {
    doe_shader_target target; /* Zero preserves the SPIR-V default. No fallback. */
    doe_shader_allocator allocator;
    DoeCompilerModuleCapacities capacities;
    DoeCompilerEntrySelectionV5 selection;
    /* Exact caller-supplied identity of loaded compiler/build and semantic
     * options. Not a self-certified hash or a cross-build compatibility claim. */
    const void *compiler_identity;
    size_t compiler_identity_size;
    /* Explicit additional typed stage pools; zero preserves compute-only admission. */
    size_t interface_attributes, stage_interfaces;
    /* Only compiler semantic bits, never output/measure flags. Zero preserves
     * conservative standalone defaults. Policy participates in artifact identity. */
    uint32_t semantic_flags;
    size_t sampling_pairs; /* Explicit ABI16 specialization staging capacity. */
    /* Explicit opt-in: retry only compiler-reported pool exhaustion through the
     * canonical growth policy. False preserves all caller-supplied hard pools.
     * Source bound and compiler ABI depth/representation limits never expand. */
    bool grow_pools;
} doe_shader_options;
typedef struct doe_shader_error {
    doe_shader_status status;
    DoeCompilerStatus compiler_status;
    DoeCompilerDiagnostic diagnostic;
} doe_shader_error;
typedef struct doe_shader_view {
    doe_shader_target target;
    uint32_t target_language_version;
    uint32_t metal_sample_mask; /* UINT32_MAX for ordinary artifacts; ABI15 fragment coverage otherwise. */
    const char *text; /* Exact owned bytes, not NUL terminated; NULL for SPIR-V. */
    size_t text_size;
    const DoeCompilerTextBindingV10 *target_bindings;
    size_t target_binding_count;
    const DoeCompilerTextEntryV10 *target_entries;
    size_t target_entry_count;
    /* HLSL has explicit register namespaces and dispatch requirements, never
     * reinterpreted Metal slot/size records. Other targets leave these empty. */
    const DoeCompilerHlslBindingV11 *hlsl_bindings;
    size_t hlsl_binding_count;
    const DoeCompilerHlslEntryV11 *hlsl_entries;
    size_t hlsl_entry_count;
    /* ABI12 Metal rows belong to one exact entry, never a global resource map.
     * Legacy ABI10 target spans remain empty for these owned artifacts. */
    const DoeCompilerMetalBindingV12 *metal_bindings;
    size_t metal_binding_count;
    const DoeCompilerMetalEntryV12 *metal_entries;
    size_t metal_entry_count;
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
    const DoeCompilerSamplingPairV16 *sampling_pairs;
    size_t sampling_pair_count;
    const DoeCompilerTranslationEntryV17 *translation_entries;
    size_t translation_entry_count;
    bool translation_complete; /* False for legacy artifacts. */
    bool sampling_pairs_complete; /* False for legacy artifacts without ABI16 metadata. */
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
 * ABI8 matrix reflection reuses the typed records with an explicit matrix kind.
 * Valid entryless/library modules own zero words and zero entries; they are not
 * Vulkan shaders and cannot create a pipeline without a selected stage.
 * This owns compiled data, not device pipelines, GPU work or a cache policy.
 * Explicit Metal artifacts own compiler-produced target slots and per-entry size
 * metadata. The runtime supplies actual bound byte lengths; no layout/slot policy
 * is recalculated here. Other production TranslationInfo preconditions remain
 * unfinished. Different target selections never share artifact identity. */
doe_shader_status doe_shader_create(const doe_shader_options *options,
    const char *source, size_t source_size, doe_shader **out, doe_shader_error *error);
/* Module-only handles own symbolic whole-source validation metadata, never
 * executable code/layouts. options.selection must be NONE. Explicit override
 * storage is an ABI13 compiler capacity, not a default or hidden allocation. */
typedef struct doe_shader_module_view {
    doe_shader_target target;
    uint32_t semantic_flags;
    const DoeCompilerOverrideV13 *overrides;
    size_t override_count;
    const DoeCompilerModuleEntryV13 *entries;
    size_t entry_count;
    const char *names;
    size_t name_bytes;
} doe_shader_module_view;
doe_shader_status doe_shader_create_module(const doe_shader_options *options,
    size_t override_capacity, const char *source, size_t source_size,
    doe_shader **out, doe_shader_error *error);
doe_shader_status doe_shader_borrow_module(const doe_shader *module, doe_shader_module_view *out);
/* Specialize the immutable saved module/options, with exact key bytes and double
 * value bits snapshotted before measure/fill. AUTO/NAME produces a separately
 * owned executable artifact with one entry at index0 and its declared identity.
 * Failure leaves *out unchanged. Allocator userdata outlives both handles. */
doe_shader_status doe_shader_specialize(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t value_count,
    doe_shader **out, doe_shader_error *error);
/* Conservative exact identity: same immutable module instance, selection and
 * ordered key bytes/value representations. No semantic or reordered equivalence. */
bool doe_shader_specialization_matches(const doe_shader *shader, const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t value_count);
/* Metal fragment pipeline policy is snapshotted before allocator callbacks and
 * participates in exact artifact identity. Ordinary matching accepts only the
 * UINT32_MAX policy; the explicit matcher includes all 32 mask bits. */
doe_shader_status doe_shader_specialize_metal_pipeline(const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t value_count,
    const DoeCompilerMetalPipelineOptionsV15 *, doe_shader **out, doe_shader_error *error);
bool doe_shader_metal_pipeline_matches(const doe_shader *shader, const doe_shader *module,
    const DoeCompilerEntrySelectionV5 *selection,
    const DoeCompilerOverrideValueV13 *values, size_t value_count,
    const DoeCompilerMetalPipelineOptionsV15 *);
doe_shader_status doe_shader_retain(doe_shader *shader);
void doe_shader_release(doe_shader *shader);
/* Module-only borrow rejects without touching *out. */
doe_shader_status doe_shader_borrow(const doe_shader *shader, doe_shader_view *out);
/* Exact byte equality only. This helper does not validate a compiler identity
 * supplied by the caller, hash source or equate different entry selections or
 * semantic flags. Target/semantic policy must match independently of identity bytes. */
/* Immutable module notices, borrowed while this shader is retained. Specialized
 * artifacts retain their module. NULL/direct artifacts return NULL and count=0. */
const DoeCompilerNoticeV18 *doe_shader_get_diagnostics(const doe_shader *, size_t *count);

bool doe_shader_matches(const doe_shader *shader, doe_shader_target target, uint32_t semantic_flags,
    const char *source, size_t size,
    const void *identity, size_t identity_size,
    const DoeCompilerEntrySelectionV5 *selection);
#ifdef __cplusplus
}
#endif
#endif
