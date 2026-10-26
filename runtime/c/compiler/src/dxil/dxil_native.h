/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_NATIVE_H
#define DOE_DXIL_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"
#include "dxil_ir.h"

/* Native DXIL emitter. Faithful port of emit_dxil_native.zig: it walks
 * a Doe IR module (dxil_ir.h) and produces a DXBC container through
 * the existing builder, serializer, and container writers, with no
 * external toolchain. The internal budgets are Zig's: bitcode
 * 192 KiB, DXIL part 200 KiB, at most 64 bound resources, output
 * 256 KiB as the caller buffer's contract (the caller buffer itself
 * is the bound; a container larger than out_len fails with
 * OutputTooLarge).
 *
 * Zig behavior preserved deliberately, including placeholders:
 * param_ref/local_ref/global_ref/construct evaluate to i32 constant
 * 0 (construct and user calls do NOT evaluate their arguments);
 * assign evaluates only its rhs and performs no store; if_ emits
 * both branches in order; address_of/deref fail with
 * UnsupportedConstruct; collected resources are never emitted, only
 * the 65th bound resource fails. Walk recursion is capped at depth
 * 64 (Zig recurses without bound); deeper nesting fails visibly with
 * InvalidIr. The first error aborts the walk and is reported. */

DxilStatus dxil_native_emit(const DxilIrModule *module, uint8_t *out,
    size_t out_len, size_t *written);

#endif /* DOE_DXIL_NATIVE_H */
