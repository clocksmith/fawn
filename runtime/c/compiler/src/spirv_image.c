/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"

static TypeId vector_type(Emitter *e, TypeId scalar, uint32_t width) {
    if (width == 1u) return scalar;
    for (uint32_t i = 0u; i < e->program->type_count; ++i) {
        const TypeRecord *t = &e->program->types[i];
        if (t->kind == TYPE_VECTOR && t->element == scalar && t->count == width) return i + 7u;
    }
    (void)spirv_emit_error(e, 0u, "missing semantic image auxiliary type");
    return IR_VOID;
}
static uint32_t query_width(ImageType image) {
    const bool cube = image.dimension == DOE_COMPILER_VIEW_CUBE || image.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY;
    return (cube ? 2u : image_coordinate_width(image.dimension)) + (image_arrayed(image.dimension) ? 1u : 0u);
}
bool spirv_image_type(Emitter *e, const TypeRecord *t, uint32_t id) {
    if (t->kind == TYPE_SAMPLER) {
        EMIT(OP_TYPE_SAMPLER, id);
        return true;
    }
    const ImageType image = t->state.image;
    const uint32_t dimension = image.dimension == DOE_COMPILER_VIEW_1D ? 0u :
        image.dimension == DOE_COMPILER_VIEW_3D ? 2u :
        image.dimension == DOE_COMPILER_VIEW_CUBE || image.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY ? 3u : 1u;
    EMIT(OP_TYPE_IMAGE, id, spirv_scalar_type(t->element), dimension,
         (image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u ? 1u : 0u,
         image_arrayed(image.dimension) ? 1u : 0u,
         (image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u ? 1u : 0u,
         image.format != 0u ? 2u : 1u, image.format);
    return true;
}
bool spirv_image_plan(Emitter *e, const IrInstruction *ir) {
    const ImageType image = e->program->types[ir->operands.composite.reserved - 7u].state.image;
    if (ir->operands.composite.kind != IMAGE_SAMPLE_LEVEL || image_arrayed(image.dimension)) e->image_capabilities |= 1u;
    if (ir->operands.composite.kind == IMAGE_SAMPLE_LEVEL && spirv_sampled_type(e, ir->operands.composite.reserved) == 0u) return false;
    if (ir->type != IR_VOID && spirv_zero_constant(e, ir->type) == 0u) return false;
    (void)spirv_zero_constant(e, IR_I32);
    (void)spirv_unsigned_constant(e, 0u);
    (void)spirv_unsigned_constant(e, 1u);
    e->constants_used |= UINT32_C(1) << ID_TRUE;
    return !e->failed;
}
static bool component(Emitter *e, TypeId scalar, uint32_t vector, uint32_t width, uint32_t index, uint32_t *out) {
    if (width == 1u) { *out = vector; return true; }
    *out = spirv_reserve_ids(e, 1u);
    EMIT(OP_COMPOSITE_EXTRACT, spirv_scalar_type(scalar), *out, vector, index);
    return true;
}
static bool combine_valid(Emitter *e, uint32_t test, uint32_t *valid) {
    if (*valid == ID_TRUE) { *valid = test; return true; }
    const uint32_t result = spirv_reserve_ids(e, 1u);
    EMIT(OP_LOGICAL_AND, ID_BOOL, result, *valid, test);
    *valid = result;
    return true;
}
/* Signed nonnegativity is independent of the unsigned upper bound. No device
 * size assumption makes a negative index valid after unsigned conversion. */
static bool checked_index(Emitter *e, TypeId scalar, uint32_t value, uint32_t bound, uint32_t *index, uint32_t *valid) {
    *index = value;
    if (scalar == IR_I32) {
        const uint32_t positive = spirv_reserve_ids(e, 1u);
        EMIT(OP_SGREATER_EQUAL, ID_BOOL, positive, value, ID_ZERO_I32);
        if (!combine_valid(e, positive, valid)) return false;
        *index = spirv_reserve_ids(e, 1u);
        EMIT(OP_BITCAST, ID_U32, *index, value);
    }
    const uint32_t inside = spirv_reserve_ids(e, 1u);
    EMIT(OP_ULESS_THAN, ID_BOOL, inside, *index, bound);
    return combine_valid(e, inside, valid);
}
static bool dimensions(Emitter *e, ImageType image, uint32_t handle, uint32_t lod, uint32_t *out) {
    const uint32_t type = spirv_target_type(e, vector_type(e, IR_U32, query_width(image)));
    *out = spirv_reserve_ids(e, 1u);
    if (image.format != 0u || (image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u) {
        EMIT(OP_IMAGE_QUERY_SIZE, type, *out, handle);
    } else { EMIT(OP_IMAGE_QUERY_SIZE_LOD, type, *out, handle, lod); }
    return !e->failed;
}
static bool safe_level(Emitter *e, uint32_t handle, uint32_t operand, uint32_t *level, uint32_t *valid) {
    const uint32_t levels = spirv_reserve_ids(e, 1u);
    EMIT(OP_IMAGE_QUERY_LEVELS, ID_U32, levels, handle);
    uint32_t index;
    if (!checked_index(e, spirv_value_type(e->program, operand), e->values[operand], levels, &index, valid)) return false;
    *level = spirv_reserve_ids(e, 1u);
    EMIT(OP_SELECT, ID_U32, *level, *valid, index, ID_ZERO_U32);
    return true;
}
static bool zero_outside(Emitter *e, const IrInstruction *ir, uint32_t value, uint32_t valid) {
    const uint32_t result = e->values[ir->result], type = spirv_target_type(e, ir->type);
    if (valid == ID_TRUE) { EMIT(OP_COPY_OBJECT, type, result, value); return true; }
    const uint32_t before = e->current_label, active = spirv_reserve_ids(e, 1u), merge = spirv_reserve_ids(e, 1u);
    EMIT(OP_SELECTION_MERGE, merge, 0u);
    EMIT(OP_BRANCH_CONDITIONAL, valid, active, merge);
    EMIT(OP_LABEL, active);
    EMIT(OP_BRANCH, merge);
    EMIT(OP_LABEL, merge);
    EMIT(OP_PHI, type, result, value, active, spirv_zero_constant(e, ir->type), before);
    e->current_label = merge;
    return true;
}
static bool query(Emitter *e, const IrInstruction *ir, ImageType image, const uint32_t *operands) {
    const uint32_t handle = e->values[operands[0]], operation = ir->operands.composite.kind;
    const uint32_t result = e->values[ir->result];
    if (operation == IMAGE_LEVELS || operation == IMAGE_SAMPLES) {
        EMIT(operation == IMAGE_LEVELS ? OP_IMAGE_QUERY_LEVELS : OP_IMAGE_QUERY_SAMPLES, ID_U32, result, handle);
        return true;
    }
    uint32_t valid = ID_TRUE, level = ID_ZERO_U32, value;
    if (ir->operands.composite.count == 2u && !safe_level(e, handle, operands[1], &level, &valid)) return false;
    if (!dimensions(e, image, handle, level, &value)) return false;
    const uint32_t width = query_width(image);
    if (operation == IMAGE_LAYERS) {
        if (!component(e, IR_U32, value, width, width - 1u, &value)) return false;
    } else if (image_arrayed(image.dimension)) {
        const uint32_t projected = spirv_reserve_ids(e, 1u);
        EMIT(OP_VECTOR_SHUFFLE, spirv_target_type(e, ir->type), projected, value, value, 0u, 1u);
        value = projected;
    }
    return zero_outside(e, ir, value, valid);
}
static bool coordinates(Emitter *e, ImageType image, const uint32_t *operands, uint32_t size,
                        uint32_t *coordinate, uint32_t *valid) {
    const uint32_t width = image_coordinate_width(image.dimension), total = width + (image_arrayed(image.dimension) ? 1u : 0u);
    const TypeId input = spirv_value_type(e->program, operands[1]), scalar = type_leaf(e->program, input);
    uint32_t lanes[4];
    for (uint32_t i = 0u; i < total; ++i) {
        uint32_t bound, value;
        if (!component(e, IR_U32, size, total, i, &bound)) return false;
        TypeId leaf = scalar;
        if (i == width) {
            value = e->values[operands[2]];
            leaf = spirv_value_type(e->program, operands[2]);
        } else if (!component(e, scalar, e->values[operands[1]], width, i, &value)) return false;
        if (!checked_index(e, leaf, value, bound, &lanes[i], valid)) return false;
    }
    *coordinate = lanes[0];
    if (total != 1u) {
        *coordinate = spirv_reserve_ids(e, 1u);
        uint32_t *words;
        if (!spirv_instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)total + 2u, &words)) return false;
        words[0] = spirv_target_type(e, vector_type(e, IR_U32, total));
        words[1] = *coordinate;
        for (uint32_t i = 0u; i < total; ++i) words[i + 2u] = lanes[i];
    }
    return !e->failed;
}
static bool texel(Emitter *e, const IrInstruction *ir, const TypeRecord *t, const uint32_t *operands) {
    const ImageType image = t->state.image;
    const bool store = ir->operands.composite.kind == IMAGE_STORE;
    const bool storage = image.format != 0u, multisampled = (image.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u;
    const uint32_t handle = e->values[operands[0]], extra = 2u + (image_arrayed(image.dimension) ? 1u : 0u);
    uint32_t valid = ID_TRUE, level = ID_ZERO_U32, sample = ID_ZERO_U32, size, coordinate;
    if (!storage && !multisampled && !safe_level(e, handle, operands[extra], &level, &valid)) return false;
    if (multisampled) {
        const uint32_t samples = spirv_reserve_ids(e, 1u);
        EMIT(OP_IMAGE_QUERY_SAMPLES, ID_U32, samples, handle);
        if (!checked_index(e, spirv_value_type(e->program, operands[extra]), e->values[operands[extra]], samples, &sample, &valid)) return false;
    }
    if (!dimensions(e, image, handle, level, &size) || !coordinates(e, image, operands, size, &coordinate, &valid)) return false;
    const uint32_t before = e->current_label, active = spirv_reserve_ids(e, 1u), merge = spirv_reserve_ids(e, 1u);
    EMIT(OP_SELECTION_MERGE, merge, 0u);
    EMIT(OP_BRANCH_CONDITIONAL, valid, active, merge);
    EMIT(OP_LABEL, active);
    uint32_t loaded = 0u;
    if (store) { EMIT(OP_IMAGE_WRITE, handle, coordinate, e->values[operands[extra]]); }
    else {
        loaded = spirv_reserve_ids(e, 1u);
        const uint32_t vector = spirv_target_type(e, vector_type(e, t->element, 4u));
        if (storage) { EMIT(OP_IMAGE_READ, vector, loaded, handle, coordinate); }
        else { EMIT(OP_IMAGE_FETCH, vector, loaded, handle, coordinate, multisampled ? 64u : 2u, multisampled ? sample : level); }
        if ((image.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u) {
            const uint32_t depth = spirv_reserve_ids(e, 1u);
            EMIT(OP_COMPOSITE_EXTRACT, ID_F32, depth, loaded, 0u);
            loaded = depth;
        }
    }
    EMIT(OP_BRANCH, merge);
    EMIT(OP_LABEL, merge);
    if (!store) {
        EMIT(OP_PHI, spirv_target_type(e, ir->type), e->values[ir->result], loaded, active,
             spirv_zero_constant(e, ir->type), before);
    }
    e->current_label = merge;
    return true;
}
static bool sampled_coordinates(Emitter *e, ImageType image, const uint32_t *operands, uint32_t *coordinate) {
    *coordinate = e->values[operands[2]];
    if (!image_arrayed(image.dimension)) return true;
    uint32_t size, layers;
    if (!dimensions(e, image, e->values[operands[0]], ID_ZERO_U32, &size) ||
        !component(e, IR_U32, size, query_width(image), query_width(image) - 1u, &layers)) return false;
    const uint32_t maximum = spirv_reserve_ids(e, 1u), inside = spirv_reserve_ids(e, 1u), clamped = spirv_reserve_ids(e, 1u);
    EMIT(OP_ISUB, ID_U32, maximum, layers, ID_ONE_U32);
    uint32_t layer = e->values[operands[3]];
    if (spirv_value_type(e->program, operands[3]) == IR_I32) {
        const uint32_t positive = spirv_reserve_ids(e, 1u), nonnegative = spirv_reserve_ids(e, 1u), converted = spirv_reserve_ids(e, 1u);
        EMIT(OP_SGREATER_EQUAL, ID_BOOL, positive, layer, ID_ZERO_I32);
        EMIT(OP_SELECT, ID_I32, nonnegative, positive, layer, ID_ZERO_I32);
        EMIT(OP_BITCAST, ID_U32, converted, nonnegative);
        layer = converted;
    }
    EMIT(OP_ULESS_THAN, ID_BOOL, inside, layer, layers);
    EMIT(OP_SELECT, ID_U32, clamped, inside, layer, maximum);
    const uint32_t as_float = spirv_reserve_ids(e, 1u), width = image_coordinate_width(image.dimension);
    EMIT(OP_CONVERT_U_TO_F, ID_F32, as_float, clamped);
    const uint32_t combined = spirv_reserve_ids(e, 1u);
    EMIT(OP_COMPOSITE_CONSTRUCT, spirv_target_type(e, vector_type(e, IR_F32, width + 1u)), combined, *coordinate, as_float);
    *coordinate = combined;
    return true;
}
bool spirv_image_operation(Emitter *e, const IrInstruction *ir) {
    const TypeRecord *texture = &e->program->types[ir->operands.composite.reserved - 7u];
    const uint32_t *operands = e->program->typed_operands + ir->operands.composite.first;
    const ImageOperation op = (ImageOperation)ir->operands.composite.kind;
    if (op == IMAGE_LOAD || op == IMAGE_STORE) return texel(e, ir, texture, operands);
    if (op != IMAGE_SAMPLE_LEVEL) return query(e, ir, texture->state.image, operands);
    uint32_t coordinate;
    if (!sampled_coordinates(e, texture->state.image, operands, &coordinate)) return false;
    const uint32_t sampled = spirv_reserve_ids(e, 1u);
    EMIT(OP_SAMPLED_IMAGE, texture->aux, sampled, e->values[operands[0]], e->values[operands[1]]);
    const uint32_t level = 3u + (image_arrayed(texture->state.image.dimension) ? 1u : 0u);
    EMIT(OP_IMAGE_SAMPLE_EXPLICIT_LOD, spirv_target_type(e, ir->type), e->values[ir->result], sampled,
         coordinate, 2u, e->values[operands[level]]);
    return true;
}
#undef EMIT
