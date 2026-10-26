// SPDX-License-Identifier: MIT
// emit_csl_host_plan.zig: schema-backed HostPlan artifact.
// Field names/order below follow the Zig JSON emitters as read:
// root, schemaVersion, artifactKind, target, contract, hostPlan,
// compileTargets, peGrid, width, height, kernels, phases, eosTokenId,
// and per-target name, pattern, layout, peProgram, compileParams,
// executable, discovery, validation, command, minimumVersion.
// Full JSON-schema validation is NOT ported: validateHostPlanArtifact
// checks only the structural shape (root fields present, schema version
// 3, target wse3, contract explicit_host_plan) and returns
// CSL_ERR_UNSUPPORTED for deeper schema failures. Discovery labels:
// _prefill / _decode phase suffixes and specialized kernels are
// recorded in the phase/kernels fields, not re-derived here.
#ifndef CSL_HOST_PLAN_H
#define CSL_HOST_PLAN_H
#include "csl_host.h"
typedef struct {
    const char *name; const char *pattern; const char *layout; const char *pe_program;
    const char *compile_params; /* JSON fragment or NULL */
} CslCompileTarget;
typedef struct {
    const char *name; const char *pattern;
    const char *binding_name; const char *shape; const char *transform;
} CslBindingMetadata;
typedef struct {
    const char *executable; const char *discovery; /* explicit_config / implicit_path_lookup */
    const char *command0; const char *command1; const char *minimum_version;
} CslCslcPlan;
typedef enum { CSL_DISC_EXPLICIT, CSL_DISC_IMPLICIT } CslDiscoveryMode;
const char *csl_discovery_label(CslDiscoveryMode mode);
CslErr csl_make_cslc_plan(const char *executable, CslDiscoveryMode mode, CslCslcPlan *out);
CslErr csl_emit_compile_target_metadata_json(CslBuf *out, const CslCompileTarget *t);
CslErr csl_emit_compile_params_json(CslBuf *out, const CslCompileTarget *t);
CslErr csl_emit_host_plan_artifact_json(CslBuf *out, const CslHostPlan *plan, const CslCompileTarget *targets, size_t target_count, const CslCslcPlan *cslc);
CslErr csl_validate_host_plan_artifact_json(const char *json);
#endif
