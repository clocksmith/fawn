// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_tool.zig (UNTESTED).
// Status: partial. Ported: argument parsing and validation, phase
// specialized target expansion, dense GEMV hidden tile expansion,
// compile parameter computation for every pattern branch in the Zig
// source, state buffer kind mapping, and the max feature dimension
// helper. NOT ported: execution JSON lowering (manifest and steps
// modes), host plan / memory plan / runtime config / simulator plan /
// launcher emission, and cslc plan construction, all of which depend on
// out-of-scope emitters. Those entry points return CSL_ERR_UNSUPPORTED.
// No file or directory is created by this module; the caller writes
// artifacts. Parity is NOT verified.
#ifndef DOE_CSL_SPATIAL_HOST_PLAN_TOOL_H
#define DOE_CSL_SPATIAL_HOST_PLAN_TOOL_H

#include <stddef.h>
#include <stdint.h>

#include "csl_error.h"
#include "csl_text.h"
#include "csl_spatial_bindings.h"
#include "csl_spatial_host_plan_tool_config.h"

#define CSL_SPATIAL_TOOL_MAX_KERNELS 64
#define CSL_SPATIAL_TOOL_MAX_COMPILE_TARGETS 76
#define CSL_SPATIAL_TOOL_MAX_PARAMS 16
#define CSL_SPATIAL_TOOL_NAME_LEN 256
#define CSL_SPATIAL_TOOL_PHASE_LEN 64

typedef enum CslSpatialToolMode {
    CSL_SPATIAL_TOOL_MODE_MANIFEST = 0,
    CSL_SPATIAL_TOOL_MODE_STEPS = 1
} CslSpatialToolMode;

/*
 * Borrowed argv strings; the caller must keep argv alive while using
 * these. Zig duplicates the strings; this port borrows them to avoid
 * hidden allocation, and says so here.
 */
typedef struct CslSpatialToolArgs {
    const char *input_path;
    const char *output_path; /* NULL when bundle_root is used */
    const char *bundle_root; /* NULL when output_path is used */
    CslSpatialToolMode mode;
    const char *cslc_executable; /* NULL when absent */
    const char *driver_executable_path; /* NULL when absent */
    int help_shown;
} CslSpatialToolArgs;

typedef struct CslSpatialKernelSpec {
    const char *name;
    const char *pattern;
} CslSpatialKernelSpec;

typedef struct CslSpatialHostPlan {
    uint32_t pe_grid_width;
    uint32_t pe_grid_height;
    const CslSpatialKernelSpec *kernels;
    size_t kernel_count;
} CslSpatialHostPlan;

typedef struct CslSpatialCompileParam {
    const char *name; /* static literal */
    uint32_t value;
} CslSpatialCompileParam;

typedef struct CslSpatialCompileTarget {
    char kernel_name[CSL_SPATIAL_TOOL_NAME_LEN];
    char layout_path[CSL_SPATIAL_TOOL_NAME_LEN];
    char pe_program_path[CSL_SPATIAL_TOOL_NAME_LEN];
    int has_base_kernel;
    char base_kernel[CSL_SPATIAL_TOOL_NAME_LEN];
    int has_phase;
    char phase[CSL_SPATIAL_TOOL_PHASE_LEN];
    int has_metadata;
    char metadata_phase[CSL_SPATIAL_TOOL_PHASE_LEN];
    CslSpatialBinding bindings[CSL_SPATIAL_BINDINGS_MAX];
    size_t binding_count;
    CslSpatialCompileParam params[CSL_SPATIAL_TOOL_MAX_PARAMS];
    size_t param_count;
} CslSpatialCompileTarget;

typedef enum CslSpatialMemBufferKind {
    CSL_SPATIAL_MEM_KV_CACHE = 0,
    CSL_SPATIAL_MEM_DECODE_POSITION = 1,
    CSL_SPATIAL_MEM_SLIDING_WINDOW = 2,
    CSL_SPATIAL_MEM_ACTIVATION_SCRATCH = 3,
    CSL_SPATIAL_MEM_OUTPUT_LOGITS = 4,
    CSL_SPATIAL_MEM_OTHER = 5
} CslSpatialMemBufferKind;

