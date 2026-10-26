// SPDX-License-Identifier: MIT
// UNRUN: this test file has NOT been executed. It is a draft for the
// coordinator to run later; a passing claim must not be made from it.
// Tests for the csl_spatial_* port. Compile (not run) with gcc
// -std=c11 -Wall -Wextra -Wpedantic -Werror -I ../src, listing
// csl_spatial_tests.c plus ../src/csl_spatial_*.c and csl_text.c.
// Note: csl_spatial_bundle_translate_wgsl and the other UNSUPPORTED
// entry points are intentionally not exercised as successes.
#include <stdio.h>
#include <string.h>

#include "csl_spatial_bundle.h"
#include "csl_spatial_dense_gemv.h"
#include "csl_spatial_bindings.h"
#include "csl_spatial_pe_metadata.h"
#include "csl_spatial_host_plan_tool_config.h"
#include "csl_spatial_host_plan_tool.h"
#include "csl_spatial_materialize.h"
#include "csl_spatial_sim_runner.h"

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

#define CHECK_ERR(call) CHECK((call) == CSL_OK)

static int str_eq_n(const char *a, size_t alen, const char *b) {
    return strlen(b) == alen && memcmp(a, b, alen) == 0;
}

static CslSpatialModelConfig qwen_config(void) {
    CslSpatialModelConfig c;
    memset(&c, 0, sizeof(c));
    c.hidden_dim = 5120;
    c.num_heads = 24;
    c.head_dim = 256;
    c.has_linear_key_head_dim = 1;
    c.linear_key_head_dim = 96;
    c.has_linear_value_head_dim = 1;
    c.linear_value_head_dim = 64;
    c.has_linear_conv_kernel_dim = 1;
    c.linear_conv_kernel_dim = 7;
    c.num_layers = 64;
    c.vocab_size = 248320;
    c.max_seq_len = 4096;
    c.quant_format = CSL_SPATIAL_QUANT_Q4K;
    c.ffn_expansion_factor = 4;
    c.ffn_matrix_count = 3;
    c.partial_rotary_factor = 1.0f;
    return c;
}

