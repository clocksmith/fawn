/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"

typedef struct { uint32_t columns, rows; TypeId scalar; bool matrix; } MatrixShape;

static MatrixShape shape(const CompilerWorkspace *c, TypeId type, bool left) {
    if (type_matrix(c, type)) {
        const TypeRecord *m = &c->types[type - 7u];
        return (MatrixShape){m->count, type_vector_width(c, m->element), type_leaf(c, m->element), true};
    }
    const uint32_t width = type_vector_width(c, type);
    return (MatrixShape){left ? width : 1u, left ? 1u : width, type_leaf(c, type), false};
}
static bool materialize(Source *s, ExpressionValue *v, TypeId scalar) {
    const CompilerWorkspace *c = s->compiler;
    TypeId target = scalar;
    if (type_matrix(c, v->constant.type)) {
        const TypeRecord *m = &c->types[v->constant.type - 7u];
        if (!type_matrix_intern(s, scalar, m->count, type_vector_width(c, m->element), &target)) return false;
    } else if (type_vector_width(c, v->constant.type) != 1u &&
        !type_intern(s, TYPE_VECTOR, scalar, type_vector_width(c, v->constant.type), &target)) return false;
    return sema_materialize(s, v, target);
}
static bool component(Source *s, ConstantValue value, uint32_t column, uint32_t row, NumericValue *out) {
    if (type_matrix(s->compiler, value.type)) {
        if (!constant_project(s, value, column, &value) || !constant_project(s, value, row, &value)) return false;
    } else if (value.type >= 7u && !constant_project(s, value, column + row, &value)) return false;
    *out = (NumericValue){value.bits, (IrType)value.type};
    return true;
}
static bool element(Source *s, uint32_t offset, ExpressionKind op, const ExpressionValue *a,
    const ExpressionValue *b, MatrixShape left, MatrixShape right, uint32_t column,
    uint32_t row, NumericValue *out) {
    if (op != EXPRESSION_MULTIPLY) {
        NumericValue x, y;
        return component(s, a->constant, column, row, &x) &&
            component(s, b->constant, column, row, &y) &&
            numeric_binary(s, offset, op, COMPARE_EQUAL, x, y, out);
    }
    const bool scalar_left = left.columns == 1u && left.rows == 1u;
    const bool scalar_right = right.columns == 1u && right.rows == 1u;
    if (scalar_left || scalar_right) {
        NumericValue x, y;
        return component(s, a->constant, scalar_left ? 0u : column, scalar_left ? 0u : row, &x) &&
            component(s, b->constant, scalar_right ? 0u : column, scalar_right ? 0u : row, &y) &&
            numeric_binary(s, offset, op, COMPARE_EQUAL, x, y, out);
    }
    for (uint32_t k = 0u; k < left.columns; ++k) {
        NumericValue x, y, product;
        if (!component(s, a->constant, k, row, &x) || !component(s, b->constant, column, k, &y) ||
            !numeric_binary(s, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, x, y, &product)) return false;
        if (k == 0u) *out = product;
        else if (!numeric_binary(s, offset, EXPRESSION_ADD, COMPARE_EQUAL, *out, product, out)) return false;
    }
    return true;
}
static bool constant_product(Source *s, uint32_t offset, ExpressionKind op, const ExpressionValue *a,
    const ExpressionValue *b, MatrixShape left, MatrixShape right, MatrixShape result, ConstantValue *out) {
    const TypeId type = out->type;
    const uint32_t width = result.matrix ? result.rows : result.rows > 1u ? result.rows : result.columns;
    const uint32_t columns = result.matrix ? result.columns : 1u;
    uint32_t first;
    if (!constant_reserve(s, type, CONSTANT_COMPOSITE, result.matrix ? columns : width, out, &first)) return false;
    for (uint32_t col = 0u; col < columns; ++col) {
        uint32_t lane_first = first;
        if (result.matrix && !constant_reserve(s, s->compiler->types[type - 7u].element,
            CONSTANT_COMPOSITE, width, &s->compiler->constant_edges[first + col], &lane_first)) return false;
        for (uint32_t lane = 0u; lane < width; ++lane) {
            NumericValue value;
            const uint32_t column = result.matrix ? col : result.rows == 1u ? lane : 0u;
            const uint32_t row = result.matrix || result.rows != 1u ? lane : 0u;
            if (!element(s, offset, op, a, b, left, right, column, row, &value)) return false;
            s->compiler->constant_edges[lane_first + lane] = (ConstantValue){.type = result.scalar, .bits = value.bits};
        }
    }
    return true;
}
bool sema_matrix_binary(Source *s, ExpressionKind op, ExpressionValue *a, ExpressionValue *b,
    uint32_t offset, bool evaluate, bool emit, ExpressionValue *out) {
    const MatrixShape left = shape(s->compiler, a->constant.type, true);
    const MatrixShape right = shape(s->compiler, b->constant.type, false);
    MatrixShape result = left;
    if (op == EXPRESSION_ADD || op == EXPRESSION_SUBTRACT) {
        if (!left.matrix || !right.matrix || left.columns != right.columns || left.rows != right.rows) goto invalid;
    } else if (op == EXPRESSION_MULTIPLY) {
        if (a->constant.type < 7u) result = right;
        else if (b->constant.type >= 7u) {
            if (left.columns != right.rows) goto invalid;
            result = (MatrixShape){right.columns, left.rows, IR_VOID, left.matrix && right.matrix};
        }
    } else goto invalid;
    TypeId scalar;
    if (!sema_common_type(s, left.scalar, right.scalar, offset, &scalar)) return false;
    if (scalar != IR_F32 && scalar != IR_ABSTRACT_FLOAT) goto invalid;
    result.scalar = scalar;
    if (!materialize(s, a, scalar) || !materialize(s, b, scalar)) return false;
    TypeId type;
    if (result.matrix) {
        if (!type_matrix_intern(s, scalar, result.columns, result.rows, &type)) return false;
    } else if (!type_intern(s, TYPE_VECTOR, scalar, result.columns > 1u ? result.columns : result.rows, &type)) return false;
    *out = (ExpressionValue){.runtime = a->runtime || b->runtime, .overridable = a->overridable || b->overridable, .known = evaluate && a->known && b->known,
        .constant.type = type, .value = {.type = type, .source_offset = offset}};
    if (!out->runtime)
        return !out->known || constant_product(s, offset, op, a, b, left, right, result, &out->constant);
    out->known = false;
    if (!sema_ensure_value(s, a, emit) || !sema_ensure_value(s, b, emit)) return false;
    return sema_emit(s, emit, (IrInstruction){.opcode = op == EXPRESSION_ADD ? IR_ADD :
        op == EXPRESSION_SUBTRACT ? IR_SUBTRACT : IR_MULTIPLY, .type = type, .source_offset = offset,
        .operands.scalar = {a->value.id, b->value.id, a->constant.type, b->constant.type}}, &out->value);
invalid:
    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "operator has no matching matrix dimensions or floating-point overload");
}

