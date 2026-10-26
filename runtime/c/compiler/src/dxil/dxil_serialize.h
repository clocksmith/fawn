/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_DXIL_SERIALIZE_H
#define DOE_DXIL_SERIALIZE_H

#include <stddef.h>
#include <stdint.h>

#include "dxil_builder.h"
#include "dxil_error.h"

/* DXIL bitcode serializer. Faithful port of dxil_serialize.zig: writes a
 * populated builder as LLVM 3.7 bitcode with the MODULE block and its
 * TYPE, CONSTANTS, FUNCTION, METADATA, and VALUE_SYMTAB sub-blocks in
 * the order LLVM's bitcode reader requires.
 *
 * Ownership: the builder and bodies are borrowed (read-only) for the
 * call; `out` is a caller-owned buffer. On DXIL_OUTPUT_TOO_LARGE the
 * written length is undefined and the caller must discard the buffer. */
DxilStatus dxil_serialize(const DxilBuilder *builder,
    const DxilFunctionBody *bodies, size_t body_count,
    uint8_t *out, size_t out_len, size_t *written);

#endif /* DOE_DXIL_SERIALIZE_H */
