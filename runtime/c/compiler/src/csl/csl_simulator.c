// SPDX-License-Identifier: MIT
// See header: driver execution is deliberately not done here.
#include "csl_simulator.h"
#include "csl_emit_common.h"
#include <string.h>


static const char *status_name(CslSimulatorStatus s) {
    switch (s) {
        case CSL_SIM_LAUNCH_FAILED: return "launch_failed";
        case CSL_SIM_FAILED: return "simulator_failed";
        default: return "simulator_succeeded";
    }
}
static const char *term_name(CslTermination t) {
    switch (t) {
        case CSL_TERM_SIGNAL: return "signal";
        case CSL_TERM_STOPPED: return "stopped";
        case CSL_TERM_UNKNOWN: return "unknown";
        default: return "exited";
    }
}
CslErr csl_emit_simulator_plan_json(CslBuf *out, const CslSimulatorPlan *plan) {
    CslErr e;
    if (!out || !plan) return CSL_ERR_INVALID;
    PUT("{\"schemaVersion\": 2, \"artifactKind\": \"csl_simulator_plan\", \"target\": \"wse3\", \"contract\": \"explicit_simulator_launch\", ");
    PUT("\"driver\": {\"protocol\": \"doe.csl.simulator/v1\", \"executableEnvVar\": \"DOE_CSL_SIM_EXECUTABLE\", \"failClosedIfMissing\": ");
    { CslErr e2 = csl_buf_puts(out, plan->fail_closed_if_missing ? "true" : "false"); if (e2) return e2; }
    if (plan->driver_executable) { PUT(", \"executablePath\": "); e = csl_emit_json_string(out, plan->driver_executable); if (e) return e; }
    PUT("}, \"inputs\": {\"hostPlanArtifactPath\": "); e = csl_emit_json_string(out, plan->host_plan_path); if (e) return e;
    PUT(", \"runtimeConfigPath\": "); e = csl_emit_json_string(out, plan->runtime_config_path); if (e) return e;
    PUT(", \"compileRootPath\": "); e = csl_emit_json_string(out, plan->compile_root_path); if (e) return e;
    PUT("}, \"compileTargets\": [], \"runtime\": {\"peGrid\": {\"width\": "); PUTI(plan->pe_grid_width);
    PUT(", \"height\": "); PUTI(plan->pe_grid_height);
    PUT("}, \"prefillLaunchCount\": "); PUTI(plan->prefill_launch_count);
    PUT(", \"decodeLaunchCount\": "); PUTI(plan->decode_launch_count);
    PUT(", \"weightMappingCount\": "); PUTI(plan->weight_mapping_count);
    PUT(", \"stateBufferCount\": "); PUTI(plan->state_buffer_count);
    PUT(", \"maxDecodeTokens\": "); PUTI(plan->max_decode_tokens);
    PUT(", \"timeoutMs\": "); PUTI(plan->timeout_ms);
    PUT(", \"batchSize\": "); PUTI(plan->batch_size ? plan->batch_size : 1);
    if (plan->has_eos_token_id) { PUT(", \"eosTokenId\": "); PUTI(plan->eos_token_id); }
    PUT("}, \"outputs\": {\"stdoutPath\": "); e = csl_emit_json_string(out, plan->stdout_path); if (e) return e;
    PUT(", \"stderrPath\": "); e = csl_emit_json_string(out, plan->stderr_path); if (e) return e;
    PUT(", \"tracePath\": "); e = csl_emit_json_string(out, plan->trace_path); if (e) return e;
    PUT("}}\n");
    return CSL_OK;
}
CslErr csl_emit_simulator_launcher(CslBuf *out, const CslSimulatorPlan *plan) {
    if (!out || !plan) return CSL_ERR_INVALID;
    PUT("#!/bin/sh\n# CSL simulator launcher: fails closed on a missing driver.\n");
    PUT("DRIVER=\"${DOE_CSL_SIM_EXECUTABLE:-");
    if (plan->driver_executable) PUT(plan->driver_executable);
    PUT("}\"\n");
    PUT("if [ -z \"$DRIVER\" ]; then echo \"Missing DOE_CSL_SIM_EXECUTABLE\" >&2; exit 1; fi\n");
    PUT("# driver_executable=$DRIVER\n# (invoking the driver is the caller's job; this script only resolves it.)\n");
    return CSL_OK;
}
CslErr csl_emit_simulator_result_json(CslBuf *out, const CslSimulatorResult *r) {
    CslErr e;
    if (!out || !r) return CSL_ERR_INVALID;
    PUT("{\"schemaVersion\": 1, \"artifactKind\": \"csl_simulator_result\", \"target\": \"wse3\", \"contract\": \"explicit_simulator_outcome\", \"simulatorPlanPath\": ");
    e = csl_emit_json_string(out, r->simulator_plan_path); if (e) return e;
    PUT(", \"driverExecutable\": "); e = csl_emit_json_string(out, r->driver_executable); if (e) return e;
    PUT(", \"status\": "); e = csl_emit_json_string(out, status_name(r->status)); if (e) return e;
    PUT(", \"termination\": "); e = csl_emit_json_string(out, term_name(r->termination)); if (e) return e;
    PUT(", \"exitCode\": "); PUTI(r->exit_code);
    PUT(", \"traceProduced\": "); { CslErr e2 = csl_buf_puts(out, r->trace_produced ? "true" : "false"); if (e2) return e2; }
    PUT("}\n");
    return CSL_OK;
}
CslErr csl_validate_simulator_plan_json(const char *json) {
    if (!json) return CSL_ERR_INVALID;
    if (!strstr(json, "csl_simulator_plan")) return CSL_ERR_INVALID;
    if (!strstr(json, "explicit_simulator_launch")) return CSL_ERR_INVALID;
    return CSL_OK;
}
CslErr csl_validate_simulator_result_json(const char *json) {
    if (!json) return CSL_ERR_INVALID;
    if (!strstr(json, "csl_simulator_result")) return CSL_ERR_INVALID;
    if (!strstr(json, "explicit_simulator_outcome")) return CSL_ERR_INVALID;
    return CSL_OK;
}
CslErr csl_run_simulator_driver(const CslSimulatorPlan *plan) {
    (void)plan; return CSL_ERR_UNSUPPORTED;
}
