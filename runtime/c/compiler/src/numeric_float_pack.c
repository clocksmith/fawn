/* SPDX-License-Identifier: MIT */
#include "sema.h"
#include "numeric.h"
#include <math.h>
#include <string.h>

bool float_pack_unpack(NumericBuiltinKind kind) {
    return kind >= BUILTIN_UNPACK4X8_SNORM && kind <= BUILTIN_UNPACK2X16_FLOAT;
}
uint32_t float_pack_width(NumericBuiltinKind kind) {
    return kind == BUILTIN_PACK4X8_SNORM || kind == BUILTIN_PACK4X8_UNORM ||
        kind == BUILTIN_UNPACK4X8_SNORM || kind == BUILTIN_UNPACK4X8_UNORM ? 4u : 2u;
}
bool float_pack_signed(NumericBuiltinKind kind) {
    return kind == BUILTIN_PACK4X8_SNORM || kind == BUILTIN_PACK2X16_SNORM ||
        kind == BUILTIN_UNPACK4X8_SNORM || kind == BUILTIN_UNPACK2X16_SNORM;
}
static bool from_double(Source *s, uint32_t offset, double value, ConstantValue *out) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    NumericValue converted;
    if (!numeric_convert(s, offset, (NumericValue){bits,IR_ABSTRACT_FLOAT}, IR_F32, &converted)) return false;
    *out = (ConstantValue){.type=IR_F32,.bits=converted.bits};
    return true;
}
bool float_pack_constant(Source *s, uint32_t offset, NumericBuiltinKind kind,
    ConstantValue input, TypeId result_type, ConstantValue *out) {
    const bool unpack = float_pack_unpack(kind), quantize = kind == BUILTIN_QUANTIZE_F16;
    const bool half = kind == BUILTIN_PACK2X16_FLOAT || kind == BUILTIN_UNPACK2X16_FLOAT || quantize;
    const bool sign = float_pack_signed(kind);
    const uint32_t width = quantize ? type_vector_width(s->compiler, result_type) : float_pack_width(kind);
    const uint32_t bits = width == 4u ? 8u : 16u, mask = (1u << bits) - 1u;
    const double scale = (double)(sign ? mask >> 1u : mask);
    uint32_t packed = 0u;
    ConstantValue lanes[4];
    for (uint32_t i = 0u; i < width; ++i) {
        if (unpack) {
            const uint32_t word = ((uint32_t)input.bits >> (i * bits)) & mask;
            const int64_t integer = sign && (word & (1u << (bits - 1u))) != 0u ?
                (int64_t)word - (INT64_C(1) << bits) : (int64_t)word;
            const double value = half ? numeric_half_value(word) : fmax(-1.0, (double)integer / scale);
            if (!from_double(s, offset, value, &lanes[i])) return false;
        } else {
            ConstantValue lane = input;
            if (type_vector_width(s->compiler, input.type) != 1u && !constant_project(s, input, i, &lane)) return false;
            NumericValue wide;
            if (!numeric_convert(s, offset, (NumericValue){lane.bits,IR_F32}, IR_ABSTRACT_FLOAT, &wide)) return false;
            double value;
            memcpy(&value, &wide.bits, sizeof(value));
            uint32_t word;
            if (half) {
                if (fabs(value) > 65504.0)
                    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "half packing input is outside the finite binary16 range");
                NumericValue converted;
                if (!numeric_half_pack(s, offset, value, &converted)) return false;
                word = (uint32_t)converted.bits;
                if (quantize && !from_double(s, offset, numeric_half_value(word), &lanes[i])) return false;
            } else {
                /* WGSL ties go toward positive infinity, unlike GLSL Round. */
                value = fmin(1.0, fmax(sign ? -1.0 : 0.0, value));
                word = (uint32_t)(int64_t)floor(0.5 + value * scale) & mask;
            }
            if (!quantize) packed |= word << (i * bits);
        }
    }
    if (!unpack && !quantize) { *out = (ConstantValue){.type=IR_U32,.bits=packed}; return true; }
    if (width == 1u) { *out = lanes[0]; return true; }
    return constant_composite(s, result_type, lanes, width, false, out);
}
