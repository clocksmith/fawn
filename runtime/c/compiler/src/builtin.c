/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

static const NumericBuiltin numeric_builtins[] = {
    {"pack4x8snorm", BUILTIN_PACK4X8_SNORM, 1u, BUILTIN_FLOAT_PACK, 0u},
    {"pack4x8unorm", BUILTIN_PACK4X8_UNORM, 1u, BUILTIN_FLOAT_PACK, 0u},
    {"pack2x16snorm", BUILTIN_PACK2X16_SNORM, 1u, BUILTIN_FLOAT_PACK, 0u},
    {"pack2x16unorm", BUILTIN_PACK2X16_UNORM, 1u, BUILTIN_FLOAT_PACK, 0u},
    {"pack2x16float", BUILTIN_PACK2X16_FLOAT, 1u, BUILTIN_FLOAT_PACK, 58u},
    {"unpack4x8snorm", BUILTIN_UNPACK4X8_SNORM, 1u, BUILTIN_FLOAT_PACK, 63u},
    {"unpack4x8unorm", BUILTIN_UNPACK4X8_UNORM, 1u, BUILTIN_FLOAT_PACK, 64u},
    {"unpack2x16snorm", BUILTIN_UNPACK2X16_SNORM, 1u, BUILTIN_FLOAT_PACK, 60u},
    {"unpack2x16unorm", BUILTIN_UNPACK2X16_UNORM, 1u, BUILTIN_FLOAT_PACK, 61u},
    {"unpack2x16float", BUILTIN_UNPACK2X16_FLOAT, 1u, BUILTIN_FLOAT_PACK, 62u},
    {"quantizeToF16", BUILTIN_QUANTIZE_F16, 1u, BUILTIN_FLOAT_PACK, 0u},
    {"dpdx", BUILTIN_DPDX, 1u, BUILTIN_DERIVATIVE, 0u},
    {"dpdy", BUILTIN_DPDY, 1u, BUILTIN_DERIVATIVE, 0u},
    {"fwidth", BUILTIN_FWIDTH, 1u, BUILTIN_DERIVATIVE, 0u},
    {"dpdxFine", BUILTIN_DPDX_FINE, 1u, BUILTIN_DERIVATIVE, 0u},
    {"dpdyFine", BUILTIN_DPDY_FINE, 1u, BUILTIN_DERIVATIVE, 0u},
    {"fwidthFine", BUILTIN_FWIDTH_FINE, 1u, BUILTIN_DERIVATIVE, 0u},
    {"dpdxCoarse", BUILTIN_DPDX_COARSE, 1u, BUILTIN_DERIVATIVE, 0u},
    {"dpdyCoarse", BUILTIN_DPDY_COARSE, 1u, BUILTIN_DERIVATIVE, 0u},
    {"fwidthCoarse", BUILTIN_FWIDTH_COARSE, 1u, BUILTIN_DERIVATIVE, 0u},
    {"subgroupAll", BUILTIN_SUBGROUP_ALL, 1u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupAny", BUILTIN_SUBGROUP_ANY, 1u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupBallot", BUILTIN_SUBGROUP_BALLOT, 1u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupElect", BUILTIN_SUBGROUP_ELECT, 0u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupBroadcast", BUILTIN_SUBGROUP_BROADCAST, 2u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupBroadcastFirst", BUILTIN_SUBGROUP_BROADCAST_FIRST, 1u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupShuffle", BUILTIN_SUBGROUP_SHUFFLE, 2u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupShuffleXor", BUILTIN_SUBGROUP_SHUFFLE_XOR, 2u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupShuffleUp", BUILTIN_SUBGROUP_SHUFFLE_UP, 2u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"subgroupShuffleDown", BUILTIN_SUBGROUP_SHUFFLE_DOWN, 2u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"quadBroadcast", BUILTIN_QUAD_BROADCAST, 2u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"quadSwapDiagonal", BUILTIN_QUAD_SWAP_DIAGONAL, 1u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"quadSwapX", BUILTIN_QUAD_SWAP_X, 1u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"quadSwapY", BUILTIN_QUAD_SWAP_Y, 1u, BUILTIN_SUBGROUP_EXCHANGE, 0u},
    {"frexp", BUILTIN_FREXP, 1u, BUILTIN_DECOMPOSE, 52u},
    {"modf", BUILTIN_MODF, 1u, BUILTIN_DECOMPOSE, 36u},
    {"ldexp", BUILTIN_LDEXP, 2u, BUILTIN_EXPONENT, 53u},
    {"extractBits", BUILTIN_EXTRACT_BITS, 3u, BUILTIN_BITFIELD, 0u},
    {"insertBits", BUILTIN_INSERT_BITS, 4u, BUILTIN_BITFIELD, 0u},
    {"fma", BUILTIN_FMA, 3u, BUILTIN_COMPONENT_FLOAT, 50u},
    {"step", BUILTIN_STEP, 2u, BUILTIN_COMPONENT_FLOAT, 48u},
    {"smoothstep", BUILTIN_SMOOTHSTEP, 3u, BUILTIN_COMPONENT_FLOAT, 0u},
    {"saturate", BUILTIN_SATURATE, 1u, BUILTIN_COMPONENT_FLOAT, 0u},
    {"reverseBits", BUILTIN_REVERSE_BITS, 1u, BUILTIN_COMPONENT_INTEGER, 0u},
    {"countLeadingZeros", BUILTIN_COUNT_LEADING_ZEROS, 1u, BUILTIN_COMPONENT_INTEGER, 75u},
    {"countTrailingZeros", BUILTIN_COUNT_TRAILING_ZEROS, 1u, BUILTIN_COMPONENT_INTEGER, 73u},
    {"firstLeadingBit", BUILTIN_FIRST_LEADING_BIT, 1u, BUILTIN_COMPONENT_INTEGER, 75u},
    {"firstTrailingBit", BUILTIN_FIRST_TRAILING_BIT, 1u, BUILTIN_COMPONENT_INTEGER, 73u},

    {"countOneBits", BUILTIN_COUNT_ONE_BITS, 1u, BUILTIN_COMPONENT_INTEGER, 0u},
    {"unpack4xU8", BUILTIN_UNPACK4X_U8, 1u, BUILTIN_PACKED_INTEGER, 0u},
    {"pack4xI8", BUILTIN_PACK4X_I8, 1u, BUILTIN_PACKED_INTEGER, 0u},
    {"pack4xU8", BUILTIN_PACK4X_U8, 1u, BUILTIN_PACKED_INTEGER, 0u},
    {"pack4xI8Clamp", BUILTIN_PACK4X_I8_CLAMP, 1u, BUILTIN_PACKED_INTEGER, 0u},
    {"pack4xU8Clamp", BUILTIN_PACK4X_U8_CLAMP, 1u, BUILTIN_PACKED_INTEGER, 0u},
    {"unpack4xI8", BUILTIN_UNPACK4X_I8, 1u, BUILTIN_PACKED_INTEGER, 0u},
    {"dot4I8Packed", BUILTIN_DOT4_I8_PACKED, 2u, BUILTIN_PACKED_INTEGER, 0u},
    {"dot4U8Packed", BUILTIN_DOT4_U8_PACKED, 2u, BUILTIN_PACKED_INTEGER, 0u},
    {"all", BUILTIN_ALL, 1u, BUILTIN_BOOLEAN_REDUCTION, 0u},
    {"any", BUILTIN_ANY, 1u, BUILTIN_BOOLEAN_REDUCTION, 0u},
    {"transpose", BUILTIN_TRANSPOSE, 1u, BUILTIN_MATRIX, 0u},
    {"determinant", BUILTIN_DETERMINANT, 1u, BUILTIN_MATRIX, 33u},
    {"min", BUILTIN_MIN, 2u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"max", BUILTIN_MAX, 2u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"clamp", BUILTIN_CLAMP, 3u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"exp", BUILTIN_EXP, 1u, BUILTIN_COMPONENT_FLOAT, 27u},
    {"tanh", BUILTIN_TANH, 1u, BUILTIN_COMPONENT_FLOAT, 21u},
    {"sqrt", BUILTIN_SQRT, 1u, BUILTIN_COMPONENT_FLOAT, 31u},
    {"ceil", BUILTIN_CEIL, 1u, BUILTIN_COMPONENT_FLOAT, 9u},
    {"abs", BUILTIN_ABS, 1u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"sign", BUILTIN_SIGN, 1u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"floor", BUILTIN_FLOOR, 1u, BUILTIN_COMPONENT_FLOAT, 8u},
    {"round", BUILTIN_ROUND, 1u, BUILTIN_COMPONENT_FLOAT, 2u},
    {"trunc", BUILTIN_TRUNC, 1u, BUILTIN_COMPONENT_FLOAT, 3u},
    {"fract", BUILTIN_FRACT, 1u, BUILTIN_COMPONENT_FLOAT, 10u},
    {"sin", BUILTIN_SIN, 1u, BUILTIN_COMPONENT_FLOAT, 13u},
    {"cos", BUILTIN_COS, 1u, BUILTIN_COMPONENT_FLOAT, 14u},
    {"tan", BUILTIN_TAN, 1u, BUILTIN_COMPONENT_FLOAT, 15u},
    {"asin", BUILTIN_ASIN, 1u, BUILTIN_COMPONENT_FLOAT, 16u},
    {"acos", BUILTIN_ACOS, 1u, BUILTIN_COMPONENT_FLOAT, 17u},
    {"atan", BUILTIN_ATAN, 1u, BUILTIN_COMPONENT_FLOAT, 18u},
    {"atan2", BUILTIN_ATAN2, 2u, BUILTIN_COMPONENT_FLOAT, 25u},
    {"pow", BUILTIN_POW, 2u, BUILTIN_COMPONENT_FLOAT, 26u},
    {"sinh", BUILTIN_SINH, 1u, BUILTIN_COMPONENT_FLOAT, 19u},
    {"cosh", BUILTIN_COSH, 1u, BUILTIN_COMPONENT_FLOAT, 20u},
    {"asinh", BUILTIN_ASINH, 1u, BUILTIN_COMPONENT_FLOAT, 22u},
    {"acosh", BUILTIN_ACOSH, 1u, BUILTIN_COMPONENT_FLOAT, 23u},
    {"atanh", BUILTIN_ATANH, 1u, BUILTIN_COMPONENT_FLOAT, 24u},
    {"log", BUILTIN_LOG, 1u, BUILTIN_COMPONENT_FLOAT, 28u},
    {"log2", BUILTIN_LOG2, 1u, BUILTIN_COMPONENT_FLOAT, 30u},
    {"exp2", BUILTIN_EXP2, 1u, BUILTIN_COMPONENT_FLOAT, 29u},
    {"inverseSqrt", BUILTIN_INVERSE_SQRT, 1u, BUILTIN_COMPONENT_FLOAT, 32u},
    {"degrees", BUILTIN_DEGREES, 1u, BUILTIN_COMPONENT_FLOAT, 12u},
    {"radians", BUILTIN_RADIANS, 1u, BUILTIN_COMPONENT_FLOAT, 11u},
    {"dot", BUILTIN_DOT, 2u, BUILTIN_NUMERIC_REDUCTION, 0u},
    {"length", BUILTIN_LENGTH, 1u, BUILTIN_FLOAT_REDUCTION, 66u},
    {"distance", BUILTIN_DISTANCE, 2u, BUILTIN_FLOAT_REDUCTION, 67u},
    {"normalize", BUILTIN_NORMALIZE, 1u, BUILTIN_FLOAT_VECTOR, 69u},
    {"cross", BUILTIN_CROSS, 2u, BUILTIN_FLOAT_CROSS, 68u},
    {"mix", BUILTIN_MIX, 3u, BUILTIN_FLOAT_MIX, 46u},
    {"reflect", BUILTIN_REFLECT, 2u, BUILTIN_FLOAT_VECTOR, 71u},
    {"faceForward", BUILTIN_FACE_FORWARD, 3u, BUILTIN_FLOAT_VECTOR, 70u},
    {"refract", BUILTIN_REFRACT, 3u, BUILTIN_FLOAT_REFRACT, 72u},
    {"subgroupAdd", BUILTIN_SUBGROUP_ADD, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u},
    {"subgroupMin", BUILTIN_SUBGROUP_MIN, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u},
    {"subgroupMax", BUILTIN_SUBGROUP_MAX, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u},
    {"subgroupMul", BUILTIN_SUBGROUP_MUL, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u},
    {"subgroupAnd", BUILTIN_SUBGROUP_AND, 1u, BUILTIN_SUBGROUP_INTEGER, 0u},
    {"subgroupOr", BUILTIN_SUBGROUP_OR, 1u, BUILTIN_SUBGROUP_INTEGER, 0u},
    {"subgroupXor", BUILTIN_SUBGROUP_XOR, 1u, BUILTIN_SUBGROUP_INTEGER, 0u},
    {"subgroupExclusiveAdd", BUILTIN_SUBGROUP_EXCLUSIVE_ADD, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u},
    {"subgroupInclusiveAdd", BUILTIN_SUBGROUP_INCLUSIVE_ADD, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u},
    {"subgroupExclusiveMul", BUILTIN_SUBGROUP_EXCLUSIVE_MUL, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u},
    {"subgroupInclusiveMul", BUILTIN_SUBGROUP_INCLUSIVE_MUL, 1u, BUILTIN_SUBGROUP_NUMERIC, 0u}
};

