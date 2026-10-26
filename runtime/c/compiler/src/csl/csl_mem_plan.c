// SPDX-License-Identifier: MIT
// See header for the partial-preference note.
#include "csl_mem_plan.h"
#include "csl_emit_common.h"
#include "csl_spec.h"
#include <string.h>


const char *csl_buffer_kind_name(CslBufferKind k) {
    switch (k) {
        case CSL_BUF_KV_CACHE: return "kv_cache";
        case CSL_BUF_ACTIVATION_SCRATCH: return "activation_scratch";
        case CSL_BUF_DECODE_POSITION: return "decode_position";
        case CSL_BUF_SLIDING_WINDOW: return "sliding_window";
        case CSL_BUF_OUTPUT_LOGITS: return "output_logits";
        case CSL_BUF_TOKEN_EMBEDDINGS: return "token_embeddings";
        case CSL_BUF_PLE_TABLE: return "ple_table";
        case CSL_BUF_PLE_PROJECTION: return "ple_projection";
        case CSL_BUF_PLE_NORM: return "ple_norm";
        case CSL_BUF_LAYER_WEIGHTS: return "layer_weights";
        case CSL_BUF_EMBEDDING_ROWS: return "embedding_rows";
        default: return "ple_rows";
    }
}
CslErr csl_plan_memory(CslResidencyMode residency, uint32_t width, uint32_t height, const CslBufferPlan *buffers, size_t count, CslMemoryPlan *out) {
    uint64_t total = 0; size_t i; uint32_t pe_count;
    if (!buffers || !out || width == 0 || height == 0) return CSL_ERR_INVALID;
    if (count > 64) return CSL_ERR_TOO_LARGE;
    pe_count = width * height;
    for (i = 0; i < count; i++)
        total += buffers[i].total_bytes ? (buffers[i].total_bytes + pe_count - 1) / pe_count : buffers[i].bytes_per_pe;
    out->residency = residency; out->grid.width = width; out->grid.height = height;
    out->grid.name = residency == CSL_RES_FULL_RESIDENT ? "full_resident" : "layer_streaming";
    out->buffers = buffers; out->buffer_count = count;
    out->stream_stages = NULL; out->stream_stage_count = 0;
    out->pe_count = pe_count;
    out->total_bytes_per_pe = total; out->capacity_bytes_per_pe = CSL_PE_SRAM_BYTES;
    out->persistent_bytes_per_pe = total; out->streamed_working_set_bytes_per_pe = 0;
    out->total_persistent_bytes = total * pe_count; out->total_streamed_bytes = 0;
    out->total_model_bytes = out->total_persistent_bytes;
    out->total_sram_available = (uint64_t)pe_count * CSL_PE_SRAM_BYTES;
    out->utilization_pct = total * 100 / CSL_PE_SRAM_BYTES > 100 ? 100 : total * 100 / CSL_PE_SRAM_BYTES;
    out->fits = total <= CSL_PE_SRAM_BYTES;
    return CSL_OK;
}
int csl_better_grid(uint32_t cand_w, uint32_t cand_h, uint32_t cur_w, uint32_t cur_h, int prefer_square) {
    uint64_t cand = (uint64_t)cand_w * cand_h, cur = (uint64_t)cur_w * cur_h;
    uint32_t cand_delta, cur_delta;
    if (cand < cur) return 1;
    if (cand > cur) return 0;
    if (!prefer_square) return cand_w < cur_w;
    cand_delta = cand_w > cand_h ? cand_w - cand_h : cand_h - cand_w;
    cur_delta = cur_w > cur_h ? cur_w - cur_h : cur_h - cur_w;
    return cand_delta < cur_delta;
}
CslErr csl_derive_grid(uint32_t max_width, uint32_t max_height, const CslBufferPlan *buffers, size_t count, int prefer_square, CslDerivedGrid *out) {
    uint32_t w, h; int have = 0; CslDerivedGrid best;
    CslMemoryPlan cand;
    if (!out || max_width == 0 || max_height == 0 || !buffers) return CSL_ERR_INVALID;
    best.width = 0; best.height = 0; best.name = "derived";
    for (w = 1; w <= max_width; w++) {
        for (h = 1; h <= max_height; h++) {
            if (csl_plan_memory(CSL_RES_FULL_RESIDENT, w, h, buffers, count, &cand) != CSL_OK) continue;
            if (!cand.fits) continue;
            if (!have || csl_better_grid(w, h, best.width, best.height, prefer_square)) {
                best.width = w; best.height = h; have = 1;
            }
        }
    }
    if (!have) return CSL_ERR_INVALID;
    *out = best;
    return CSL_OK;
}
CslErr csl_emit_memory_plan_json(CslBuf *out, const CslMemoryPlan *plan) {
    size_t i; CslErr e;
    const char *placement_name;
    if (!out || !plan) return CSL_ERR_INVALID;
    PUT("{\n  \"schemaVersion\": 1, \n  \"artifactKind\": \"csl_memory_plan\", \n  \"target\": \"wse3\", \n  \"contract\": \"explicit_memory_plan\", \n  \"grid\": { \"width\": ");
    PUTI(plan->grid.width);
    PUT(", \"height\": "); PUTI(plan->grid.height);
    PUT(" },\n  \"peCount\": "); PUTI(plan->pe_count);
    PUT(",\n  \"residencyMode\": "); e = csl_emit_json_string(out, plan->residency == CSL_RES_FULL_RESIDENT ? "full_resident" : "layer_streaming"); if (e) return e;
    PUT(",\n  \"totalModelBytes\": "); PUTI(plan->total_model_bytes);
    PUT(",\n  \"totalPersistentBytes\": "); PUTI(plan->total_persistent_bytes);
    PUT(",\n  \"totalStreamedBytes\": "); PUTI(plan->total_streamed_bytes);
    PUT(",\n  \"persistentBytesPerPe\": "); PUTI(plan->persistent_bytes_per_pe);
    PUT(",\n  \"streamedWorkingSetBytesPerPe\": "); PUTI(plan->streamed_working_set_bytes_per_pe);
    PUT(",\n  \"totalSramAvailable\": "); PUTI(plan->total_sram_available);
    PUT(",\n  \"utilizationPct\": "); PUTI(plan->utilization_pct);
    PUT(",\n  \"fits\": "); { CslErr e2 = csl_buf_puts(out, plan->fits ? "true" : "false"); if (e2) return e2; }
    PUT(",\n  \"buffers\": [\n");
    for (i = 0; i < plan->buffer_count; i++) {
        placement_name = plan->buffers[i].placement == CSL_PLACE_KV_CACHE ? "kv_cache" : plan->buffers[i].placement == CSL_PLACE_EMBEDDINGS ? "embeddings" : plan->buffers[i].placement == CSL_PLACE_STREAMED ? "streamed" : "persistent";
        PUT("    { \"name\": "); e = csl_emit_json_string(out, plan->buffers[i].name); if (e) return e;
        PUT(", \"kind\": "); e = csl_emit_json_string(out, csl_buffer_kind_name(plan->buffers[i].kind)); if (e) return e;
        PUT(", \"placement\": "); e = csl_emit_json_string(out, placement_name); if (e) return e;
        PUT(", \"bytesTotal\": "); PUTI(plan->buffers[i].total_bytes);
        PUT(", \"bytesPerPe\": "); PUTI(plan->buffers[i].bytes_per_pe);
        PUT(" }");
        if (i + 1 < plan->buffer_count) PUT(",");
        PUT("\n");
    }
    PUT("  ],\n  \"streamStages\": [\n");
    for (i = 0; i < plan->stream_stage_count; i++) {
        PUT("    { \"name\": "); e = csl_emit_json_string(out, plan->stream_stages[i].name); if (e) return e;
        PUT(", \"kind\": "); e = csl_emit_json_string(out, csl_buffer_kind_name(plan->stream_stages[i].kind)); if (e) return e;
        PUT(", \"repeatCount\": "); PUTI(plan->stream_stages[i].repeat_count);
        PUT(", \"bytesTotal\": "); PUTI(plan->stream_stages[i].bytes_total);
        PUT(", \"bytesPerPe\": "); PUTI(plan->stream_stages[i].bytes_per_pe);
        PUT(" }");
        if (i + 1 < plan->stream_stage_count) PUT(",");
        PUT("\n");
    }
    PUT("  ]\n}\n");
    return CSL_OK;
}
