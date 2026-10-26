// SPDX-License-Identifier: MIT
// emit_csl_simulator.zig: simulator plan/result artifacts and launcher.
// Ported: plan/result JSON with Zig field names (schemaVersion,
// artifactKind, target, contract, driver protocol/executableEnvVar/
// failClosedIfMissing/executablePath, inputs hostPlanArtifactPath/
// runtimeConfigPath/compileRootPath, compileTargets, runtime peGrid/
// prefillLaunchCount/decodeLaunchCount/weightMappingCount/
// stateBufferCount/maxDecodeTokens/timeoutMs/batchSize/eosTokenId,
// outputs stdoutPath/stderrPath/tracePath; result status
// simulator_failed/simulator_succeeded/launch_failed, termination
// exited/signal/stopped/unknown, exitCode, traceProduced), launcher
// script text that fails closed on a missing driver, and structural
// validation. NOT ported: actually executing the driver subprocess,
// and parsing a plan artifact back to a typed contract beyond the
// structural checks in csl_validate_simulator_plan_json.
#ifndef CSL_SIMULATOR_H
#define CSL_SIMULATOR_H
#include "csl_kernel.h"
typedef struct {
    const char *host_plan_path, *runtime_config_path, *compile_root_path;
    const char *driver_executable; /* NULL means env-var lookup by caller */
    int fail_closed_if_missing;
    uint32_t pe_grid_width, pe_grid_height, prefill_launch_count, decode_launch_count;
    uint32_t weight_mapping_count, state_buffer_count, max_decode_tokens, timeout_ms, batch_size;
    int has_eos_token_id; uint32_t eos_token_id;
    const char *stdout_path, *stderr_path, *trace_path;
} CslSimulatorPlan;
typedef enum { CSL_SIM_LAUNCH_FAILED, CSL_SIM_FAILED, CSL_SIM_SUCCEEDED } CslSimulatorStatus;
typedef enum { CSL_TERM_EXITED, CSL_TERM_SIGNAL, CSL_TERM_STOPPED, CSL_TERM_UNKNOWN } CslTermination;
typedef struct {
    const char *simulator_plan_path, *driver_executable;
    CslSimulatorStatus status; CslTermination termination;
    int exit_code, trace_produced;
} CslSimulatorResult;
CslErr csl_emit_simulator_plan_json(CslBuf *out, const CslSimulatorPlan *plan);
CslErr csl_emit_simulator_launcher(CslBuf *out, const CslSimulatorPlan *plan);
CslErr csl_emit_simulator_result_json(CslBuf *out, const CslSimulatorResult *result);
CslErr csl_validate_simulator_plan_json(const char *json);
CslErr csl_validate_simulator_result_json(const char *json);
CslErr csl_run_simulator_driver(const CslSimulatorPlan *plan);
#endif