const NumericBuiltin *builtin_named(const Source *source, NameSpan name) {
    for (size_t i = 0u; i < sizeof(numeric_builtins) / sizeof(*numeric_builtins); ++i)
        if (source_span_is(source, name, numeric_builtins[i].name)) return &numeric_builtins[i];
    return NULL;
}

const NumericBuiltin *builtin_kind(NumericBuiltinKind kind) {
    for (size_t i = 0u; i < sizeof(numeric_builtins) / sizeof(*numeric_builtins); ++i)
        if (kind == numeric_builtins[i].kind) return &numeric_builtins[i];
    return NULL;
}

uint32_t builtin_extended_instruction(const NumericBuiltin *builtin, TypeId leaf) {
    if (builtin->kind == BUILTIN_FIRST_LEADING_BIT) return leaf == IR_I32 ? 74u : 75u;
    if (builtin->kind == BUILTIN_ABS) return (leaf == IR_F32 || leaf == IR_F16) ? 4u : leaf == IR_I32 ? 5u : 0u;
    if (builtin->kind == BUILTIN_SIGN) return (leaf == IR_F32 || leaf == IR_F16) ? 6u : leaf == IR_I32 ? 7u : 0u;
    return builtin->extended_instruction;
}

bool builtin_floating(const NumericBuiltin *builtin) {
    return builtin->family == BUILTIN_DERIVATIVE || builtin->family == BUILTIN_COMPONENT_FLOAT || builtin->family == BUILTIN_FLOAT_REDUCTION ||
        builtin->family == BUILTIN_FLOAT_VECTOR || builtin->family == BUILTIN_FLOAT_CROSS ||
        builtin->family == BUILTIN_FLOAT_REFRACT || builtin->family == BUILTIN_FLOAT_MIX;
}

