/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_TARGET_METAL_H
#define DOE_COMPILER_TARGET_METAL_H
#include "../scratch.h"
#include "../../include/doe_compiler_metal.h"

/* Private target adapter, not a public ABI. Borrow a fully validated module
 * until return. No IR mutation, allocation, retained pointers or Zig dependency.
 * The caller supplies exclusive scratch sized by doe_target_scratch_bytes.
 * selected_entry is NO_ID for all entries or an entry index. Output is an exact
 * byte span (not NUL terminated). NULL/zero measures. Failure leaves bytes alone.
 * Source, IR and result must not overlap output or change during the call.
 * Graphics uses the shared typed stage workspace. Invariant output remains
 * unsupported until the native preserve-invariance compilation policy is qualified. */
enum { DOE_METAL_SIZES_SLOT = DOE_COMPILER_METAL_SIZES_SLOT, DOE_METAL_BINDINGS_PER_GROUP = 16,
       DOE_METAL_TEXTURE_SLOTS = 31, DOE_METAL_SAMPLER_SLOTS = 16,
       DOE_METAL_MAX_OUTPUT = 1024 * 1024, DOE_METAL_MAX_DEPTH = 96 };
typedef struct {
    size_t required_bytes, written_bytes;
    uint32_t entry_count;
    bool needs_sizes_buffer;
} DoeMetalResult;
DoeCompilerStatus doe_metal_emit(Source *source, const CompilerWorkspace *module,
                                 uint32_t selected_entry, DoeTargetScratch scratch, char *output,
                                 size_t capacity, DoeMetalResult *result);
typedef struct {
    DoeCompilerMetalBindingV12 *bindings;
    size_t binding_capacity, binding_count;
    DoeCompilerMetalEntryV12 *entries;
    size_t entry_capacity, entry_count;
} DoeMetalMetadata;
DoeCompilerStatus doe_metal_emit_dense(Source *, const CompilerWorkspace *, uint32_t,
    DoeTargetScratch, const DoeMetalMetadata *, char *, size_t, DoeMetalResult *);
bool doe_metal_binding_slot(uint32_t group, uint32_t binding, uint32_t *slot);
#endif
