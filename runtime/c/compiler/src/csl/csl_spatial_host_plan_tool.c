// SPDX-License-Identifier: MIT
// Port of runtime/zig/src/spatial/csl/csl_host_plan_tool.zig (UNTESTED).
// Execution JSON lowering and artifact emission are NOT ported.
// Parity NOT verified.
#include "csl_spatial_host_plan_tool.h"

#include <stdio.h>
#include <string.h>

#include "csl_spatial_dense_gemv.h"

#define DENSE_TILE_WIDTH 32u

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}


static int eq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static int starts_with(const char *s, const char *prefix) {
    size_t plen;
    if (!s || !prefix) {
        return 0;
    }
    plen = strlen(prefix);
    return strncmp(s, prefix, plen) == 0;
}

static CslErr copy_str(char *dst, size_t dst_size, const char *src) {
    size_t len;
    if (!dst || dst_size == 0 || !src) {
        return CSL_ERR_INVALID;
    }
    len = strlen(src);
    if (len >= dst_size) {
        return CSL_ERR_TOO_LARGE;
    }
    memcpy(dst, src, len + 1);
    return CSL_OK;
}

static CslErr join_slash(char *dst, size_t dst_size, const char *dir,
                         const char *file) {
    size_t dir_len;
    size_t file_len;
    if (!dst || dst_size == 0 || !dir || !file) {
        return CSL_ERR_INVALID;
    }
    dir_len = strlen(dir);
    file_len = strlen(file);
    if (dir_len + 1u + file_len + 1u > dst_size) {
        return CSL_ERR_TOO_LARGE;
    }
    memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    memcpy(dst + dir_len + 1, file, file_len + 1);
    return CSL_OK;
}

CslErr csl_spatial_tool_usage(CslBuf *out) {
    if (!out) {
        return CSL_ERR_INVALID;
    }
    return csl_buf_puts(out, "usage: doe-csl-host-plan-tool --input <execution.json> (--output <host-plan.json> | --bundle-root <dir>) [--mode manifest|steps] [--cslc-executable <path>] [--driver-executable-path <path>]\n");
}

CslErr csl_spatial_tool_parse_args(const char *const *argv, int argc,
                                   CslSpatialToolArgs *out) {
    int i;
    if (!argv || argc < 0 || !out) {
        return CSL_ERR_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->mode = CSL_SPATIAL_TOOL_MODE_MANIFEST;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char **slot = NULL;
        if (!arg) {
            return CSL_ERR_INVALID;
        }
        if (eq(arg, "--help")) {
            out->help_shown = 1;
            return CSL_OK;
        }
        if (eq(arg, "--input")) {
            slot = &out->input_path;
        } else if (eq(arg, "--output")) {
            slot = &out->output_path;
        } else if (eq(arg, "--bundle-root")) {
            slot = &out->bundle_root;
        } else if (eq(arg, "--cslc-executable")) {
            slot = &out->cslc_executable;
        } else if (eq(arg, "--driver-executable-path")) {
            slot = &out->driver_executable_path;
        } else if (eq(arg, "--mode")) {
            if (i + 1 >= argc) {
                return CSL_ERR_INVALID;
            }
            i++;
            if (eq(argv[i], "manifest")) {
                out->mode = CSL_SPATIAL_TOOL_MODE_MANIFEST;
            } else if (eq(argv[i], "steps")) {
                out->mode = CSL_SPATIAL_TOOL_MODE_STEPS;
            } else {
                return CSL_ERR_INVALID;
            }
            continue;
        } else {
            return CSL_ERR_INVALID;
        }
        if (i + 1 >= argc) {
            return CSL_ERR_INVALID;
        }
        i++;
        *slot = argv[i];
    }
    if (!out->input_path || out->input_path[0] == '\0') {
        return CSL_ERR_INVALID;
    }
    if (out->bundle_root == NULL && out->output_path == NULL) {
        return CSL_ERR_INVALID;
    }
    if (out->bundle_root != NULL && out->output_path != NULL) {
        return CSL_ERR_INVALID;
    }
    return CSL_OK;
}