int main(void) {
    /* Bundle split. */
    {
        const char *bundle =
            "//--- layout.csl ---\nlayout body\n"
            "//--- pe_program.csl ---\npe body\n";
        CslSpatialBundleSplit split;
        CHECK_ERR(csl_spatial_bundle_split(bundle, strlen(bundle), &split));
        CHECK(str_eq_n(split.layout.data, split.layout.len, "layout body"));
        CHECK(str_eq_n(split.pe_program.data, split.pe_program.len, "pe body"));
        CHECK(csl_spatial_bundle_split("no markers", 10, &split) ==
              CSL_ERR_INVALID);
        CHECK(csl_spatial_bundle_translate_wgsl("x", 1, NULL) ==
              CSL_ERR_UNSUPPORTED);
    }
    /* Dense GEMV arithmetic with a caller supplied max_rect_dim. */
    {
        uint32_t w = 0;
        uint32_t h = 0;
        uint32_t in_dim = 0;
        CHECK_ERR(csl_spatial_dense_gemv_width(5120, 256, &w));
        CHECK(w == 160);
        CHECK_ERR(csl_spatial_dense_gemv_height(262144, 256, &h));
        CHECK(h == 256);
        CHECK_ERR(csl_spatial_dense_gemv_in_dim_per_pe(5120, 256, &in_dim));
        CHECK(in_dim == 32);
        /* Canonical Zig value, csl_spec.zig:45 at d25e075. */
        CHECK(CSL_SPATIAL_MAX_RECT_DIM == 512);
        CHECK_ERR(csl_spatial_dense_gemv_height(262144,
                                                CSL_SPATIAL_MAX_RECT_DIM, &h));
        CHECK(h == 512);
    }
    /* Bindings: rms_norm f16 rerouting, dense table as-is. */
    {
        CslSpatialBinding out[CSL_SPATIAL_BINDINGS_MAX];
        size_t count = 0;
        int found = 0;
        CHECK_ERR(csl_spatial_bindings_for_pattern(
            "rms_norm", CSL_SPATIAL_SCALAR_F16, out,
            CSL_SPATIAL_BINDINGS_MAX, &count, &found));
        CHECK(found && count == 3);
        CHECK(strcmp(out[0].elem_type, "f16") == 0);
        CHECK(strcmp(out[1].weight_source, "runtime_weight_mapping") == 0);
        CHECK_ERR(csl_spatial_bindings_for_pattern(
            "no_such_pattern", CSL_SPATIAL_SCALAR_F32, out,
            CSL_SPATIAL_BINDINGS_MAX, &count, &found));
        CHECK(!found && count == 0);
        CHECK(csl_spatial_bindings_for_pattern(
                  "rms_norm", CSL_SPATIAL_SCALAR_OTHER, out,
                  CSL_SPATIAL_BINDINGS_MAX, &count, &found) ==
              CSL_ERR_INVALID);
    }
    /* PE metadata: the Zig SUMMA test source. */
    {
        const char *src =
            "param Mt: i16;\n"
            "param Kt: i16;\n"
            "var A_tile = @zeros([Mt * Kt]f32);\n"
            "var B_tile = @zeros([Kt * Nt]f32);\n"
            "var A_ptr: [*]f32 = &A_tile;\n"
            "var B_ptr: [*]f32 = &B_tile;\n"
            "@export_symbol(A_ptr, \"a\");\n"
            "@export_symbol(B_ptr, \"b\");\n";
        CslSpatialPeParsed parsed;
        CslBuf buf;
        CHECK_ERR(csl_spatial_pe_parse(src, strlen(src), &parsed));
        CHECK(parsed.decl_count == 2);
        CHECK(parsed.pointer_count == 2);
        CHECK(parsed.export_count == 2);
        CHECK(str_eq_n(parsed.decls[0].name.data, parsed.decls[0].name.len,
                       "A_tile"));
        CHECK(str_eq_n(parsed.decls[0].size_expr.data,
                       parsed.decls[0].size_expr.len, "Mt * Kt"));
        CHECK_ERR(csl_buf_init(&buf, 512));
        CHECK_ERR(csl_spatial_pe_emit_json(src, strlen(src), &buf));
        CHECK(strstr(buf.data, "\"symbol\":\"a\"") != NULL);
        CHECK(strstr(buf.data, "\"backing\":\"A_tile\"") != NULL);
        csl_buf_free(&buf);
    }
    /* Layout metadata: the Zig layout test source. */
    {
        const char *src =
            "layout {\n"
            "    @export_name(\"input\", [*]f32, true);\n"
            "    @export_name(\"compute\", fn()void);\n"
            "}\n";
        CslSpatialLayoutParsed parsed;
        CHECK_ERR(csl_spatial_pe_parse_layout(src, strlen(src), &parsed));
        CHECK(parsed.export_count == 2);
        CHECK(parsed.exports[0].mutable_flag == 1);
        CHECK(str_eq_n(parsed.exports[1].kind.data,
                       parsed.exports[1].kind.len, "device_function"));
        CHECK(str_eq_n(parsed.exports[1].type_str.data,
                       parsed.exports[1].type_str.len, "fn()void"));
    }
    /* Config validators, mirroring the Zig admission tests. */
    {
        CslSpatialScalar scalar;
        CHECK_ERR(csl_spatial_config_admit_activation_dtype("f16"));
        CHECK_ERR(csl_spatial_config_admit_activation_dtype(NULL));
        CHECK(csl_spatial_config_admit_activation_dtype("bf16") ==
              CSL_ERR_INVALID);
        CHECK_ERR(csl_spatial_config_activation_scalar(NULL, &scalar));
        CHECK(scalar == CSL_SPATIAL_SCALAR_F32);
        /* 64 lowercase hex chars from the Zig weight mapping test. */
        CHECK_ERR(csl_spatial_config_check_sha256(
            "6a0e8ecfb1190554392143f79434839b629ee9284a3fd643d84cb62522a0cdcd"));
        CHECK(csl_spatial_config_check_sha256(
                  "6A0e8ecfb1190554392143f79434839b629ee9284a3fd643d84cb62522a0cdcd") ==
              CSL_ERR_INVALID);
        CHECK(csl_spatial_config_parse_bundle_model_config("{}", 2, NULL,
                                                            NULL) ==
              CSL_ERR_UNSUPPORTED);
    }
    /* Tool params: Qwen linear SSM dims from the Zig tests. */
    {
        CslSpatialModelConfig cfg = qwen_config();
        CslSpatialHostPlan plan;
        CslSpatialCompileParam params[CSL_SPATIAL_TOOL_MAX_PARAMS];
        size_t count = 0;
        memset(&plan, 0, sizeof(plan));
        plan.pe_grid_width = 16;
        plan.pe_grid_height = 1;
        CHECK_ERR(csl_spatial_tool_compile_params(
            &cfg, &plan, "ssm_conv1d_depthwise", "conv1d_depthwise", NULL,
            256, params, CSL_SPATIAL_TOOL_MAX_PARAMS, &count));
        CHECK(count == 5 && params[3].value == 96 && params[4].value == 7);
        CHECK_ERR(csl_spatial_tool_compile_params(
            &cfg, &plan, "ssm_linear_attention", "linear_attention", NULL,
            256, params, CSL_SPATIAL_TOOL_MAX_PARAMS, &count));
        CHECK(count == 5 && params[0].value == 1 && params[1].value == 2);
        CHECK(params[2].value == 96 && params[3].value == 64 &&
              params[4].value == 32);
    }
    /* Tool target expansion: rmsnorm plus sample gives 4 targets. */
    {
        CslSpatialKernelSpec kernels[2];
        CslSpatialHostPlan plan;
        CslSpatialCompileTarget targets[CSL_SPATIAL_TOOL_MAX_COMPILE_TARGETS];
        size_t count = 0;
        kernels[0].name = "rmsnorm";
        kernels[0].pattern = "element_wise";
        kernels[1].name = "sample";
        kernels[1].pattern = "sample";
        memset(&plan, 0, sizeof(plan));
        plan.pe_grid_width = 16;
        plan.pe_grid_height = 1;
        plan.kernels = kernels;
        plan.kernel_count = 2;
        CHECK_ERR(csl_spatial_tool_build_targets(
            &plan, NULL, CSL_SPATIAL_SCALAR_F32, 256, targets,
            CSL_SPATIAL_TOOL_MAX_COMPILE_TARGETS, &count));
        CHECK(count == 4);
        CHECK(strcmp(targets[1].kernel_name, "rmsnorm_prefill") == 0);
        CHECK(strcmp(targets[1].layout_path, "rmsnorm/layout.csl") == 0);
        CHECK(strcmp(targets[3].kernel_name, "sample") == 0);
    }
    /* Materialize stale directory decision. */
    {
        CslSpatialDirEntry entries[5];
        const char *kernels[1];
        const char *prune[5];
        size_t count = 0;
        entries[0].name = "current";
        entries[0].is_dir = 1;
        entries[1].name = "stale";
        entries[1].is_dir = 1;
        entries[2].name = "compiled";
        entries[2].is_dir = 1;
        entries[3].name = "driver-logs";
        entries[3].is_dir = 1;
        entries[4].name = "targets.metadata.json";
        entries[4].is_dir = 0;
        kernels[0] = "current";
        CHECK_ERR(csl_spatial_materialize_stale_dirs(
            entries, 5, kernels, 1, prune, 5, &count));
        CHECK(count == 1 && strcmp(prune[0], "stale") == 0);
        CHECK(csl_spatial_materialize_compile_pattern("prefill_q4k_gemv") !=
              NULL);
        CHECK(strcmp(csl_spatial_materialize_compile_pattern(
                         "prefill_q4k_gemv"),
                     "fused_gemv_dequant") == 0);
    }
    /* Sim runner: driver order and outcome mapping. */
    {
        CslBuf buf;
        CslSpatialSimOutcome oc;
        CHECK_ERR(csl_buf_init(&buf, 256));
        CHECK_ERR(csl_spatial_sim_resolve_driver(
            "/plans/simulator-plan.json", "/opt/driver", "plan-driver",
            "/env/driver", &buf));
        CHECK(strcmp(buf.data, "/opt/driver") == 0);
        csl_buf_free(&buf);
        CHECK_ERR(csl_buf_init(&buf, 256));
        CHECK_ERR(csl_spatial_sim_resolve_driver(
            "/plans/simulator-plan.json", NULL, "driver.py", NULL, &buf));
        CHECK(strcmp(buf.data, "/plans/driver.py") == 0);
        csl_buf_free(&buf);
        CHECK_ERR(csl_buf_init(&buf, 256));
        CHECK_ERR(csl_spatial_sim_resolve_driver(
            "/plans/simulator-plan.json", NULL, NULL, "/env/driver", &buf));
        CHECK(strcmp(buf.data, "/env/driver") == 0);
        csl_buf_free(&buf);
        CHECK_ERR(csl_buf_init(&buf, 256));
        CHECK(csl_spatial_sim_resolve_driver(
                  "/plans/simulator-plan.json", NULL, NULL, NULL, &buf) ==
              CSL_ERR_INVALID);
        csl_buf_free(&buf);
        CHECK(csl_spatial_sim_trace_exists("/nonexistent-trace.json") == 0);
        CHECK(csl_spatial_sim_trace_exists(NULL) == 0);
        oc = csl_spatial_sim_outcome(CSL_SPATIAL_SIM_EXITED, 0);
        CHECK(oc.status == CSL_SPATIAL_SIM_SUCCEEDED && oc.has_exit_code);
        oc = csl_spatial_sim_outcome(CSL_SPATIAL_SIM_SIGNAL, 0);
        CHECK(oc.status == CSL_SPATIAL_SIM_FAILED && !oc.has_exit_code);
        CHECK(csl_spatial_sim_run(NULL) == CSL_ERR_UNSUPPORTED);
    }
    if (failures == 0) {
        printf("all csl_spatial checks passed\n");
    } else {
        printf("%d csl_spatial checks failed\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