bool sema_matrix_result(Source *s, NumericBuiltinKind kind, TypeId argument,
    uint32_t offset, TypeId *result) {
    if (!type_matrix(s->compiler, argument))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "matrix builtin requires a matrix argument");
    const TypeRecord *m = &s->compiler->types[argument - 7u];
    const uint32_t rows = type_vector_width(s->compiler, m->element);
    const TypeId scalar = type_leaf(s->compiler, m->element);
    if (kind == BUILTIN_TRANSPOSE) return type_matrix_intern(s, scalar, rows, m->count, result);
    if (kind == BUILTIN_DETERMINANT && rows == m->count) {
        *result = scalar;
        return true;
    }
    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "determinant requires a square matrix");
}

static bool transpose_constant(Source *s, ConstantValue value, TypeId type, ConstantValue *out) {
    const TypeRecord *m = &s->compiler->types[type - 7u];
    uint32_t first;
    if (!constant_reserve(s, type, CONSTANT_COMPOSITE, m->count, out, &first)) return false;
    for (uint32_t column = 0u; column < m->count; ++column) {
        const uint32_t rows = type_vector_width(s->compiler, m->element);
        uint32_t lanes;
        if (!constant_reserve(s, m->element, CONSTANT_COMPOSITE, rows,
            &s->compiler->constant_edges[first + column], &lanes)) return false;
        for (uint32_t row = 0u; row < rows; ++row) {
            NumericValue lane;
            if (!component(s, value, row, column, &lane)) return false;
            s->compiler->constant_edges[lanes + row] = (ConstantValue){.type = (TypeId)lane.type, .bits = lane.bits};
        }
    }
    return true;
}

static bool determinant_constant(Source *s, uint32_t offset, ConstantValue value, TypeId type,
    ConstantValue *out) {
    const uint32_t width = s->compiler->types[value.type - 7u].count;
    uint32_t permutations = 1u;
    for (uint32_t i = 0u; i < width; ++i) permutations *= width;
    NumericValue sum = {0};
    bool first = true;
    /* At most 4^4 candidates, with no recursive minor expansion or workspace
     * proportional to a source aggregate. Only distinct-row permutations count. */
    for (uint32_t candidate = 0u; candidate < permutations; ++candidate) {
        uint32_t rows[4], encoded = candidate, seen = 0u, inversions = 0u;
        bool unique = true;
        for (uint32_t column = 0u; column < width; ++column) {
            const uint32_t row = encoded % width;
            encoded /= width;
            if ((seen & (1u << row)) != 0u) {
                unique = false;
                break;
            }
            seen |= 1u << row;
            rows[column] = row;
            for (uint32_t previous = 0u; previous < column; ++previous)
                if (rows[previous] > row) inversions++;
        }
        if (!unique) continue;
        NumericValue product = {0};
        for (uint32_t column = 0u; column < width; ++column) {
            NumericValue lane;
            if (!component(s, value, column, rows[column], &lane)) return false;
            if (column == 0u) product = lane;
            else if (!numeric_binary(s, offset, EXPRESSION_MULTIPLY, COMPARE_EQUAL, product, lane, &product)) return false;
        }
        if ((inversions & 1u) != 0u && !numeric_unary(s, offset, EXPRESSION_NEGATE, product, &product)) return false;
        if (first) {
            sum = product;
            first = false;
        }
        else if (!numeric_binary(s, offset, EXPRESSION_ADD, COMPARE_EQUAL, sum, product, &sum)) return false;
    }
    *out = (ConstantValue){.type = type, .bits = sum.bits};
    return true;
}

bool matrix_constant(Source *s, uint32_t offset, NumericBuiltinKind kind, ConstantValue value,
    TypeId type, ConstantValue *out) {
    if (kind == BUILTIN_TRANSPOSE) return transpose_constant(s, value, type, out);
    if (kind == BUILTIN_DETERMINANT) return determinant_constant(s, offset, value, type, out);
    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "invalid matrix constant operation");
}
