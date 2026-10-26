/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_HLSL_H
#define DOE_DXIL_HLSL_H

#include <stddef.h>
#include <stdint.h>

#include "dxil_error.h"
#include "dxil_ir.h"

/* Bounded HLSL emitter for the DXC fallback path. Port of the
 * compute-function surface of emit_hlsl.zig (plus its maps and
 * builtin tables) for modules in the dxil_ir.h subset. Output goes to
 * a caller buffer; exhaustion is OutputTooLarge; the first error is
 * reported. Types, operator spellings, bound-global registers, entry
 * semantics, and statement/expression forms follow Zig.
 *
 * Bounds, all failing visibly: the header is a single comment line,
 * NOT Zig's doe_atomic, doe_pack, and doe_unpack helper prologue, so
 * atomic and pack/unpack builtins fail with UnsupportedBuiltin here;
 * vertex and fragment stage functions fail with
 * UnsupportedConstruct (Zig's stage wrapper with impl function,
 * output struct, and wrapper is not in this bounded emitter); a
 * ref-typed local declaration fails with InvalidIr (Zig's pointer
 * capture machinery is not here); the runtime-array write guard is
 * not emitted (the subset analyzer never generates the guard
 * pattern); a continue inside a loop with a continuing block is
 * supported through the Zig break-flag form. See gaps.md. */

DxilStatus dxil_hlsl_emit(const DxilIrModule *module, char *out,
    size_t out_len, size_t *written);

#endif /* DOE_DXIL_HLSL_H */
