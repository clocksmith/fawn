// SPDX-License-Identifier: MIT
// emit_csl_mem_plan.zig: WSE-3 residency and grid planning.
// Residency modes: full_resident (all weights/state resident) and
// layer_streaming (KV/state resident, weights stream one working-set
// chunk at a time). Buffer kinds follow Zig's plan buffer list
// (kv_cache, activation_scratch, decode_position, sliding_window,
// output_logits, token_embeddings, ple_table, ple_projection, ple_norm,
// layer_weights, embedding_rows, ple_rows). deriveGrid scans candidate
// rectangles width-outer, height-inner, and keeps the betterGrid
// winner (fewer PEs; tie: smaller width, or smaller |width-height|
// when prefer_square). Zig's planForGrid derives totals from
// ModelConfig + HostPlan and auto-chooses the residency mode; here
// the caller supplies buffer totals and chooses the mode (see mapping).
#ifndef CSL_MEM_PLAN_H
#define CSL_MEM_PLAN_H
#include "csl_kernel.h"
typedef enum { CSL_RES_FULL_RESIDENT, CSL_RES_LAYER_STREAMING } CslResidencyMode;
typedef enum {
    CSL_BUF_KV_CACHE, CSL_BUF_ACTIVATION_SCRATCH, CSL_BUF_DECODE_POSITION,
    CSL_BUF_SLIDING_WINDOW, CSL_BUF_OUTPUT_LOGITS, CSL_BUF_TOKEN_EMBEDDINGS,
    CSL_BUF_PLE_TABLE, CSL_BUF_PLE_PROJECTION, CSL_BUF_PLE_NORM,
    CSL_BUF_LAYER_WEIGHTS, CSL_BUF_EMBEDDING_ROWS, CSL_BUF_PLE_ROWS
} CslBufferKind;
typedef enum { CSL_PLACE_PERSISTENT, CSL_PLACE_STREAMED, CSL_PLACE_KV_CACHE, CSL_PLACE_EMBEDDINGS } CslBufferPlacement;
typedef struct { CslBufferKind kind; const char *name; uint64_t bytes_per_pe; uint64_t total_bytes; CslBufferPlacement placement; } CslBufferPlan;
typedef struct { const char *name; CslBufferKind kind; uint32_t repeat_count; uint64_t bytes_total; uint64_t bytes_per_pe; } CslStreamStage;
typedef struct { const char *name; uint32_t width, height; } CslDerivedGrid;
typedef struct {
    CslResidencyMode residency; CslDerivedGrid grid; uint32_t pe_count;
    const CslBufferPlan *buffers; size_t buffer_count;
    const CslStreamStage *stream_stages; size_t stream_stage_count;
    uint64_t total_model_bytes, total_persistent_bytes, total_streamed_bytes;
    uint64_t persistent_bytes_per_pe, streamed_working_set_bytes_per_pe;
    uint64_t total_sram_available, utilization_pct;
    uint64_t total_bytes_per_pe, capacity_bytes_per_pe; int fits;
} CslMemoryPlan;
const char *csl_buffer_kind_name(CslBufferKind k);
CslErr csl_plan_memory(CslResidencyMode residency, uint32_t width, uint32_t height, const CslBufferPlan *buffers, size_t count, CslMemoryPlan *out);
CslErr csl_derive_grid(uint32_t max_width, uint32_t max_height, const CslBufferPlan *buffers, size_t count, int prefer_square, CslDerivedGrid *out);
/* betterGrid preference, exactly as in Zig: fewer PEs wins; on a tie,
   prefer_square picks the smaller |width-height|, otherwise smaller width. */
int csl_better_grid(uint32_t cand_w, uint32_t cand_h, uint32_t cur_w, uint32_t cur_h, int prefer_square);
CslErr csl_emit_memory_plan_json(CslBuf *out, const CslMemoryPlan *plan);
#endif
