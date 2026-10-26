// SPDX-License-Identifier: MIT
// See header for the partial-validation note.
#include "csl_host_plan.h"
#include "csl_emit_common.h"
#include "csl_spec.h"
#include <string.h>


const char *csl_discovery_label(CslDiscoveryMode mode) {
    return mode == CSL_DISC_EXPLICIT ? "explicit_config" : "implicit_path_lookup";
}
CslErr csl_make_cslc_plan(const char *executable, CslDiscoveryMode mode, CslCslcPlan *out) {
    if (!executable || !out) return CSL_ERR_INVALID;
    out->executable = executable;
    out->discovery = csl_discovery_label(mode);
    out->command0 = executable; out->command1 = "--version";
    out->minimum_version = "2.10.0";
    return CSL_OK;
}
CslErr csl_emit_compile_target_metadata_json(CslBuf *out, const CslCompileTarget *t) {
    CslErr e;
    if (!out || !t) return CSL_ERR_INVALID;
    PUT("{\"name\": "); e = csl_emit_json_string(out, t->name); if (e) return e;
    PUT(", \"pattern\": "); e = csl_emit_json_string(out, t->pattern); if (e) return e;
    PUT(", \"layout\": "); e = csl_emit_json_string(out, t->layout); if (e) return e;
    PUT(", \"peProgram\": "); e = csl_emit_json_string(out, t->pe_program); if (e) return e;
    PUT("}");
    return CSL_OK;
}
CslErr csl_emit_compile_params_json(CslBuf *out, const CslCompileTarget *t) {
    if (!out || !t) return CSL_ERR_INVALID;
    if (t->compile_params) return csl_buf_puts(out, t->compile_params);
    return csl_buf_puts(out, "{}");
}
CslErr csl_emit_host_plan_artifact_json(CslBuf *out, const CslHostPlan *plan, const CslCompileTarget *targets, size_t target_count, const CslCslcPlan *cslc) {
    size_t i; CslErr e;
    if (!out || !plan) return CSL_ERR_INVALID;
    PUT("{\"root\": \"\", \"schemaVersion\": 3, \"artifactKind\": \"csl_host_plan\", \"target\": \"wse3\", \"contract\": \"explicit_host_plan\", \"hostPlan\": {");
    PUT("\"peGrid\": {\"width\": "); PUTI(plan->pe_grid_width);
    PUT(", \"height\": "); PUTI(plan->pe_grid_height ? plan->pe_grid_height : 1);
    PUT("}, \"kernels\": [");
    for (i = 0; i < plan->kernel_count; i++) {
        if (i) PUT(", ");
        PUT("{\"name\": "); e = csl_emit_json_string(out, plan->kernels[i].name); if (e) return e;
        PUT(", \"pattern\": "); e = csl_emit_json_string(out, plan->kernels[i].pattern); if (e) return e;
        PUT(", \"count\": "); PUTI(plan->kernels[i].count ? plan->kernels[i].count : 1); PUT("}");
    }
    PUT("], \"phases\": {\"prefill\": [], \"decode\": []}");
    if (plan->has_eos_token_id) { PUT(", \"eosTokenId\": "); PUTI(plan->eos_token_id); }
    PUT("}, \"compileTargets\": [");
    for (i = 0; i < target_count; i++) {
        if (i) PUT(", ");
        e = csl_emit_compile_target_metadata_json(out, &targets[i]); if (e) return e;
    }
    PUT("]");
    if (cslc) {
        PUT(", \"cslc\": {\"executable\": "); e = csl_emit_json_string(out, cslc->executable); if (e) return e;
        PUT(", \"discovery\": "); e = csl_emit_json_string(out, cslc->discovery); if (e) return e;
        PUT(", \"validation\": {\"command\": ["); e = csl_emit_json_string(out, cslc->command0); if (e) return e;
        PUT(", "); e = csl_emit_json_string(out, cslc->command1); if (e) return e;
        PUT("], \"minimumVersion\": "); e = csl_emit_json_string(out, cslc->minimum_version); if (e) return e;
        PUT("}}");
    }
    PUT("}\n");
    return CSL_OK;
}
CslErr csl_validate_host_plan_artifact_json(const char *json) {
    if (!json) return CSL_ERR_INVALID;
    if (!strstr(json, "\"schemaVersion\": 3") && !strstr(json, "\"schemaVersion\":3")) return CSL_ERR_INVALID;
    if (!strstr(json, "csl_host_plan")) return CSL_ERR_INVALID;
    if (!strstr(json, "wse3")) return CSL_ERR_INVALID;
    if (!strstr(json, "explicit_host_plan")) return CSL_ERR_INVALID;
    return CSL_OK;
}