bool builtin_subgroup(const NumericBuiltin *builtin) {
    return builtin != NULL && (builtin->family == BUILTIN_SUBGROUP_NUMERIC ||
        builtin->family == BUILTIN_SUBGROUP_INTEGER || builtin->family == BUILTIN_SUBGROUP_EXCHANGE);
}

bool builtin_subgroup_uniform(const NumericBuiltin *builtin) {
    if (builtin != NULL && builtin->family == BUILTIN_SUBGROUP_EXCHANGE)
        return builtin->kind == BUILTIN_SUBGROUP_ALL || builtin->kind == BUILTIN_SUBGROUP_ANY ||
            builtin->kind == BUILTIN_SUBGROUP_BALLOT || builtin->kind == BUILTIN_SUBGROUP_BROADCAST ||
            builtin->kind == BUILTIN_SUBGROUP_BROADCAST_FIRST;
    return builtin_subgroup(builtin) && builtin->kind != BUILTIN_SUBGROUP_EXCLUSIVE_ADD &&
        builtin->kind != BUILTIN_SUBGROUP_INCLUSIVE_ADD && builtin->kind != BUILTIN_SUBGROUP_EXCLUSIVE_MUL &&
        builtin->kind != BUILTIN_SUBGROUP_INCLUSIVE_MUL;
}

