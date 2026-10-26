/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_HLSL_H
#define DOE_DXIL_HLSL_H

#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"
#include "dxil_ir.h"

/* Bounded caller-built typed IR serializer. Parsing and WGSL semantic rules
 * remain with the canonical source compiler; this entry validates structural
 * table/range/identity safety before walking the caller-owned module. It does
 * not cache source text, and every call observes current IR contents.
 *
 * Compute and graphics wrappers, typed atomic/numeric/texture helpers, reference
 * index snapshots, and runtime-array guards use the existing IR owners. The
 * production WGSL-to-DXC API uses the shared compiler's HLSL target directly.
 * Caller output is borrowed; written is published only on success, and the
 * first error aborts. Source-layer typing, physical layout/device qualification
 * and the explicit remaining compatibility boundaries are not inferred from a
 * successful serialization. See source-integration.md and gaps.md. */

DxilStatus dxil_hlsl_emit(const DxilIrModule *module, char *out,
    size_t out_len, size_t *written);

#endif /* DOE_DXIL_HLSL_H */
