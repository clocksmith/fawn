// SPDX-License-Identifier: MIT
// emit_csl_exec_v1.zig: Doppler execution-v1 -> HostPlan lowering.
// Ported: op table (op -> pattern, phase permission, kind), paired-gate
// check, phase/kind parsing, kernel-file -> op mapping, and step ->
// kernel/launch descriptor lowering over caller-parsed steps. NOT
// ported: JSON manifest parsing (lowerJsonToHostPlan /
// lowerManifestExecutionToHostPlan in Zig read JSON); callers supply
// parsed CslExecStep values. JSON input returns CSL_ERR_UNSUPPORTED.
#ifndef CSL_EXEC_V1_H
#define CSL_EXEC_V1_H
#include "csl_host.h"
#define CSL_EXEC_PREFILL_Q4K_GEMV_PATTERN "prefill_q4k_gemv"
typedef enum { CSL_PHASE_PREFILL, CSL_PHASE_DECODE } CslExecPhase;
typedef enum { CSL_STEP_KERNEL, CSL_STEP_OTHER } CslExecStepKind;
typedef struct {
    const char *phase; /* "prefill" or "decode" */
    const char *op; const char *kernel_key;
    const char *attention_type; /* "global"/"sliding" or NULL */
    int has_sliding_window_size; uint32_t sliding_window_size;
    const char *kv_cache_alias; uint32_t repeat;
} CslExecStep;
typedef struct { const char *op; const char *pattern; int prefill_ok; int decode_ok; } CslOpSpec;
const CslOpSpec *csl_op_to_spec(const char *op);
const char *csl_op_to_pattern(const char *op);
int csl_is_paired_gate_op(const char *op);
int csl_exec_phase_from_name(const char *name, CslExecPhase *out);
CslErr csl_lower_steps_to_host_plan(const CslExecStep *steps, size_t count, uint32_t grid_w, uint32_t grid_h, uint32_t eos_token_id, int has_eos, CslHostPlan *out_plan, CslKernelSpec *kernels, size_t kernel_cap, CslLaunchSpec *prefill, size_t prefill_cap, CslLaunchSpec *decode, size_t decode_cap);
CslErr csl_lower_json_to_host_plan(const char *json);
#endif