typedef enum CslSpatialStateKind {
    CSL_SPATIAL_STATE_KV_CACHE = 0,
    CSL_SPATIAL_STATE_POSITION = 1,
    CSL_SPATIAL_STATE_SCRATCH = 2
} CslSpatialStateKind;

typedef struct CslSpatialMemBuffer {
    const char *name;
    CslSpatialMemBufferKind kind;
    uint64_t bytes_per_pe;
} CslSpatialMemBuffer;

typedef struct CslSpatialStateBuffer {
    const char *name;
    CslSpatialStateKind kind;
    uint64_t bytes_per_pe;
} CslSpatialStateBuffer;

CslErr csl_spatial_tool_parse_args(const char *const *argv, int argc,
                                   CslSpatialToolArgs *out);
CslErr csl_spatial_tool_usage(CslBuf *out);
int csl_spatial_tool_is_phase_specialized(const char *name);
int csl_spatial_tool_is_lm_head_target(const char *name);
CslErr csl_spatial_tool_max_feature_dim(
    const CslSpatialModelConfig *config, uint32_t *out);

/*
 * Compute compile parameters for one target. config may be NULL, in
 * which case the parameter list is empty (matching Zig, which returns
 * an empty slice without a model config). phase may be NULL for a base
 * target or point at "prefill" or "decode". max_rect_dim is the
 * caller supplied csl_spec.MAX_RECT_DIM value (see the dense GEMV
 * header); it feeds the dense GEMV width and height computations.
 * A division by a zero grid dimension returns CSL_ERR_INVALID; the
 * Zig source would panic there, so this port fails visibly instead.
 * An mrope section whose parts do not sum to num_pairs returns
 * CSL_ERR_INVALID, matching Zig InvalidMRopeSection.
 */
/* Dense GEMV compile parameters for a given tile width. Used by the */
/* target builder for hidden tile targets; also used by the dense */
/* GEMV branch of compile_params. */
CslErr csl_spatial_tool_dense_params(
    const CslSpatialModelConfig *config, uint32_t width,
    uint32_t max_rect_dim, CslSpatialCompileParam *out, size_t capacity,
    size_t *out_count);

CslErr csl_spatial_tool_compile_params(
    const CslSpatialModelConfig *config, const CslSpatialHostPlan *plan,
    const char *target_name, const char *pattern, const char *phase,
    uint32_t max_rect_dim, CslSpatialCompileParam *out, size_t capacity,
    size_t *out_count);

/*
 * Expand a host plan's kernels into compile targets, including phase
 * variants and dense GEMV hidden tiles. The caller array must hold at
 * least CSL_SPATIAL_TOOL_MAX_COMPILE_TARGETS entries. Exceeding the
 * target budget returns CSL_ERR_TOO_LARGE; the Zig source panics
 * there, so this port fails visibly instead.
 */
CslErr csl_spatial_tool_build_targets(
    const CslSpatialHostPlan *plan, const CslSpatialModelConfig *config,
    CslSpatialScalar activation_elem, uint32_t max_rect_dim,
    CslSpatialCompileTarget *out, size_t capacity, size_t *out_count);

/* Map memory plan buffers to runtime state buffers, skipping kinds */
/* with no state mapping, exactly as in Zig. */
CslErr csl_spatial_tool_build_state_buffers(
    const CslSpatialMemBuffer *buffers, size_t count,
    CslSpatialStateBuffer *out, size_t capacity, size_t *out_count);

/* NOT PORTED: execution JSON lowering plus artifact emission. */
CslErr csl_spatial_tool_run_from_execution_json(
    const CslSpatialToolArgs *args, const char *execution_json,
    size_t len);
CslErr csl_spatial_tool_make_cslc_plan(const char *cslc_executable);

#endif