bool builtin_scalar_result(const NumericBuiltin *builtin) {
    return builtin->family == BUILTIN_NUMERIC_REDUCTION || builtin->family == BUILTIN_FLOAT_REDUCTION ||
        builtin->family == BUILTIN_BOOLEAN_REDUCTION;
}

bool builtin_shape(const NumericBuiltin *builtin, uint32_t width) {
    if (builtin->family == BUILTIN_PACKED_INTEGER) return width == (packed_pack(builtin->kind) ? 4u : 1u);
    if (builtin->family == BUILTIN_FLOAT_CROSS) return width == 3u;
    if (builtin->family == BUILTIN_NUMERIC_REDUCTION || builtin->family == BUILTIN_FLOAT_VECTOR ||
        builtin->family == BUILTIN_FLOAT_REFRACT)
        return width >= 2u && width <= 4u;
    return width >= 1u && width <= 4u;
}

uint32_t builtin_argument_width(const NumericBuiltin *builtin, uint32_t argument,
                                uint32_t vector_width, uint32_t actual_width) {
    if (argument == 2u && (builtin->family == BUILTIN_FLOAT_REFRACT ||
        (builtin->family == BUILTIN_FLOAT_MIX && actual_width == 1u))) return 1u;
    return vector_width;
}

/* Projection borrows an existing compact constant DAG. Only the completed
 * vector result allocates a node; no intermediate geometric vector is stored. */
