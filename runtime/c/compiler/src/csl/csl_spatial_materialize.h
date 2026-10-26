// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_materialize.zig (UNTESTED).
// Status: partial. Ported: capacity constants, compile source pattern
// and element mappings, reserved directory and kernel name rules, the
// stale compile directory decision, the default driver path rule, and
// pe_program/layout metadata emission into a caller buffer (via the
// csl_spatial_pe_metadata module). NOT ported: host plan, memory plan,
// runtime config, simulator plan, and launcher emission, which depend
// on out-of-scope emitters and return CSL_ERR_UNSUPPORTED. File and
// directory creation is the caller's responsibility: this module never
// creates a file or directory, it only decides what to prune and what
// text to emit, and the caller writes and deletes. Parity NOT verified.
#ifndef DOE_CSL_SPATIAL_MATERIALIZE_H
#define DOE_CSL_SPATIAL_MATERIALIZE_H

#include <stddef.h>
#include <stdint.h>

#include "csl_error.h"
#include "csl_text.h"
#include "csl_spatial_bindings.h"
#include "csl_spatial_host_plan_tool.h"
#include "csl_spatial_pe_metadata.h"

#define CSL_SPATIAL_MATERIALIZE_HOST_PLAN_CAPACITY (128u * 1024u)
#define CSL_SPATIAL_MATERIALIZE_RUNTIME_CONFIG_CAPACITY (128u * 1024u)
#define CSL_SPATIAL_MATERIALIZE_MEMORY_PLAN_CAPACITY (64u * 1024u)
#define CSL_SPATIAL_MATERIALIZE_SIMULATOR_PLAN_CAPACITY (64u * 1024u)
#define CSL_SPATIAL_MATERIALIZE_LAUNCHER_CAPACITY (8u * 1024u)
#define CSL_SPATIAL_MATERIALIZE_PE_METADATA_CAPACITY (16u * 1024u)
#define CSL_SPATIAL_MATERIALIZE_MAX_TARGET_DESCRIPTORS 128
#define CSL_SPATIAL_MATERIALIZE_COMPILE_ROOT "compile"

/* "prefill_q4k_gemv" maps to "fused_gemv_dequant"; every other */
/* pattern maps to itself. The result borrows the input when it is */
/* unchanged, or points at a static literal for the mapped case. */
const char *csl_spatial_materialize_compile_pattern(const char *pattern);

/*
 * Element for a compile source: dense_gemv and sample always use f32;
 * every other pattern keeps the caller's element.
 */
CslSpatialScalar csl_spatial_materialize_compile_elem(
    const char *pattern, CslSpatialScalar elem);

/* Reserved compile subdirectories that are never pruned. */
int csl_spatial_materialize_is_reserved_dir(const char *name);

/* True when name matches one of the plan's kernel names. */
int csl_spatial_materialize_has_kernel_name(const char *const *kernels,
                                            size_t kernel_count,
                                            const char *name);

/*
 * Stale directory decision, mirroring pruneStaleCompileKernelDirs.
 * entries are the compile root's children; only entries with
 * is_dir set, that are not reserved and do not match a kernel name,
 * are reported in prune_out (borrowed pointers, in entry order).
 * The caller performs the actual deletion; this function deletes
 * nothing. Non-directory entries (for example targets.metadata.json)
 * are never reported.
 */
typedef struct CslSpatialDirEntry {
    const char *name;
    int is_dir;
} CslSpatialDirEntry;

CslErr csl_spatial_materialize_stale_dirs(
    const CslSpatialDirEntry *entries, size_t entry_count,
    const char *const *kernels, size_t kernel_count,
    const char **prune_out, size_t capacity, size_t *out_count);

/*
 * Default driver path rule: "<cwd>/runtime/zig/tools/csl_sdk_driver.py"
 * when that file can be opened for reading, otherwise
 * "<cwd>/tools/csl_sdk_driver.py" (returned unchecked, exactly as in
 * Zig). bundle_root is accepted for signature parity with Zig and is
 * ignored, exactly as in Zig. This only reads for an existence check;
 * it creates nothing.
 */
CslErr csl_spatial_materialize_default_driver_path(const char *cwd,
                                                   const char *bundle_root,
                                                   CslBuf *out);

/*
 * Convert one compile target into a targets.metadata.json descriptor,
 * mirroring materializeTargetsMetadata: name is the target kernel
 * name, base kernel is the target base kernel or the kernel name when
 * absent, phase is the target phase or NULL, layout and pe program
 * are the target paths. All descriptor strings borrow the target's
 * buffers, so the target must outlive the descriptor.
 */
CslErr csl_spatial_materialize_target_descriptor(
    const CslSpatialCompileTarget *target,
    CslSpatialTargetDescriptor *out);

/* Metadata emission into a caller buffer; the caller writes the file. */
CslErr csl_spatial_materialize_pe_metadata(const char *pe_program_source,
                                           size_t len, CslBuf *out);
CslErr csl_spatial_materialize_layout_metadata(const char *layout_source,
                                               size_t len, CslBuf *out);
CslErr csl_spatial_materialize_targets_metadata(
    const CslSpatialTargetDescriptor *targets, size_t count, CslBuf *out);

/* NOT PORTED: artifact emission depending on out-of-scope emitters. */
CslErr csl_spatial_materialize_emit_host_plan(void);
CslErr csl_spatial_materialize_emit_memory_plan(void);
CslErr csl_spatial_materialize_emit_runtime_config(void);
CslErr csl_spatial_materialize_emit_simulator_plan(void);
CslErr csl_spatial_materialize_emit_launcher(void);

#endif
