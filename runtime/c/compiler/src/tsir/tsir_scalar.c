/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "tsir_scalar.h"

#include <math.h>
#include <string.h>

static uint32_t f32_bits(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float bits_f32(uint32_t bits) {
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint16_t read_u16_le(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_u16_le(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

TsirStatus tsir_reduction_identity_f32(TsirReductionOp op, float *out) {
    if (out == NULL) {
        return TSIR_ERR_INVALID;
    }
    switch (op) {
        case TSIR_REDUCTION_SUM:
            *out = 0.0f;
            return TSIR_OK;
        case TSIR_REDUCTION_PRODUCT:
            *out = 1.0f;
            return TSIR_OK;
        case TSIR_REDUCTION_MIN:
            *out = INFINITY;
            return TSIR_OK;
        case TSIR_REDUCTION_MAX:
            *out = -INFINITY;
            return TSIR_OK;
        case TSIR_REDUCTION_SOFTMAX_STABLE:
            /* Zig reaches unreachable here and traps. C refuses
             * visibly instead; neither path produces a value. */
            return TSIR_ERR_INVALID;
        default:
            return TSIR_ERR_INVALID;
    }
}

TsirStatus tsir_combine_f32(TsirReductionOp op, float acc, float val,
    float *out) {
    if (out == NULL) {
        return TSIR_ERR_INVALID;
    }
    switch (op) {
        case TSIR_REDUCTION_SUM:
            *out = acc + val;
            return TSIR_OK;
        case TSIR_REDUCTION_PRODUCT:
            *out = acc * val;
            return TSIR_OK;
        case TSIR_REDUCTION_MIN:
            /* IEEE minimum with NaN propagation: if either operand is
             * NaN the result is NaN, matching the propagate contract
             * the Zig scalar helper documents. fminf would drop NaN. */
            if (isnan(acc) || isnan(val)) {
                *out = NAN;
            } else {
                *out = acc < val ? acc : val;
            }
            return TSIR_OK;
        case TSIR_REDUCTION_MAX:
            if (isnan(acc) || isnan(val)) {
                *out = NAN;
            } else {
                *out = acc > val ? acc : val;
            }
            return TSIR_OK;
        case TSIR_REDUCTION_SOFTMAX_STABLE:
            return TSIR_ERR_INVALID;
        default:
            return TSIR_ERR_INVALID;
    }
}

float tsir_f16_bits_to_f32(uint16_t bits) {
    uint32_t sign = ((uint32_t)bits & 0x8000u) << 16;
    uint32_t exp = ((uint32_t)bits >> 10) & 0x1fu;
    uint32_t mant = (uint32_t)bits & 0x03ffu;
    uint32_t out_bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            out_bits = sign;
        } else {
            /* Subnormal f16: renormalize by shifting the mantissa
             * until the leading one appears, tracking the exponent. */
            int32_t e = -14;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1;
                e -= 1;
            }
            mant &= 0x03ffu;
            out_bits = sign | ((uint32_t)(e + 127) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out_bits = sign | 0x7f800000u | (mant << 13);
    } else {
        out_bits = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    return bits_f32(out_bits);
}

uint16_t tsir_f32_to_f16_bits(float value) {
    uint32_t bits = f32_bits(value);
    uint32_t sign = (bits >> 16) & 0x8000u;
    uint32_t exp_field = (bits >> 23) & 0xffu;
    uint32_t mant = bits & 0x7fffffu;
    if (exp_field == 0xffu) {
        if (mant != 0) {
            return (uint16_t)(sign | 0x7e00u);
        }
        return (uint16_t)(sign | 0x7c00u);
    }
    if (exp_field == 0) {
        /* f32 zero or subnormal: far below the f16 subnormal range. */
        return (uint16_t)sign;
    } else {
        int32_t e = (int32_t)exp_field - 127;
        if (e >= 16) {
            return (uint16_t)(sign | 0x7c00u);
        }
        if (e >= -14) {
            uint32_t half = (uint32_t)(e + 15) << 10;
            uint32_t m = mant >> 13;
            uint32_t rem = mant & 0x1fffu;
            uint32_t rounded = half | m;
            if (rem > 0x1000u || (rem == 0x1000u && (rounded & 1u) != 0)) {
                rounded += 1;
            }
            return (uint16_t)(sign | rounded);
        } else {
            /* Subnormal f16: value = (2^23 + mant) * 2^(e - 23);
             * scaled by 2^24 this is (2^23 + mant) * 2^(e + 1), so
             * shift right by -(e + 1) with round-to-nearest-even. */
            int32_t shift = -(e + 1);
            uint32_t full = (1u << 23) | mant;
            uint32_t rounded = 0;
            if (shift >= 32) {
                return (uint16_t)sign;
            } else {
                uint32_t kept = full >> shift;
                uint32_t rem_mask = shift == 0 ? 0u : ((1u << shift) - 1u);
                uint32_t rem = full & rem_mask;
                uint32_t half_bit = shift == 0 ? 0u : (1u << (shift - 1));
                rounded = kept;
                if (rem > half_bit ||
                    (rem == half_bit && (kept & 1u) != 0)) {
                    rounded = kept + 1;
                }
            }
            return (uint16_t)(sign | rounded);
        }
    }
}

uint16_t tsir_f32_to_bf16_rne(float value) {
    uint32_t bits = f32_bits(value);
    uint32_t exp = (bits >> 23) & 0xffu;
    uint32_t mant = bits & 0x7fffffu;
    if (exp == 0xffu && mant != 0) {
        return (uint16_t)((bits >> 16) | 0x40u);
    } else {
        uint32_t lsb = (bits >> 16) & 1u;
        uint32_t bias = 0x7fffu + lsb;
        uint32_t rounded = bits + bias;
        return (uint16_t)(rounded >> 16);
    }
}

float tsir_bf16_to_f32(uint16_t bits) {
    return bits_f32((uint32_t)bits << 16);
}

TsirStatus tsir_read_f32(const uint8_t *bytes, size_t byte_len,
    TsirScalarKind elem, size_t elem_index, float *out) {
    size_t elem_bytes = 0;
    size_t offset = 0;
    if (bytes == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    elem_bytes = tsir_scalar_byte_size(elem);
    if (elem_bytes == 0) {
        return TSIR_ERR_INVALID;
    }
    if (elem != TSIR_SCALAR_F32 && elem != TSIR_SCALAR_F16 &&
        elem != TSIR_SCALAR_BF16) {
        return TSIR_ERR_UNSUPPORTED;
    }
    if (elem_index > SIZE_MAX / elem_bytes) {
        return TSIR_ERR_OVERFLOW;
    }
    offset = elem_index * elem_bytes;
    if (offset > byte_len || byte_len - offset < elem_bytes) {
        return TSIR_ERR_RANGE;
    }
    switch (elem) {
        case TSIR_SCALAR_F32:
            *out = bits_f32(read_u32_le(bytes + offset));
            return TSIR_OK;
        case TSIR_SCALAR_F16:
            *out = tsir_f16_bits_to_f32(read_u16_le(bytes + offset));
            return TSIR_OK;
        case TSIR_SCALAR_BF16:
            *out = tsir_bf16_to_f32(read_u16_le(bytes + offset));
            return TSIR_OK;
        case TSIR_SCALAR_I32:
        case TSIR_SCALAR_U32:
        default:
            return TSIR_ERR_UNSUPPORTED;
    }
}

TsirStatus tsir_write_f32(uint8_t *bytes, size_t byte_len,
    TsirScalarKind elem, size_t elem_index, float value) {
    size_t elem_bytes = 0;
    size_t offset = 0;
    if (bytes == NULL) {
        return TSIR_ERR_INVALID;
    }
    elem_bytes = tsir_scalar_byte_size(elem);
    if (elem_bytes == 0) {
        return TSIR_ERR_INVALID;
    }
    if (elem != TSIR_SCALAR_F32 && elem != TSIR_SCALAR_F16 &&
        elem != TSIR_SCALAR_BF16) {
        return TSIR_ERR_UNSUPPORTED;
    }
    if (elem_index > SIZE_MAX / elem_bytes) {
        return TSIR_ERR_OVERFLOW;
    }
    offset = elem_index * elem_bytes;
    if (offset > byte_len || byte_len - offset < elem_bytes) {
        return TSIR_ERR_RANGE;
    }
    switch (elem) {
        case TSIR_SCALAR_F32:
            write_u32_le(bytes + offset, f32_bits(value));
            return TSIR_OK;
        case TSIR_SCALAR_F16:
            write_u16_le(bytes + offset, tsir_f32_to_f16_bits(value));
            return TSIR_OK;
        case TSIR_SCALAR_BF16:
            write_u16_le(bytes + offset, tsir_f32_to_bf16_rne(value));
            return TSIR_OK;
        case TSIR_SCALAR_I32:
        case TSIR_SCALAR_U32:
        default:
            return TSIR_ERR_UNSUPPORTED;
    }
}

TsirStatus tsir_expected_bytes(const TsirBufferBinding *binding,
    uint64_t *out) {
    uint64_t elems = 1;
    size_t i = 0;
    if (binding == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (binding->logical_shape_len > 0 && binding->logical_shape == NULL) {
        return TSIR_ERR_INVALID;
    }
    if (tsir_scalar_byte_size(binding->elem) == 0) {
        return TSIR_ERR_INVALID;
    }
    for (i = 0; i < binding->logical_shape_len; i++) {
        uint64_t dim = binding->logical_shape[i];
        if (dim == 0) {
            *out = 0;
            return TSIR_OK;
        }
        if (elems > UINT64_MAX / dim) {
            return TSIR_ERR_OVERFLOW;
        }
        elems *= dim;
    }
    if (elems > UINT64_MAX / tsir_scalar_byte_size(binding->elem)) {
        return TSIR_ERR_OVERFLOW;
    }
    *out = elems * tsir_scalar_byte_size(binding->elem);
    return TSIR_OK;
}

size_t tsir_count_read_only_bindings(const TsirSemanticFunction *func) {
    size_t count = 0;
    size_t i = 0;
    if (func == NULL) {
        return 0;
    }
    for (i = 0; i < func->binding_count; i++) {
        if (!func->bindings[i].read_write) {
            count += 1;
        }
    }
    return count;
}

bool tsir_input_for_read_only_binding(const TsirSemanticFunction *func,
    const uint8_t *const *inputs, const size_t *input_lens,
    size_t input_count, uint32_t binding_index,
    const uint8_t **out_data, size_t *out_len) {
    size_t slot = 0;
    size_t i = 0;
    if (func == NULL || inputs == NULL || input_lens == NULL ||
        out_data == NULL || out_len == NULL) {
        return false;
    }
    if (binding_index >= func->binding_count) {
        return false;
    }
    if (func->bindings[binding_index].read_write) {
        return false;
    }
    for (i = 0; i < binding_index; i++) {
        if (!func->bindings[i].read_write) {
            slot += 1;
        }
    }
    if (slot >= input_count) {
        return false;
    }
    *out_data = inputs[slot];
    *out_len = input_lens[slot];
    return true;
}
