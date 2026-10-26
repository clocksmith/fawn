// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_sim_runner.zig (UNTESTED).
// This module never spawns a subprocess. Parity NOT verified.
#include "csl_spatial_sim_runner.h"

#include <stdio.h>
#include <string.h>

CslSpatialSimOutcome csl_spatial_sim_outcome(
    CslSpatialSimTermination term, int exit_code) {
    CslSpatialSimOutcome out;
    out.has_exit_code = 0;
    out.exit_code = 0;
    if (term == CSL_SPATIAL_SIM_EXITED) {
        out.status = (exit_code == 0) ? CSL_SPATIAL_SIM_SUCCEEDED
                                      : CSL_SPATIAL_SIM_FAILED;
        out.has_exit_code = 1;
        out.exit_code = exit_code;
    } else {
        out.status = CSL_SPATIAL_SIM_FAILED;
    }
    return out;
}

CslErr csl_spatial_sim_usage(CslBuf *out) {
    CslErr err;
    if (!out) {
        return CSL_ERR_INVALID;
    }
    err = csl_buf_puts(out, "doe-csl-sim-runner --plan <path> [--driver-executable <path>] [--result-json <path>]\n");
    if (err) {
        return err;
    }
    err = csl_buf_puts(out, "Reads a Doe CSL simulator plan artifact, validates it, and launches the explicit simulator driver.\n");
    if (err) {
        return err;
    }
    err = csl_buf_puts(out, "Driver resolution order:\n");
    if (err) {
        return err;
    }
    err = csl_buf_puts(out, "  1. --driver-executable\n");
    if (err) {
        return err;
    }
    err = csl_buf_puts(out, "  2. plan.driver.executablePath\n");
    if (err) {
        return err;
    }
    err = csl_buf_puts(out, "  3. $DOE_CSL_SIM_EXECUTABLE\n");
    if (err) {
        return err;
    }
    return csl_buf_puts(out, "The runner writes stdout/stderr to the paths declared by the simulator plan and emits a result artifact.\n");
}

CslErr csl_spatial_sim_parse_args(const char *const *argv, int argc,
                                  CslSpatialSimArgs *out) {
    int i;
    if (!argv || argc < 0 || !out) {
        return CSL_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char **slot = NULL;
        if (!arg) {
            return CSL_ERR_INVALID;
        }
        if (strcmp(arg, "--help") == 0) {
            out->help_shown = 1;
            return CSL_OK;
        }
        if (strcmp(arg, "--plan") == 0) {
            slot = &out->plan_path;
        } else if (strcmp(arg, "--driver-executable") == 0) {
            slot = &out->driver_executable;
        } else if (strcmp(arg, "--result-json") == 0) {
            slot = &out->result_json_path;
        } else {
            /* Zig silently ignores unknown arguments; this port */
            /* fails visibly instead, so a typo cannot silently */
            /* change which plan runs. See spatial-mapping.md. */
            return CSL_ERR_INVALID;
        }
        if (i + 1 >= argc) {
            return CSL_ERR_INVALID;
        }
        i++;
        *slot = argv[i];
    }
    if (!out->plan_path || out->plan_path[0] == '\0') {
        return CSL_ERR_INVALID;
    }
    return CSL_OK;
}

static int is_absolute_path(const char *path) {
    return path && path[0] == '/';
}

CslErr csl_spatial_sim_resolve_artifact_path(const char *plan_path,
                                             const char *raw_path,
                                             CslBuf *out) {
    const char *slash;
    size_t dir_len;
    CslErr err;
    if (!plan_path || !raw_path || !out) {
        return CSL_ERR_INVALID;
    }
    if (is_absolute_path(raw_path)) {
        return csl_buf_puts(out, raw_path);
    }
    slash = strrchr(plan_path, '/');
    if (!slash) {
        err = csl_buf_puts(out, ".");
    } else {
        size_t i;
        dir_len = (size_t)(slash - plan_path);
        if (dir_len == 0) {
            /* Plan at the filesystem root: directory is "/". */
            err = csl_buf_putc(out, '/');
            if (err) {
                return err;
            }
            return csl_buf_puts(out, raw_path);
        }
        for (i = 0; i < dir_len; i++) {
            err = csl_buf_putc(out, plan_path[i]);
            if (err) {
                return err;
            }
        }
        err = CSL_OK;
    }
    if (err) {
        return err;
    }
    err = csl_buf_putc(out, '/');
    if (err) {
        return err;
    }
    return csl_buf_puts(out, raw_path);
}

CslErr csl_spatial_sim_default_result_path(const char *trace_path,
                                           CslBuf *out) {
    CslErr err;
    if (!trace_path || !out) {
        return CSL_ERR_INVALID;
    }
    err = csl_buf_puts(out, trace_path);
    if (err) {
        return err;
    }
    return csl_buf_puts(out, ".result.json");
}

CslErr csl_spatial_sim_resolve_driver(const char *plan_path,
                                      const char *cli_value,
                                      const char *plan_value,
                                      const char *env_value, CslBuf *out) {
    if (!plan_path || !out) {
        return CSL_ERR_INVALID;
    }
    if (cli_value) {
        return csl_buf_puts(out, cli_value);
    }
    if (plan_value) {
        return csl_spatial_sim_resolve_artifact_path(plan_path, plan_value,
                                                     out);
    }
    if (env_value) {
        return csl_buf_puts(out, env_value);
    }
    return CSL_ERR_INVALID;
}

int csl_spatial_sim_trace_exists(const char *path) {
    FILE *probe;
    if (!path) {
        return 0;
    }
    probe = fopen(path, "rb");
    if (!probe) {
        return 0;
    }
    fclose(probe);
    return 1;
}

CslErr csl_spatial_sim_run(const CslSpatialSimArgs *args) {
    (void)args;
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_sim_validate_plan(const char *plan_bytes, size_t len) {
    (void)plan_bytes;
    (void)len;
    return CSL_ERR_UNSUPPORTED;
}