static bool constant_lane(Source *source, ConstantValue input, uint32_t index, NumericValue *out) {
    ConstantValue value = input;
    if (input.type >= IR_PRIMITIVE_COUNT && !constant_project(source, input, index, &value)) return false;
    *out = (NumericValue){value.bits, (IrType)value.type};
    return true;
}

bool builtin_bounds(Source *source, uint32_t offset, NumericBuiltinKind kind,
                    const ConstantValue *lower, const ConstantValue *upper) {
    const uint32_t width = type_vector_width(source->compiler, lower->type);
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue low, high, comparison;
        if (!constant_lane(source, *lower, i, &low) || !constant_lane(source, *upper, i, &high) ||
            !numeric_binary(source, offset, EXPRESSION_COMPARE,
                kind == BUILTIN_SMOOTHSTEP ? COMPARE_EQUAL : COMPARE_GREATER,
                low, high, &comparison)) return false;
        if (comparison.bits != 0u)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
                kind == BUILTIN_SMOOTHSTEP ? "smoothstep constant edges are equal" :
                "clamp constant lower bound exceeds upper bound");
    }
    return true;
}

static bool component_constant(Source *source, uint32_t offset, const NumericBuiltin *builtin,
                               const ConstantValue *arguments, TypeId result_type, ConstantValue *out) {
    const uint32_t width = type_vector_width(source->compiler, result_type);
    ConstantValue lanes[4];
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue values[3], result;
        for (uint32_t j = 0u; j < builtin->arity; ++j)
            if (!constant_lane(source, arguments[j], i, &values[j])) return false;
        if (!numeric_builtin(source, offset, builtin, values, &result)) return false;
        lanes[i] = (ConstantValue){.type = (TypeId)result.type, .bits = result.bits};
    }
    if (width == 1u) {
        *out = lanes[0];
        return true;
    }
    return constant_composite(source, result_type, lanes, width, false, out);
}

