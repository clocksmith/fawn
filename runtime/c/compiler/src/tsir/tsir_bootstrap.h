/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_BOOTSTRAP_H
#define DOE_TSIR_BOOTSTRAP_H

#include "tsir_schema.h"

/* Bootstrap tools JSON layer
 * (tsir_bootstrap_manifest_inputs.zig and
 * tsir_bootstrap_oracle.zig). The JSON reader is the repo bounded
 * reader (tsir_json.h, reused from runtime/c, not hand-rolled);
 * the writers below emit Zig's exact field orders.
 *
 * The entry writer accepts explicit identities. The actual WGSL pipeline
 * and nine-entry manifest tool are exposed in tsir_bootstrap_manifest.h.
 * The oracle runner uses tsir_reference_run with an empty realization.
 *
 * Oracle status JSON, exactly as Zig writeStatus writes it
 * (detail, referenceHash, status, plus a trailing newline):
 * success is status "pass" with detail
 * "zig bootstrap TSIR oracle executed"; NotImplemented is status
 * "not_implemented" with detail
 * "bootstrap oracle cannot execute this input" and a null hash;
 * malformed JSON is status "not_implemented" with detail
 * "bootstrap oracle input JSON is unsupported or missing
 * kernel/inputs" and a null hash. Other failures (allocation)
 * return a TsirStatus and no status JSON. Input over the 16 MiB
 * cap, malformed hex and reference execution failures return non-OK,
 * matching Zig's process failure boundary. */

#define TSIR_BOOTSTRAP_MAX_INPUT_BYTES (16u * 1024u * 1024u)

const char *tsir_bootstrap_frontend_version(void);
const char *tsir_bootstrap_compiler_version(void);

/* Borrowed result: either a static mapped name ("rms_norm",
 * "fused_gemv") or the input with the "doe.tsir.bootstrap."
 * prefix stripped. */
const char *tsir_bootstrap_canonical_kernel(const char *kernel);

/* One manifest entry, in Zig writeEntryInput field order. All
 * digest strings must be exactly 64 lowercase hex characters;
 * tolerance_metric NULL means "" and tolerance_epsilon NULL
 * means "0" (Zig's KernelSpec defaults). tolerance_epsilon is
 * copied as a raw JSON number token. rejection_reasons NULL
 * means an empty array. kernel_ref is the bare kernel name;
 * "doe.tsir.bootstrap." is prepended. Optional byte lengths and length arrays
 * follow tsir_schema.h; positive lengths need not include a terminator. */
typedef struct TsirManifestEntry {
    const char *backend;
    const char *emitter_digest;
    const char *exactness_class;
    const char *const *algorithm_exact_invariants;
    size_t invariant_count;
    const char *tolerance_metric;
    const char *tolerance_epsilon;
    const char *kernel_ref;
    const char *const *rejection_reasons;
    size_t rejection_count;
    const char *target_descriptor_correctness_hash;
    const char *tsir_realization_digest;
    const char *tsir_semantic_digest;
    size_t backend_length;
    size_t emitter_digest_length;
    size_t exactness_class_length;
    size_t tolerance_metric_length;
    size_t tolerance_epsilon_length;
    size_t kernel_ref_length;
    size_t target_descriptor_correctness_hash_length;
    size_t tsir_realization_digest_length;
    size_t tsir_semantic_digest_length;
    const size_t *algorithm_exact_invariant_lengths;
    const size_t *rejection_reason_lengths;
} TsirManifestEntry;

/* Validate before writing and stage privately: invalid input or allocation
 * failure preserves earlier bytes. Final publication may set buf->err.
 * Pointer arrays require non-NULL entries; tolerance_epsilon must be a JSON
 * number token. All strings are escaped by the shared entry writer. */
TsirStatus tsir_bootstrap_write_manifest_entry(TsirBuffer *buf,
    const TsirManifestEntry *entry);

/* Fill a manifest entry template for one of the three bootstrap
 * kernels (fused_gemv, rms_norm, gather) with Zig's exactness
 * class, invariants, and tolerance values. backend and digests
 * are caller-supplied and borrowed, not validated beyond the
 * 64-hex digest check in the writer. Unknown kernel returns
 * TSIR_ERR_UNSUPPORTED. */
TsirStatus tsir_bootstrap_manifest_template(const char *kernel,
    const char *backend, const char *emitter_digest,
    const char *target_descriptor_correctness_hash,
    const char *tsir_realization_digest,
    const char *tsir_semantic_digest, TsirManifestEntry *out);

/* Run the oracle for one kernel over one input JSON document.
 * kernel is the --kernel argument (canonicalized); the document
 * must carry the same canonical kernel in its "kernel" field.
 * On success *out_status_json holds a NUL-terminated status
 * JSON string (caller frees); the function itself returns
 * TSIR_OK for both pass and not_implemented outcomes, and a
 * non-OK status for argument, input-size, allocation, malformed-hex or
 * reference execution failures described above. */
TsirStatus tsir_bootstrap_oracle_run(const char *kernel,
    const char *input_json, size_t input_len,
    char **out_status_json, size_t *out_len);

#endif
