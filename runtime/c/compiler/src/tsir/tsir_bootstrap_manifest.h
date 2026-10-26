/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_TSIR_BOOTSTRAP_MANIFEST_H
#define DOE_TSIR_BOOTSTRAP_MANIFEST_H

#include "tsir_schema.h"
#include "../../include/doe_compiler.h"

#define TSIR_BOOTSTRAP_MAX_WGSL_BYTES (1u << 20)

/* Failure metadata owns its diagnostic text. Stage and input point to static
 * literals or the borrowed kernel argument supplied to manifest_source. A
 * nonzero system_error preserves the first filesystem error. */
typedef struct TsirBootstrapManifestError {
    const char *stage;
    const char *input;
    int system_error;
    DoeCompilerDiagnostic diagnostic;
} TsirBootstrapManifestError;

/* Lower actual WGSL using the shared C parser/sema/typed IR owner, plan the
 * three non-CSL targets and append their manifest entry JSON (comma-separated,
 * without outer brackets). kernel selects only exactness metadata; it never
 * selects shader meaning. root supplies retained emitter source files whose
 * bytes identify the reference emitter, as the Zig manifest contract requires.
 * No Zig compiler is invoked. The caller owns the initialized output buffer.
 * Failure keeps its previous bytes and length; an allocation failure while
 * publishing may set out->err. No pointers to source are retained. */
TsirStatus tsir_bootstrap_manifest_source(const char *root, const char *kernel,
    const char *source, size_t source_length, TsirBuffer *out,
    TsirBootstrapManifestError *error);

/* Emit the complete nine-entry manifest JSON for the three fixed bootstrap
 * WGSL paths under root. Each source is bounded to 1 MiB. Output is staged
 * privately and published only after all sources/targets succeed. */
TsirStatus tsir_bootstrap_manifest_generate(const char *root, TsirBuffer *out,
    TsirBootstrapManifestError *error);

#endif
