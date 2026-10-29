/* SPDX-License-Identifier: MIT */
/* Host developer checks for emitted TSIR/CSL text. These checks do not
 * execute CSL on a device or establish target parity. */
#include <stdio.h>
#include <string.h>

#include "csl_text.h"
#include "tsir_csl_common.h"
#include "tsir_csl_emit.h"
#include "tsir_csl_kernel_body.h"

static int failures = 0;

static void check(int ok, const char *label) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", label);
        failures++;
    }
}

static int contains(const CslBuf *b, const char *needle) {
    if (b == NULL || b->data == NULL || needle == NULL) {
        return 0;
    }
    return strstr(b->data, needle) != NULL;
}

static TsirCslBinding bind(const char *name, TsirCslElem elem,
    TsirCslRole role) {
    TsirCslBinding b;
    b.name = name;
    b.elem = elem;
    b.read_write = 0;
    b.role = role;
    return b;
}

int main(void) {
    CslBuf buf;
    CslErr err = CSL_OK;
    TsirCslConfig config;
    TsirCslKernelDesc desc;
    TsirCslRealization real;
    unsigned char hash[32];
    memset(&desc, 0, sizeof(desc));
    memset(&real, 0, sizeof(real));
    memset(hash, 0, sizeof(hash));
    tsir_csl_default_config(&config);
    config.var_prefix = "";

    /* KV write, slot-sharded, mirroring the Zig test in
     * emit_kernel_body_kv.zig. */
    {
        TsirCslBinding bindings[5];
        bindings[0] = bind("kp", TSIR_CSL_F32,
            TSIR_CSL_ROLE_KEY_PROJECTION);
        bindings[1] = bind("vp", TSIR_CSL_F32,
            TSIR_CSL_ROLE_VALUE_PROJECTION);
        bindings[2] = bind("kc", TSIR_CSL_F32,
            TSIR_CSL_ROLE_KEY_CACHE);
        bindings[3] = bind("vc", TSIR_CSL_F32,
            TSIR_CSL_ROLE_VALUE_CACHE);
        bindings[4] = bind("position", TSIR_CSL_U32,
            TSIR_CSL_ROLE_DECODE_POSITION);
        config.kv_cache_pe_strategy = TSIR_CSL_KV_SLOT_SHARDED;
        config.has_head_dim_default = 1;
        config.head_dim_default = 256;
        config.has_max_seq_len_default = 1;
        config.max_seq_len_default = 4096;
        config.has_kv_slots_per_pe_default = 1;
        config.kv_slots_per_pe_default = 17;
        desc.op = TSIR_CSL_OP_KV_WRITE;
        desc.bindings = bindings;
        desc.binding_count = 5;
        desc.config = &config;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_OK, "kv write returns OK");
        check(contains(&buf, "param pe_id: i16;"),
            "kv write pe_id param");
        check(contains(&buf, "param slots_per_pe: i16 = 17;"),
            "kv write slots default");
        check(contains(&buf,
            "const local_kv_len: u32 = @as(u32, slots_per_pe) * @as(u32, head_dim);"),
            "kv write local_kv_len");
        check(contains(&buf, "var kc: [local_kv_len]f32"),
            "kv write kc buffer");
        check(contains(&buf, "if (owning_pe == @as(u32, pe_id))"),
            "kv write ownership guard");
        check(!contains(&buf, "[kv_cache_len]f32"),
            "kv write has no full-per-pe buffer");
        csl_buf_free(&buf);
        /* Full-per-pe KV must fail visibly, not emit a full body. */
        config.kv_cache_pe_strategy = TSIR_CSL_KV_FULL_PER_PE;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_ERR_UNSUPPORTED,
            "kv write full_per_pe unsupported");
        csl_buf_free(&buf);
    }

    /* Attention, full_per_pe, f32. */
    {
        TsirCslBinding bindings[4];
        bindings[0] = bind("q", TSIR_CSL_F32, TSIR_CSL_ROLE_QUERY);
        bindings[1] = bind("k", TSIR_CSL_F32, TSIR_CSL_ROLE_KEY);
        bindings[2] = bind("v", TSIR_CSL_F32, TSIR_CSL_ROLE_VALUE);
        bindings[3] = bind("o", TSIR_CSL_F32, TSIR_CSL_ROLE_OUTPUT);
        tsir_csl_default_config(&config);
        config.var_prefix = "tsir_";
        memset(&desc, 0, sizeof(desc));
        desc.op = TSIR_CSL_OP_ATTENTION_SCORES;
        desc.bindings = bindings;
        desc.binding_count = 4;
        desc.config = &config;
        desc.body.attn_head_dim = 256;
        desc.body.attn_softmax_mode =
            TSIR_CSL_SOFTMAX_TWO_PASS_STABLE;
        desc.body.attn_scale_source = TSIR_CSL_SCALE_LITERAL_F32;
        desc.body.attn_has_scale_literal = 1;
        desc.body.attn_scale_literal = 0.125;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_OK, "attention returns OK");
        check(contains(&buf, "const attn_scale: f32 = "),
            "attention scale line");
        check(contains(&buf, "var attn_scores: [kv_len]f32"),
            "attention scores buffer");
        check(contains(&buf, "sys_mod.unblock_cmd_stream();"),
            "attention unblock");
        csl_buf_free(&buf);
        desc.body.attn_softmax_mode =
            TSIR_CSL_SOFTMAX_STREAMING_ONLINE;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_ERR_INVALID, "streaming attention rejected");
        csl_buf_free(&buf);
    }

    /* Gated gelu, conv1d with bias, L2, linear attention: one
     * substring per body, full parity checks are in tsir-tests.md. */
    {
        TsirCslBinding bindings[6];
        tsir_csl_default_config(&config);
        config.var_prefix = "tsir_";
        bindings[0] = bind("gate", TSIR_CSL_F32, TSIR_CSL_ROLE_GATE);
        bindings[1] = bind("in", TSIR_CSL_F32, TSIR_CSL_ROLE_INPUT);
        bindings[2] = bind("out", TSIR_CSL_F32, TSIR_CSL_ROLE_OUTPUT);
        memset(&desc, 0, sizeof(desc));
        desc.op = TSIR_CSL_OP_GELU_GATED;
        desc.bindings = bindings;
        desc.binding_count = 3;
        desc.config = &config;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_OK, "gelu returns OK");
        check(contains(&buf, "fn gelu(x: f32) f32"),
            "gelu helper");
        check(contains(&buf, "const GELU_A: f32 = 0.7978845608028654;"),
            "gelu constant");
        csl_buf_free(&buf);

        bindings[0] = bind("x", TSIR_CSL_F32, TSIR_CSL_ROLE_INPUT);
        bindings[1] = bind("w", TSIR_CSL_F32, TSIR_CSL_ROLE_WEIGHT);
        bindings[2] = bind("y", TSIR_CSL_F32, TSIR_CSL_ROLE_OUTPUT);
        bindings[3] = bind("b", TSIR_CSL_F32, TSIR_CSL_ROLE_BIAS);
        memset(&desc, 0, sizeof(desc));
        desc.op = TSIR_CSL_OP_CONV1D_DEPTHWISE;
        desc.bindings = bindings;
        desc.binding_count = 4;
        desc.config = &config;
        desc.body.conv_channels = 8;
        desc.body.conv_kernel_size = 4;
        desc.body.conv_has_bias = 1;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_OK, "conv1d returns OK");
        check(contains(&buf, "const t_in: i16 = t - (kernel_size - 1 - k);"),
            "conv1d causal index");
        csl_buf_free(&buf);

        bindings[0] = bind("x", TSIR_CSL_F32, TSIR_CSL_ROLE_INPUT);
        bindings[1] = bind("y", TSIR_CSL_F32, TSIR_CSL_ROLE_OUTPUT);
        memset(&desc, 0, sizeof(desc));
        desc.op = TSIR_CSL_OP_L2_NORMALIZE;
        desc.bindings = bindings;
        desc.binding_count = 2;
        desc.config = &config;
        desc.body.l2_hidden = 128;
        desc.body.l2_eps = 0.000001;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_OK, "l2 returns OK");
        check(contains(&buf, "fn sqrt_nr(x: f32) f32"),
            "l2 sqrt_nr");
        check(contains(&buf, "const l2_eps: f32 = "),
            "l2 eps line");
        csl_buf_free(&buf);

        bindings[0] = bind("q", TSIR_CSL_F32, TSIR_CSL_ROLE_QUERY);
        bindings[1] = bind("k", TSIR_CSL_F32, TSIR_CSL_ROLE_KEY);
        bindings[2] = bind("v", TSIR_CSL_F32, TSIR_CSL_ROLE_VALUE);
        bindings[3] = bind("g", TSIR_CSL_F32, TSIR_CSL_ROLE_GATE);
        bindings[4] = bind("s", TSIR_CSL_F32,
            TSIR_CSL_ROLE_LINEAR_STATE);
        bindings[4].read_write = 1;
        bindings[5] = bind("o", TSIR_CSL_F32, TSIR_CSL_ROLE_OUTPUT);
        memset(&desc, 0, sizeof(desc));
        desc.op = TSIR_CSL_OP_LINEAR_ATTENTION;
        desc.bindings = bindings;
        desc.binding_count = 6;
        desc.config = &config;
        desc.body.lin_key_dim = 128;
        desc.body.lin_value_dim = 128;
        desc.body.lin_key_heads = 16;
        desc.body.lin_value_heads = 48;
        desc.body.lin_norm_mode = TSIR_CSL_NORM_SHARED;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_kernel_body(&desc, &buf);
        check(err == CSL_OK, "linear attention returns OK");
        check(contains(&buf, "param value_dim_per_pe: i16;"),
            "linear value_dim_per_pe param");
        check(contains(&buf, "param a_log: f32;"),
            "linear a_log param");
        csl_buf_free(&buf);
    }

    /* emit_csl skeleton and semantic wrappers. */
    {
        TsirCslBinding bindings[4];
        bindings[0] = bind("q", TSIR_CSL_F32, TSIR_CSL_ROLE_QUERY);
        bindings[1] = bind("k", TSIR_CSL_F32, TSIR_CSL_ROLE_KEY);
        bindings[2] = bind("v", TSIR_CSL_F32, TSIR_CSL_ROLE_VALUE);
        bindings[3] = bind("o", TSIR_CSL_F32, TSIR_CSL_ROLE_OUTPUT);
        real.target_name = "test-target";
        real.descriptor_hash = hash;
        real.semantic_index = 0;
        real.pe_grid_width = 1;
        real.pe_grid_height = 1;
        real.emitter_params_json = "{}";
        real.bindings = bindings;
        real.binding_count = 4;
        csl_buf_init(&buf, 4096);
        err = tsir_csl_emit_skeleton(&real, &buf);
        check(err == CSL_OK, "skeleton returns OK");
        check(contains(&buf, "//--- layout.csl ---"),
            "skeleton layout marker");
        check(contains(&buf,
            "tsir mechanical skeleton: kernel body is emitted by later lowering."),
            "skeleton stub comment");
        csl_buf_free(&buf);
        tsir_csl_default_config(&config);
        config.var_prefix = "tsir_";
        memset(&desc, 0, sizeof(desc));
        desc.op = TSIR_CSL_OP_ATTENTION_SCORES;
        desc.bindings = bindings;
        desc.binding_count = 4;
        desc.config = &config;
        desc.body.attn_head_dim = 64;
        desc.body.attn_softmax_mode =
            TSIR_CSL_SOFTMAX_TWO_PASS_STABLE;
        desc.body.attn_scale_source = TSIR_CSL_SCALE_LITERAL_F32;
        desc.body.attn_has_scale_literal = 1;
        desc.body.attn_scale_literal = 0.125;
        csl_buf_init(&buf, 8192);
        err = tsir_csl_emit_semantic(&real, &desc, &buf);
        check(err == CSL_OK, "semantic returns OK");
        check(contains(&buf, "param kv_len: i16;"),
            "semantic attention kv_len param");
        check(contains(&buf, "@export_name(\"q\", [*]f32, true);"),
            "semantic binding export");
        csl_buf_free(&buf);
    }

    check(tsir_csl_emitter_code_digest(hash) == CSL_ERR_UNSUPPORTED,
        "emitter digest unsupported");
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
