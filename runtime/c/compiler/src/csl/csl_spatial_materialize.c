// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_materialize.zig (UNTESTED).
// File and directory creation is the caller's responsibility.
// Parity NOT verified.
#include "csl_spatial_materialize.h"

#include <stdio.h>
#include <string.h>

const char *csl_spatial_materialize_compile_pattern(const char *pattern) {
    if (pattern && strcmp(pattern, "prefill_q4k_gemv") == 0) {
        return "fused_gemv_dequant";
    }
    return pattern;
}

CslSpatialScalar csl_spatial_materialize_compile_elem(
    const char *pattern, CslSpatialScalar elem) {
    if (pattern && (strcmp(pattern, "dense_gemv") == 0 ||
                    strcmp(pattern, "sample") == 0)) {
        return CSL_SPATIAL_SCALAR_F32;
    }
    return elem;
}

int csl_spatial_materialize_is_reserved_dir(const char *name) {
    return name && (strcmp(name, "compiled") == 0 ||
                    strcmp(name, "driver-logs") == 0);
}

int csl_spatial_materialize_has_kernel_name(const char *const *kernels,
                                            size_t kernel_count,
                                            const char *name) {
    size_t i;
    if (!kernels || !name) {
        return 0;
    }
    for (i = 0; i < kernel_count; i++) {
        if (kernels[i] && strcmp(kernels[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

CslErr csl_spatial_materialize_stale_dirs(
    const CslSpatialDirEntry *entries, size_t entry_count,
    const char *const *kernels, size_t kernel_count,
    const char **prune_out, size_t capacity, size_t *out_count) {
    size_t i;
    size_t kept = 0;
    if (!prune_out || !out_count) {
        return CSL_ERR_INVALID;
    }
    if (entry_count > 0 && !entries) {
        return CSL_ERR_INVALID;
    }
    *out_count = 0;
    for (i = 0; i < entry_count; i++) {
        if (!entries[i].is_dir) {
            continue;
        }
        if (csl_spatial_materialize_is_reserved_dir(entries[i].name)) {
            continue;
        }
        if (csl_spatial_materialize_has_kernel_name(kernels, kernel_count,
                                                    entries[i].name)) {
            continue;
        }
        if (kept >= capacity) {
            return CSL_ERR_TOO_LARGE;
        }
        prune_out[kept] = entries[i].name;
        kept++;
    }
    *out_count = kept;
    return CSL_OK;
}

CslErr csl_spatial_materialize_default_driver_path(const char *cwd,
                                                   const char *bundle_root,
                                                   CslBuf *out) {
    CslBuf candidate;
    CslErr err;
    FILE *probe;
    (void)bundle_root;
    if (!cwd || !out) {
        return CSL_ERR_INVALID;
    }
    /* Build the two candidates directly, without a nested helper, so */
    /* the path segments stay exactly as in Zig. */
    err = csl_buf_init(&candidate, 256);
    if (err) {
        return err;
    }
    err = csl_buf_puts(&candidate, cwd);
    if (!err) {
        err = csl_buf_puts(&candidate, "/runtime/zig/tools/csl_sdk_driver.py");
    }
    if (err) {
        csl_buf_free(&candidate);
        return err;
    }
    probe = fopen(candidate.data, "rb");
    if (probe) {
        fclose(probe);
        err = csl_buf_puts(out, candidate.data);
        csl_buf_free(&candidate);
        return err;
    }
    csl_buf_free(&candidate);
    err = csl_buf_puts(out, cwd);
    if (err) {
        return err;
    }
    return csl_buf_puts(out, "/tools/csl_sdk_driver.py");
}

CslErr csl_spatial_materialize_target_descriptor(
    const CslSpatialCompileTarget *target,
    CslSpatialTargetDescriptor *out) {
    if (!target || !out) {
        return CSL_ERR_INVALID;
    }
    out->name = target->kernel_name;
    out->base_kernel = target->has_base_kernel ? target->base_kernel
                                               : target->kernel_name;
    out->phase = target->has_phase ? target->phase : NULL;
    out->layout = target->layout_path;
    out->pe_program = target->pe_program_path;
    return CSL_OK;
}

CslErr csl_spatial_materialize_pe_metadata(const char *pe_program_source,
                                           size_t len, CslBuf *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    return csl_spatial_pe_emit_json(pe_program_source, len, out);
}

CslErr csl_spatial_materialize_layout_metadata(const char *layout_source,
                                               size_t len, CslBuf *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    return csl_spatial_pe_emit_layout_json(layout_source, len, out);
}

CslErr csl_spatial_materialize_targets_metadata(
    const CslSpatialTargetDescriptor *targets, size_t count, CslBuf *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    if (count > CSL_SPATIAL_MATERIALIZE_MAX_TARGET_DESCRIPTORS) {
        return CSL_ERR_TOO_LARGE;
    }
    return csl_spatial_pe_emit_targets_json(targets, count, out);
}

CslErr csl_spatial_materialize_emit_host_plan(void) {
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_materialize_emit_memory_plan(void) {
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_materialize_emit_runtime_config(void) {
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_materialize_emit_simulator_plan(void) {
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_materialize_emit_launcher(void) {
    return CSL_ERR_UNSUPPORTED;
}