int csl_spatial_tool_is_phase_specialized(const char *name) {
    static const char *const kernels[] = {
        "rmsnorm", "residual", "gelu", "gelu_gated", "silu_gated",
        "sigmoid_gated",
    };
    size_t i;
    for (i = 0; i < 6; i++) {
        if (eq(name, kernels[i])) {
            return 1;
        }
    }
    return 0;
}

int csl_spatial_tool_is_lm_head_target(const char *name) {
    if (eq(name, "lm_head")) {
        return 1;
    }
    return starts_with(name, "lm_head_");
}

CslErr csl_spatial_tool_max_feature_dim(
    const CslSpatialModelConfig *config, uint32_t *out) {
    uint64_t q_dim;
    uint64_t kv_dim;
    uint64_t ffn_dim;
    uint64_t m;
    uint32_t kv_heads;
    if (!config || !out) {
        return CSL_ERR_INVALID;
    }
    q_dim = (uint64_t)config->num_heads * (uint64_t)config->head_dim;
    kv_heads = config->has_num_key_value_heads ? config->num_key_value_heads
                                                : config->num_heads;
    kv_dim = (uint64_t)kv_heads * (uint64_t)config->head_dim;
    ffn_dim = (uint64_t)config->hidden_dim *
              (uint64_t)config->ffn_expansion_factor;
    m = (uint64_t)config->hidden_dim;
    if (q_dim > m) {
        m = q_dim;
    }
    if (kv_dim > m) {
        m = kv_dim;
    }
    if (ffn_dim > m) {
        m = ffn_dim;
    }
    if (m > 0xFFFFFFFFu) {
        return CSL_ERR_TOO_LARGE;
    }
    *out = (uint32_t)m;
    return CSL_OK;
}

/* Shared target shell: name, layout/pe paths from the source */
/* kernel, optional base kernel, optional phase. Used by both the */
/* normal/phase target builder and the dense tile builder, which */
/* previously duplicated these five assignments. Field order and */
/* first-error precedence are unchanged in both callers. */
static CslErr init_target_shell(CslSpatialCompileTarget *target,
                                const char *kernel_name,
                                const char *source_name,
                                const char *phase,
                                const char *base_kernel) {
    CslErr err;
    if (!target) {
        return CSL_ERR_INVALID;
    }
    memset(target, 0, sizeof(*target));
    err = copy_str(target->kernel_name, sizeof(target->kernel_name), kernel_name);
    if (err) return err;
    err = join_slash(target->layout_path, sizeof(target->layout_path),
                     source_name, "layout.csl");
    if (err) return err;
    err = join_slash(target->pe_program_path, sizeof(target->pe_program_path),
                     source_name, "pe_program.csl");
    if (err) return err;
    if (base_kernel) {
        err = copy_str(target->base_kernel, sizeof(target->base_kernel), base_kernel);
        if (err) return err;
        target->has_base_kernel = 1;
    }
    if (phase) {
        err = copy_str(target->phase, sizeof(target->phase), phase);
        if (err) return err;
        target->has_phase = 1;
    }
    return CSL_OK;
}

static CslErr fill_target(CslSpatialCompileTarget *target,
                          const char *kernel_name, const char *source_name,
                          const char *pattern, const char *phase,
                          const char *base_kernel,
                          const CslSpatialHostPlan *plan,
                          const CslSpatialModelConfig *config,
                          CslSpatialScalar activation_elem,
                          uint32_t max_rect_dim) {
    CslErr err;
    CslSpatialBinding bindings[CSL_SPATIAL_BINDINGS_MAX];
    size_t binding_count = 0;
    int found = 0;
    size_t param_count = 0;
    err = init_target_shell(target, kernel_name, source_name, phase,
                              base_kernel);
    if (err) {
        return err;
    }
    err = csl_spatial_bindings_for_pattern(pattern, activation_elem,
                                           bindings,
                                           CSL_SPATIAL_BINDINGS_MAX,
                                           &binding_count, &found);
    if (err) {
        return err;
    }
    if (found) {
        size_t i;
        target->has_metadata = 1;
        err = copy_str(target->metadata_phase,
                       sizeof(target->metadata_phase),
                       phase ? phase : "base");
        if (err) {
            return err;
        }
        for (i = 0; i < binding_count; i++) {
            target->bindings[i] = bindings[i];
        }
        target->binding_count = binding_count;
    }
    err = csl_spatial_tool_compile_params(config, plan, kernel_name,
                                          pattern, phase, max_rect_dim,
                                          target->params,
                                          CSL_SPATIAL_TOOL_MAX_PARAMS,
                                          &param_count);
    if (err) {
        return err;
    }
    target->param_count = param_count;
    return CSL_OK;
}