static bool dot_constant(Source *source, uint32_t offset, const ConstantValue *arguments, NumericValue *out) {
    const TypeId leaf = type_leaf(source->compiler, arguments[0].type);
    const uint32_t width = type_vector_width(source->compiler, arguments[0].type);
    NumericDotAccumulator exact = {0};
    uint32_t modular = 0u;
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue left, right, product;
        if (!constant_lane(source, arguments[0], i, &left) ||
            !constant_lane(source, arguments[1], i, &right)) return false;
        if (leaf == IR_ABSTRACT_INT) {
            if (!numeric_dot_accumulate(source, offset, &exact, left, right)) return false;
        } else if (leaf == IR_I32 || leaf == IR_U32) {
            modular += (uint32_t)(left.bits * right.bits);
        } else {
            if (!numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, left, right, &product)) return false;
            if (i == 0u) *out = product;
            else if (!numeric_binary(source, offset, EXPRESSION_ADD, COMPARE_EQUAL, *out, product, out)) return false;
        }
    }
    if (leaf == IR_ABSTRACT_INT) return numeric_dot_finish(source, offset, &exact, out);
    if (leaf == IR_I32 || leaf == IR_U32) *out = (NumericValue){modular, (IrType)leaf};
    return true;
}

static bool length_constant(Source *source, uint32_t offset, const ConstantValue *arguments,
                            bool distance, NumericValue *out) {
    const uint32_t width = type_vector_width(source->compiler, arguments[0].type);
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue value, other, square;
        if (!constant_lane(source, arguments[0], i, &value)) return false;
        if (distance) {
            if (!constant_lane(source, arguments[1], i, &other) ||
                !numeric_binary(source, offset, EXPRESSION_SUBTRACT, COMPARE_EQUAL, value, other, &value)) return false;
        }
        if (!numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, value, value, &square)) return false;
        if (i == 0u) *out = square;
        else if (!numeric_binary(source, offset, EXPRESSION_ADD, COMPARE_EQUAL, *out, square, out)) return false;
    }
    return numeric_builtin(source, offset, builtin_kind(BUILTIN_SQRT), out, out);
}

static bool cross_lane(Source *source, uint32_t offset, const ConstantValue *arguments,
                       uint32_t index, NumericValue *out) {
    const uint32_t next = (index + 1u) % 3u, last = (index + 2u) % 3u;
    NumericValue left, right, product;
    if (!constant_lane(source, arguments[0], next, &left) ||
        !constant_lane(source, arguments[1], last, &right) ||
        !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, left, right, &product)) return false;
    if (!constant_lane(source, arguments[0], last, &left) ||
        !constant_lane(source, arguments[1], next, &right) ||
        !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, left, right, &left)) return false;
    return numeric_binary(source, offset, EXPRESSION_SUBTRACT, COMPARE_EQUAL, product, left, out);
}

/* Scalar refraction factors share declared-width arithmetic with every const
 * operator. Test k before sqrt: total internal reflection is a zero result,
 * not an invalid square root evaluated and then selected away. */
