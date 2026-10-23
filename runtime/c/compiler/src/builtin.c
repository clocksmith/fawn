/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

static const NumericBuiltin numeric_builtins[] = {
    {"min", BUILTIN_MIN, 2u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"max", BUILTIN_MAX, 2u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"clamp", BUILTIN_CLAMP, 3u, BUILTIN_COMPONENT_NUMERIC, 0u},
    {"exp", BUILTIN_EXP, 1u, BUILTIN_COMPONENT_FLOAT, 27u},
    {"tanh", BUILTIN_TANH, 1u, BUILTIN_COMPONENT_FLOAT, 21u},
    {"sqrt", BUILTIN_SQRT, 1u, BUILTIN_COMPONENT_FLOAT, 31u},
    {"dot", BUILTIN_DOT, 2u, BUILTIN_NUMERIC_REDUCTION, 0u},
    {"length", BUILTIN_LENGTH, 1u, BUILTIN_FLOAT_REDUCTION, 66u},
    {"distance", BUILTIN_DISTANCE, 2u, BUILTIN_FLOAT_REDUCTION, 67u},
    {"normalize", BUILTIN_NORMALIZE, 1u, BUILTIN_FLOAT_VECTOR, 69u},
    {"cross", BUILTIN_CROSS, 2u, BUILTIN_FLOAT_CROSS, 68u}
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

bool builtin_floating(const NumericBuiltin *builtin) {
    return builtin->family != BUILTIN_COMPONENT_NUMERIC && builtin->family != BUILTIN_NUMERIC_REDUCTION;
}

bool builtin_scalar_result(const NumericBuiltin *builtin) {
    return builtin->family == BUILTIN_NUMERIC_REDUCTION || builtin->family == BUILTIN_FLOAT_REDUCTION;
}

bool builtin_shape(const NumericBuiltin *builtin, uint32_t width) {
    if (builtin->family == BUILTIN_FLOAT_CROSS) return width == 3u;
    if (builtin->family == BUILTIN_NUMERIC_REDUCTION || builtin->family == BUILTIN_FLOAT_VECTOR)
        return width >= 2u && width <= 4u;
    return width >= 1u && width <= 4u;
}

/* Projection borrows an existing compact constant DAG. Only the completed
 * vector result allocates a node; no intermediate geometric vector is stored. */
static bool constant_lane(Source *source, ConstantValue input, uint32_t index, NumericValue *out) {
    ConstantValue value = input;
    if (input.type >= 7u && !constant_project(source, input, index, &value)) return false;
    *out = (NumericValue){value.bits, (IrType)value.type};
    return true;
}

bool builtin_clamp_bounds(Source *source, uint32_t offset, const ConstantValue *lower, const ConstantValue *upper) {
    const uint32_t width = type_vector_width(source->compiler, lower->type);
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue low, high, comparison;
        if (!constant_lane(source, *lower, i, &low) || !constant_lane(source, *upper, i, &high) ||
            !numeric_binary(source, offset, EXPRESSION_COMPARE, COMPARE_GREATER, low, high, &comparison)) return false;
        if (comparison.bits != 0u)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, offset,
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

static bool geometric_vector(Source *source, uint32_t offset, const NumericBuiltin *builtin,
                              const ConstantValue *arguments, TypeId result_type, ConstantValue *out) {
    const uint32_t width = type_vector_width(source->compiler, result_type);
    ConstantValue lanes[4];
    NumericValue length = {0};
    if (builtin->kind == BUILTIN_NORMALIZE && !length_constant(source, offset, arguments, false, &length)) return false;
    for (uint32_t i = 0u; i < width; ++i) {
        NumericValue result;
        if (builtin->kind == BUILTIN_CROSS) {
            if (!cross_lane(source, offset, arguments, i, &result)) return false;
        } else {
            if (!constant_lane(source, arguments[0], i, &result) ||
                !numeric_binary(source, offset, EXPRESSION_DIVIDE, COMPARE_EQUAL, result, length, &result)) return false;
        }
        lanes[i] = (ConstantValue){.type = (TypeId)result.type, .bits = result.bits};
    }
    return constant_composite(source, result_type, lanes, width, false, out);
}

bool builtin_constant(Source *source, uint32_t offset, const NumericBuiltin *builtin,
                      const ConstantValue *arguments, TypeId result_type, ConstantValue *out) {
    if (builtin->family == BUILTIN_COMPONENT_NUMERIC || builtin->family == BUILTIN_COMPONENT_FLOAT)
        return component_constant(source, offset, builtin, arguments, result_type, out);
    if (builtin->family == BUILTIN_FLOAT_VECTOR || builtin->family == BUILTIN_FLOAT_CROSS)
        return geometric_vector(source, offset, builtin, arguments, result_type, out);
    NumericValue result;
    const bool valid = builtin->kind == BUILTIN_DOT ?
        dot_constant(source, offset, arguments, &result) :
        length_constant(source, offset, arguments, builtin->kind == BUILTIN_DISTANCE, &result);
    if (!valid) return false;
    *out = (ConstantValue){.type = result_type, .bits = result.bits};
    return true;
}