CslErr csl_spatial_tool_build_targets(
    const CslSpatialHostPlan *plan, const CslSpatialModelConfig *config,
    CslSpatialScalar activation_elem, uint32_t max_rect_dim,
    CslSpatialCompileTarget *out, size_t capacity, size_t *out_count) {
    size_t count = 0;
    size_t k;
    CslErr err;
    if (!plan || !out || !out_count) {
        return CSL_ERR_INVALID;
    }
    if (plan->kernel_count > 0 && !plan->kernels) {
        return CSL_ERR_INVALID;
    }
    if (plan->kernel_count > CSL_SPATIAL_TOOL_MAX_KERNELS) {
        return CSL_ERR_TOO_LARGE;
    }
    *out_count = 0;
    for (k = 0; k < plan->kernel_count; k++) {
        const CslSpatialKernelSpec *kernel = &plan->kernels[k];
        int tiled = 0;
        if (!kernel->name || !kernel->pattern) {
            return CSL_ERR_INVALID;
        }
        if (config && eq(kernel->pattern, "dense_gemv") &&
            csl_spatial_tool_is_lm_head_target(kernel->name)) {
            uint32_t full_width;
            err = csl_spatial_dense_gemv_width(config->hidden_dim,
                                               max_rect_dim, &full_width);
            if (err) {
                return err;
            }
            if (full_width > DENSE_TILE_WIDTH) {
                uint32_t width_start = 0;
                tiled = 1;
                while (width_start < full_width) {
                    uint32_t width_count = min_u32(DENSE_TILE_WIDTH,
                                                   full_width - width_start);
                    char tile_name[CSL_SPATIAL_TOOL_NAME_LEN];
                    CslSpatialCompileTarget *target;
                    int n;
                    if (count >= capacity ||
                        count >= CSL_SPATIAL_TOOL_MAX_COMPILE_TARGETS) {
                        return CSL_ERR_TOO_LARGE;
                    }
                    n = snprintf(tile_name, sizeof(tile_name),
                                 "%s_width_tile_x%u_w%u", kernel->name,
                                 width_start, width_count);
                    if (n < 0 ||
                        (size_t)n >= sizeof(tile_name)) {
                        return CSL_ERR_TOO_LARGE;
                    }
                    target = &out[count];
                    err = init_target_shell(target, tile_name, kernel->name,
                                            NULL, kernel->name);
                    if (err) {
                        return err;
                    }
                    {
                        CslSpatialBinding bindings[CSL_SPATIAL_BINDINGS_MAX];
                        size_t binding_count = 0;
                        int found = 0;
                        size_t i;
                        err = csl_spatial_bindings_for_pattern(
                            kernel->pattern, activation_elem, bindings,
                            CSL_SPATIAL_BINDINGS_MAX, &binding_count,
                            &found);
                        if (err) {
                            return err;
                        }
                        if (found) {
                            target->has_metadata = 1;
                            err = copy_str(target->metadata_phase,
                                           sizeof(target->metadata_phase),
                                           "base");
                            if (err) {
                                return err;
                            }
                            for (i = 0; i < binding_count; i++) {
                                target->bindings[i] = bindings[i];
                            }
                            target->binding_count = binding_count;
                        }
                    }
                    {
                        size_t dense_count = 0;
                        err = csl_spatial_tool_dense_params(
                            config, width_count, max_rect_dim,
                            target->params, CSL_SPATIAL_TOOL_MAX_PARAMS,
                            &dense_count);
                        if (err) {
                            return err;
                        }
                        target->param_count = dense_count;
                    }
                    count++;
                    width_start += width_count;
                }
            }
        }
        if (tiled) {
            continue;
        }
        if (count >= capacity ||
            count >= CSL_SPATIAL_TOOL_MAX_COMPILE_TARGETS) {
            return CSL_ERR_TOO_LARGE;
        }
        err = fill_target(&out[count], kernel->name, kernel->name,
                          kernel->pattern, NULL, NULL, plan, config,
                          activation_elem, max_rect_dim);
        if (err) {
            return err;
        }
        count++;
        if (csl_spatial_tool_is_phase_specialized(kernel->name)) {
            static const char *const suffixes[] = { "prefill", "decode" };
            size_t s;
            for (s = 0; s < 2; s++) {
                char phase_name[CSL_SPATIAL_TOOL_NAME_LEN];
                int n = snprintf(phase_name, sizeof(phase_name), "%s_%s",
                                 kernel->name, suffixes[s]);
                if (n < 0 || (size_t)n >= sizeof(phase_name)) {
                    return CSL_ERR_TOO_LARGE;
                }
                if (count >= capacity ||
                    count >= CSL_SPATIAL_TOOL_MAX_COMPILE_TARGETS) {
                    return CSL_ERR_TOO_LARGE;
                }
                err = fill_target(&out[count], phase_name, kernel->name,
                                  kernel->pattern, suffixes[s],
                                  kernel->name, plan, config,
                                  activation_elem, max_rect_dim);
                if (err) {
                    return err;
                }
                count++;
            }
        }
    }
    *out_count = count;
    return CSL_OK;
}