static bool refraction_scale(Source *source, uint32_t offset, const ConstantValue *arguments,
                             NumericValue *scale, bool *total_internal) {
    const ConstantValue pair[2] = {arguments[1], arguments[0]};
    NumericValue dot, eta, one, zero, square, eta_square, k, negative, root, product;
    if (!dot_constant(source, offset, pair, &dot) ||
        !constant_lane(source, arguments[2], 0u, &eta) ||
        !numeric_materialize(source, offset, (NumericValue){1u, IR_ABSTRACT_INT}, dot.type, &one) ||
        !numeric_materialize(source, offset, (NumericValue){0u, IR_ABSTRACT_INT}, dot.type, &zero) ||
        !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, dot, dot, &square) ||
        !numeric_binary(source, offset, EXPRESSION_SUBTRACT, COMPARE_EQUAL, one, square, &k) ||
        !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, eta, eta, &eta_square) ||
        !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, eta_square, k, &k) ||
        !numeric_binary(source, offset, EXPRESSION_SUBTRACT, COMPARE_EQUAL, one, k, &k) ||
        !numeric_binary(source, offset, EXPRESSION_COMPARE, COMPARE_LESS, k, zero, &negative)) return false;
    *total_internal = negative.bits != 0u;
    if (*total_internal) {
        *scale = zero;
        return true;
    }
    return numeric_builtin(source, offset, builtin_kind(BUILTIN_SQRT), &k, &root) &&
        numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, eta, dot, &product) &&
        numeric_binary(source, offset, EXPRESSION_ADD, COMPARE_EQUAL, product, root, scale);
}

static bool geometric_vector(Source *source, uint32_t offset, const NumericBuiltin *builtin,
                              const ConstantValue *arguments, TypeId result_type, ConstantValue *out) {
    const uint32_t width = type_vector_width(source->compiler, result_type);
    ConstantValue lanes[4];
    NumericValue length = {0};
    if (builtin->kind == BUILTIN_NORMALIZE && !length_constant(source, offset, arguments, false, &length)) return false;
    NumericValue reflection_scale = {0};
    if (builtin->kind == BUILTIN_REFLECT) {
        const ConstantValue dot_arguments[2] = {arguments[1], arguments[0]};
        NumericValue dot, two;
        const IrType leaf = (IrType)type_leaf(source->compiler, result_type);
        if (!dot_constant(source, offset, dot_arguments, &dot) ||
            !numeric_materialize(source, offset, (NumericValue){2u, IR_ABSTRACT_INT}, leaf, &two) ||
            !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, two, dot, &reflection_scale)) return false;
    }
    NumericValue refraction = {0};
    bool total_internal = false, forward_negative = false;
    if (builtin->kind == BUILTIN_REFRACT &&
        !refraction_scale(source, offset, arguments, &refraction, &total_internal)) return false;
    if (builtin->kind == BUILTIN_FACE_FORWARD) {
        NumericValue dot, zero, negative;
        if (!dot_constant(source, offset, arguments + 1, &dot) ||
            !numeric_materialize(source, offset, (NumericValue){0u, IR_ABSTRACT_INT}, dot.type, &zero) ||
            !numeric_binary(source, offset, EXPRESSION_COMPARE, COMPARE_LESS, dot, zero, &negative)) return false;
        forward_negative = negative.bits != 0u;
    }
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue result;
        if (builtin->kind == BUILTIN_CROSS) {
            if (!cross_lane(source, offset, arguments, i, &result)) return false;
        } else if (builtin->kind == BUILTIN_NORMALIZE) {
            if (!constant_lane(source, arguments[0], i, &result) ||
                !numeric_binary(source, offset, EXPRESSION_DIVIDE, COMPARE_EQUAL, result, length, &result)) return false;
        } else if (builtin->kind == BUILTIN_REFLECT) {
            NumericValue incident, normal, correction;
            if (!constant_lane(source, arguments[0], i, &incident) ||
                !constant_lane(source, arguments[1], i, &normal) ||
                !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, reflection_scale, normal, &correction) ||
                !numeric_binary(source, offset, EXPRESSION_SUBTRACT, COMPARE_EQUAL, incident, correction, &result)) return false;
        } else if (builtin->kind == BUILTIN_FACE_FORWARD) {
            if (!constant_lane(source, arguments[0], i, &result) ||
                (!forward_negative && !numeric_unary(source, offset, EXPRESSION_NEGATE, result, &result))) return false;
        } else if (builtin->kind == BUILTIN_REFRACT) {
            result = refraction;
            if (!total_internal) {
                NumericValue incident, normal, eta, correction;
                if (!constant_lane(source, arguments[0], i, &incident) ||
                    !constant_lane(source, arguments[1], i, &normal) ||
                    !constant_lane(source, arguments[2], 0u, &eta) ||
                    !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, eta, incident, &incident) ||
                    !numeric_binary(source, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, refraction, normal, &correction) ||
                    !numeric_binary(source, offset, EXPRESSION_SUBTRACT, COMPARE_EQUAL, incident, correction, &result)) return false;
            }
        } else return source_error(source, DOE_COMPILER_UNSUPPORTED, offset, "geometric builtin has no constant mapping");
        lanes[i] = (ConstantValue){.type = (TypeId)result.type, .bits = result.bits};
    }
    return constant_composite(source, result_type, lanes, width, false, out);
}

