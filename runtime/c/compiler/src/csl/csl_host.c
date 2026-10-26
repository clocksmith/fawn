// SPDX-License-Identifier: MIT
// SRAM formulas are the exact Zig formulas (see header note for the
// runner, which is a compact equivalent, not Zig's full text).
#include "csl_host.h"
#include "csl_emit_common.h"
#include <string.h>


void csl_model_config_init(CslModelConfig *c) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->ffn_expansion_factor = 4; c->ffn_matrix_count = 3;
    c->partial_rotary_factor = 1.0f;
}
static uint64_t bytes_per_weight(CslQuantFormat f) {
    return f == CSL_QUANT_F16 ? 2 : 1; /* q4k upper bound 1, q8_0 1, f16 2 */
}
static uint64_t weight_layer(const CslModelConfig *c) {
    uint64_t hd = c->hidden_dim, bpw = bytes_per_weight(c->quant_format);
    return 4 * hd * hd * bpw + (uint64_t)c->ffn_matrix_count * hd * c->ffn_expansion_factor * hd * bpw;
}
static uint64_t kv_for_layers(const CslModelConfig *c, uint32_t layers) {
    return 2 * (uint64_t)layers * c->max_seq_len * c->hidden_dim * 2;
}
static uint64_t ple_bytes(const CslModelConfig *c) {
    uint64_t total_width, table, proj, norm, ple_vocab;
    if (!c->has_ple_width) return 0;
    ple_vocab = c->has_ple_vocab_size ? c->ple_vocab_size : c->vocab_size;
    total_width = (uint64_t)c->num_layers * c->ple_width;
    table = ple_vocab * total_width * 2;
    proj = (uint64_t)c->hidden_dim * total_width * bytes_per_weight(c->quant_format);
    norm = total_width * 2;
    return table + proj + norm;
}
uint64_t csl_estimate_layer_sram_bytes(const CslModelConfig *c) {
    if (!c || c->num_layers == 0) return 0;
    return weight_layer(c) + kv_for_layers(c, c->num_layers) / c->num_layers;
}
CslSramEstimate csl_estimate_model_sram(const CslModelConfig *c) {
    CslSramEstimate e; uint64_t per_layer, embed;
    memset(&e, 0, sizeof(e));
    if (!c) return e;
    per_layer = csl_estimate_layer_sram_bytes(c);
    embed = (uint64_t)c->vocab_size * c->hidden_dim * 2;
    e.per_layer_bytes = per_layer; e.embedding_bytes = embed;
    e.total_bytes = per_layer * c->num_layers + embed + ple_bytes(c);
    e.capacity_bytes = CSL_WSE3_TOTAL_SRAM_BYTES;
    e.fits = e.total_bytes <= e.capacity_bytes;
    e.estimate_kind = "conservative_upper_bound";
    return e;
}
uint32_t csl_effective_kv_cache_layer_count(const CslModelConfig *c, const CslHostPlan *plan) {
    uint32_t unaliased = 0, aliased = 0; size_t i, j;
    if (!c || !plan) return c ? c->num_layers : 0;
    for (i = 0; i < plan->decode_count; i++) {
        const CslLaunchSpec *launch = &plan->decode_launches[i];
        const CslKernelSpec *kernel = NULL; size_t k;
        for (k = 0; k < plan->kernel_count; k++)
            if (plan->kernels[k].name && launch->kernel_name && strcmp(plan->kernels[k].name, launch->kernel_name) == 0) kernel = &plan->kernels[k];
        if (!kernel || !kernel->pattern || strcmp(kernel->pattern, "kv_write") != 0) continue;
        if (launch->kv_cache_alias) {
            int seen = 0;
            for (j = 0; j < i; j++) {
                const char *prior = plan->decode_launches[j].kv_cache_alias;
                if (prior && strcmp(prior, launch->kv_cache_alias) == 0) { seen = 1; break; }
            }
            if (!seen) aliased += 1;
        } else unaliased += 1;
    }
    { uint32_t eff = unaliased + aliased; return eff == 0 ? c->num_layers : eff; }
}
CslSramEstimate csl_estimate_model_sram_for_plan(const CslModelConfig *c, const CslHostPlan *plan) {
    CslSramEstimate e; uint64_t weight, kv_total, kv_per, embed, eff;
    memset(&e, 0, sizeof(e));
    if (!c || !plan) return e;
    eff = csl_effective_kv_cache_layer_count(c, plan);
    weight = weight_layer(c); kv_total = kv_for_layers(c, (uint32_t)eff);
    kv_per = eff == 0 ? 0 : kv_total / eff;
    embed = (uint64_t)c->vocab_size * c->hidden_dim * 2;
    e.per_layer_bytes = weight + kv_per; e.embedding_bytes = embed;
    e.total_bytes = weight * c->num_layers + kv_total + embed + ple_bytes(c);
    e.capacity_bytes = (uint64_t)plan->pe_grid_width * plan->pe_grid_height * CSL_WSE3_PE_SRAM_BYTES;
    e.fits = e.total_bytes <= e.capacity_bytes;
    e.estimate_kind = "plan_aware_upper_bound";
    return e;
}
uint32_t csl_rope_num_pairs(const CslModelConfig *c) {
    if (!c) return 0;
    return (uint32_t)((float)c->head_dim * c->partial_rotary_factor / 2.0f);
}
static CslErr emit_launches_json(CslBuf *out, const CslLaunchSpec *launches, size_t count) {
    size_t i;
    PUT("[");
    for (i = 0; i < count; i++) {
        const CslLaunchSpec *l = &launches[i];
        if (i) PUT(", ");
        PUT("{\"kernelName\": "); { CslErr e = csl_emit_json_string(out, l->kernel_name); if (e) return e; }
        PUT(", \"repeat\": "); PUTI(l->repeat ? l->repeat : 1);
        if (l->has_attention_type) { PUT(", \"attentionType\": "); { CslErr e = csl_emit_json_string(out, l->attention_type == CSL_ATTN_SLIDING ? "sliding" : "global"); if (e) return e; } }
        if (l->has_sliding_window_size) { PUT(", \"slidingWindowSize\": "); PUTI(l->sliding_window_size); }
        if (l->has_current_pos_source) { PUT(", \"currentPosSource\": \"decode_position\""); }
        if (l->kv_cache_alias) { PUT(", \"kvCacheAlias\": "); { CslErr e = csl_emit_json_string(out, l->kv_cache_alias); if (e) return e; } }
        PUT("}");
    }
    PUT("]");
    return CSL_OK;
}
CslErr csl_emit_compilation_manifest(CslBuf *out, const CslModelConfig *c, const CslHostPlan *plan) {
    size_t i; CslErr e;
    if (!out || !c || !plan) return CSL_ERR_INVALID;
    PUT("{\"peGrid\": {\"width\": "); PUTI(plan->pe_grid_width);
    PUT(", \"height\": "); PUTI(plan->pe_grid_height ? plan->pe_grid_height : 1);
    PUT("}, \"kernels\": [");
    for (i = 0; i < plan->kernel_count; i++) {
        if (i) PUT(", ");
        PUT("{\"name\": "); e = csl_emit_json_string(out, plan->kernels[i].name); if (e) return e;
        PUT(", \"pattern\": "); e = csl_emit_json_string(out, plan->kernels[i].pattern); if (e) return e;
        PUT(", \"count\": "); PUTI(plan->kernels[i].count ? plan->kernels[i].count : 1); PUT("}");
    }
    PUT("], \"prefillLaunches\": "); e = emit_launches_json(out, plan->prefill_launches, plan->prefill_count); if (e) return e;
    PUT(", \"decodeLaunches\": "); e = emit_launches_json(out, plan->decode_launches, plan->decode_count); if (e) return e;
    if (plan->has_eos_token_id) { PUT(", \"eosTokenId\": "); PUTI(plan->eos_token_id); }
    PUT("}\n");
    return CSL_OK;
}
CslErr csl_emit_python_runner(CslBuf *out, const CslModelConfig *c, const CslHostPlan *plan) {
    if (!out || !c || !plan) return CSL_ERR_INVALID;
    PUT("# CSL Python runner scaffold (compact equivalent; see csl_host.h).\n");
    PUT("# Compile kernels declared by the explicit host plan.\n");
    PUT("STATE_BUFFERS = {}\n\n");
    PUT("def load_and_stage_weights(rdrr_path):\n");
    PUT("    # Load RDRR shards using Doppler's shard schema fields.\n");
    PUT("    # {rdrr_path}/{filename} per shard; memcpy_h2d stages each shard.\n    pass\n\n");
    PUT("def memcpy_h2d(name, data):\n    pass\n\n");
    PUT("def init_state():\n");
    PUT("    memcpy_h2d('position', 0)\n");
    PUT("    memcpy_h2d('sliding_window', 0)\n\n");
    PUT("PREFILL_LAUNCHES = []\nDECODE_LAUNCHES = []\nEOS_TOKEN_ID = None\n\n");
    PUT("def decode_loop(prompts):\n");
    PUT("    # Autoregressive decode scaffold from the explicit host plan.\n");
    PUT("    # Execute a sequence of kernel launches with repeat counts.\n");
    PUT("    # Run decode loop for each prompt in the batch sequentially.\n");
    PUT("    # Read full logit vector from device for external sampling.\n");
    PUT("    pass\n");
    return CSL_OK;
}
