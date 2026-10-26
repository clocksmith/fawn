/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_DIGEST_H
#define DOE_TSIR_DIGEST_H

#include "tsir_schema.h"

/* Canonical JSON and digests, mirroring digest.zig. Field orders are
 * the Zig lex-sorted camelCase orders, reproduced exactly; see
 * parity-mapping.md rows D3-D18. The caller owns every returned buffer
 * and frees it with free(). A NULL string field in an input struct is
 * treated as invalid, never as an empty string, because Zig slices
 * distinguish nothing here but a NULL C pointer is a caller bug, not a
 * TSIR value. f64 formatting: shortest round-trip decimal digits,
 * rendered in plain decimal notation when the decimal exponent is in
 * [-6, 21), otherwise in scientific notation; this matches Zig's {d}
 * output for the plain cases the tests pin (0, 1, 0.5, 1.5) but is NOT
 * verified against Zig for arbitrary exponents, see gaps.md. NaN or
 * Inf f64 values are TSIR_ERR_INVALID, never serialized. */

TsirStatus tsir_canonicalize_semantic(const TsirSemantic *semantic,
    char **out_text, size_t *out_len);
TsirStatus tsir_canonicalize_realization(const TsirRealization *realization,
    char **out_text, size_t *out_len);
TsirStatus tsir_canonicalize_manifest_entry(
    const TsirManifestLoweringEntry *entry, char **out_text,
    size_t *out_len);
TsirStatus tsir_manifest_entry_digest(
    const TsirManifestLoweringEntry *entry, uint8_t out[32]);
TsirStatus tsir_digests_compute(const TsirSemantic *semantic,
    const TsirRealization *realization, const uint8_t emitter_digest[32],
    TsirDigests *out);
TsirStatus tsir_digests_compute_version(const TsirSemantic *semantic,
    const TsirRealization *realization, const char *emitter_version,
    TsirDigests *out);

#endif