/* Boolean reduction projects the same typed constant DAG as numeric builtins;
 * scalar input is the identity. Runtime arguments never visit this evaluator. */
static bool boolean_constant(Source *source, const NumericBuiltin *builtin, ConstantValue value, ConstantValue *out) {
    bool result = builtin->kind == BUILTIN_ALL;
    const uint32_t width = type_vector_width(source->compiler, value.type);
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue lane;
        if (!constant_lane(source, value, i, &lane)) return false;
        result = builtin->kind == BUILTIN_ALL ? result && lane.bits != 0u : result || lane.bits != 0u;
    }
    *out = (ConstantValue){.type = IR_BOOL, .bits = result ? 1u : 0u};
    return true;
}
bool builtin_constant(Source *source, uint32_t offset, const NumericBuiltin *builtin,
                      const ConstantValue *arguments, TypeId result_type, ConstantValue *out) {
    if (builtin->family == BUILTIN_FLOAT_PACK)
        return float_pack_constant(source, offset, builtin->kind, arguments[0], result_type, out);
    if (builtin->family == BUILTIN_DECOMPOSE || builtin->family == BUILTIN_EXPONENT)
        return decompose_constant(source, offset, builtin->kind, arguments, result_type, out);
    if (builtin->family == BUILTIN_PACKED_INTEGER)
        return packed_constant(source, builtin->kind, arguments, result_type, out);
    if (builtin->family == BUILTIN_BOOLEAN_REDUCTION)
        return boolean_constant(source, builtin, arguments[0], out);
    if (builtin->family == BUILTIN_MATRIX)
        return matrix_constant(source, offset, builtin->kind, arguments[0], result_type, out);
    if (builtin->family == BUILTIN_COMPONENT_NUMERIC || builtin->family == BUILTIN_COMPONENT_FLOAT ||
        builtin->family == BUILTIN_COMPONENT_INTEGER || builtin->family == BUILTIN_FLOAT_MIX)
        return component_constant(source, offset, builtin, arguments, result_type, out);
    if (builtin->family == BUILTIN_FLOAT_VECTOR || builtin->family == BUILTIN_FLOAT_CROSS ||
        builtin->family == BUILTIN_FLOAT_REFRACT)
        return geometric_vector(source, offset, builtin, arguments, result_type, out);
    NumericValue result;
    const bool valid = builtin->kind == BUILTIN_DOT ?
        dot_constant(source, offset, arguments, &result) :
        length_constant(source, offset, arguments, builtin->kind == BUILTIN_DISTANCE, &result);
    if (!valid) return false;
    *out = (ConstantValue){.type = result_type, .bits = result.bits};
    return true;
}
