// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_sim_runner.zig (UNTESTED).
// Status: partial. Ported: usage text, argument parsing, driver
// resolution order, artifact path resolution, default result path,
// and the termination to result status mapping. NOT ported: simulator
// plan parsing and validation, driver subprocess execution, and
// result artifact emission, which depend on the out-of-scope
// simulator emitter. Execution itself returns CSL_ERR_UNSUPPORTED:
// this is library code and must never spawn a driver subprocess.
// No file is read or written by this module except through the
// caller's own handling of the resolved paths. Parity NOT verified.
#ifndef DOE_CSL_SPATIAL_SIM_RUNNER_H
#define DOE_CSL_SPATIAL_SIM_RUNNER_H

#include <stddef.h>

#include "csl_error.h"
#include "csl_text.h"

#define CSL_SPATIAL_SIM_MAX_PLAN_BYTES (4u * 1024u * 1024u)
#define CSL_SPATIAL_SIM_MAX_PROCESS_OUTPUT_BYTES (16u * 1024u * 1024u)
#define CSL_SPATIAL_SIM_MAX_RESULT_BYTES (8u * 1024u)

/* Environment variable name, confirmed from the local clone at */
/* d25e075: runtime/zig/src/compiler/wgsl/emit/csl/csl_spec.zig:77 */
/* gives SIMULATOR_DRIVER_ENV_VAR "DOE_CSL_SIM_EXECUTABLE". */
#define CSL_SPATIAL_SIM_DRIVER_ENV_VAR "DOE_CSL_SIM_EXECUTABLE"

typedef struct CslSpatialSimArgs {
    const char *plan_path;
    const char *driver_executable; /* NULL when absent */
    const char *result_json_path; /* NULL when absent */
    int help_shown;
} CslSpatialSimArgs;

typedef enum CslSpatialSimTermination {
    CSL_SPATIAL_SIM_EXITED = 0,
    CSL_SPATIAL_SIM_SIGNAL = 1,
    CSL_SPATIAL_SIM_STOPPED = 2,
    CSL_SPATIAL_SIM_UNKNOWN = 3
} CslSpatialSimTermination;

typedef enum CslSpatialSimStatus {
    CSL_SPATIAL_SIM_SUCCEEDED = 0,
    CSL_SPATIAL_SIM_FAILED = 1,
    CSL_SPATIAL_SIM_LAUNCH_FAILED = 2
} CslSpatialSimStatus;

/*
 * Map a child termination to (status, exit code presence), mirroring
 * makeResult: exited with code 0 succeeds, any other termination
 * fails, and only an exited child carries an exit code.
 */
typedef struct CslSpatialSimOutcome {
    CslSpatialSimStatus status;
    int has_exit_code;
    int exit_code;
} CslSpatialSimOutcome;

CslSpatialSimOutcome csl_spatial_sim_outcome(
    CslSpatialSimTermination term, int exit_code);
CslErr csl_spatial_sim_usage(CslBuf *out);
CslErr csl_spatial_sim_parse_args(const char *const *argv, int argc,
                                  CslSpatialSimArgs *out);

/*
 * Resolve an artifact path against the plan file's directory: an
 * absolute raw path passes through unchanged, otherwise the result
 * is "<plan dir>/<raw>". A plan path with no directory part uses
 * "." as its directory, matching Zig dirname fallback. The result
 * is written into `out`; the caller owns it.
 */
CslErr csl_spatial_sim_resolve_artifact_path(const char *plan_path,
                                             const char *raw_path,
                                             CslBuf *out);

/* Default result path: "<trace path>.result.json". */
CslErr csl_spatial_sim_default_result_path(const char *trace_path,
                                           CslBuf *out);

/*
 * Driver resolution order, as in Zig: 1. explicit --driver-executable
 * flag, 2. the plan's driver executable path (resolved against the
 * plan directory when relative), 3. $DOE_CSL_SIM_EXECUTABLE. When no
 * source yields a value, returns CSL_ERR_INVALID (the Zig source
 * distinguishes MissingDriverExecutable; the shared enum maps it
 * here). env_value is the caller's read of the environment variable,
 * or NULL when unset; this module does not read the environment
 * itself, so the caller controls that boundary. All result strings
 * are written into `out`; the inputs are borrowed.
 */
CslErr csl_spatial_sim_resolve_driver(const char *plan_path,
                                      const char *cli_value,
                                      const char *plan_value,
                                      const char *env_value, CslBuf *out);

/* Filesystem check backing the trace_produced field of a result: */
/* returns 1 when the trace path can be opened for reading, else 0. */
/* The caller combines this with csl_spatial_sim_outcome. */
int csl_spatial_sim_trace_exists(const char *path);

/* NOT PORTED: plan parsing, driver execution, result emission. */
CslErr csl_spatial_sim_run(const CslSpatialSimArgs *args);
CslErr csl_spatial_sim_validate_plan(const char *plan_bytes, size_t len);

#endif