CslErr csl_spatial_tool_build_state_buffers(
    const CslSpatialMemBuffer *buffers, size_t count,
    CslSpatialStateBuffer *out, size_t capacity, size_t *out_count) {
    size_t i;
    size_t kept = 0;
    if (!out || !out_count) {
        return CSL_ERR_INVALID;
    }
    if (count > 0 && !buffers) {
        return CSL_ERR_INVALID;
    }
    *out_count = 0;
    for (i = 0; i < count; i++) {
        CslSpatialStateKind kind;
        int skip = 0;
        switch (buffers[i].kind) {
            case CSL_SPATIAL_MEM_KV_CACHE:
                kind = CSL_SPATIAL_STATE_KV_CACHE;
                break;
            case CSL_SPATIAL_MEM_DECODE_POSITION:
            case CSL_SPATIAL_MEM_SLIDING_WINDOW:
                kind = CSL_SPATIAL_STATE_POSITION;
                break;
            case CSL_SPATIAL_MEM_ACTIVATION_SCRATCH:
            case CSL_SPATIAL_MEM_OUTPUT_LOGITS:
                kind = CSL_SPATIAL_STATE_SCRATCH;
                break;
            case CSL_SPATIAL_MEM_OTHER:
            default:
                skip = 1;
                break;
        }
        if (skip) {
            continue;
        }
        if (kept >= capacity) {
            return CSL_ERR_TOO_LARGE;
        }
        out[kept].name = buffers[i].name;
        out[kept].kind = kind;
        out[kept].bytes_per_pe = buffers[i].bytes_per_pe;
        kept++;
    }
    *out_count = kept;
    return CSL_OK;
}

CslErr csl_spatial_tool_run_from_execution_json(
    const CslSpatialToolArgs *args, const char *execution_json,
    size_t len) {
    (void)args;
    (void)execution_json;
    (void)len;
    return CSL_ERR_UNSUPPORTED;
}

CslErr csl_spatial_tool_make_cslc_plan(const char *cslc_executable) {
    (void)cslc_executable;
    return CSL_ERR_UNSUPPORTED;
}
