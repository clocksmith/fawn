/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"

typedef struct {
    const TypeRecord *type;
    ImageOperation operation;
    uint32_t texture, sampler, coordinates, layer, level, sample, data, reference, offset, gradient_x, gradient_y, bias;
    uint32_t width;
    bool storage, multisampled, arrayed, depth;
} HlslImage;
static bool decode(HlslEmitter *e, const IrInstruction *ir, HlslImage *image) {
    const CompilerWorkspace *c = e->module;
    const TypeId type = ir->operands.composite.reserved;
    const uint32_t first = ir->operands.composite.first, count = ir->operands.composite.count;
    if (type < 7u || type - 7u >= c->type_count || c->types[type - 7u].kind != TYPE_TEXTURE ||
        first > c->operand_count || count > c->operand_count - first || count == 0u) {
        hlsl_error(e, "HLSL image type or argument range is invalid");
        return false;
    }
    const uint32_t *arguments = c->typed_operands + first;
    *image = (HlslImage){.type = &c->types[type - 7u],
        .operation = (ImageOperation)ir->operands.composite.kind, .texture = arguments[0],
        .sampler = NO_ID, .coordinates = NO_ID, .layer = NO_ID,
        .level = NO_ID, .sample = NO_ID, .data = NO_ID, .reference = NO_ID, .offset = NO_ID, .gradient_x = NO_ID, .gradient_y = NO_ID, .bias = NO_ID};
    const ImageType t = image->type->state.image;
    image->storage = t.format != 0u;
    image->multisampled = (t.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u;
    image->depth = (t.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u;
    image->arrayed = image_arrayed(t.dimension);
    image->width = image_coordinate_width(t.dimension);
    if (hlsl_value_type(e, image->texture) != type) {
        hlsl_error(e, "HLSL image operand differs from its declared texture type");
        return false;
    }
    uint32_t expected = 1u;
    switch (image->operation) {
        case IMAGE_LOAD: expected = 2u + (image->arrayed ? 1u : 0u) + (image->storage ? 0u : 1u); break;
        case IMAGE_STORE: expected = 3u + (image->arrayed ? 1u : 0u); break;
        case IMAGE_DIMENSIONS: expected = !image->storage && !image->multisampled && count == 2u ? 2u : 1u; break;
        case IMAGE_LEVELS: case IMAGE_LAYERS: case IMAGE_SAMPLES: break;
        case IMAGE_SAMPLE_GRAD: case IMAGE_SAMPLE_BIAS:
        case IMAGE_SAMPLE: case IMAGE_SAMPLE_COMPARE: case IMAGE_SAMPLE_LEVEL: case IMAGE_SAMPLE_COMPARE_LEVEL:
            expected = image_sample_arity(t, image->operation);
            if (image_offset_width(t, image->operation) != 0u && count == expected + 1u) ++expected;
            break;
        default: hlsl_error(e, "HLSL image operation has no lowering"); return false;
    }
    if (count != expected) {
        hlsl_error(e, "HLSL image argument count does not match its typed operation");
        return false;
    }
    if (image->operation == IMAGE_LOAD || image->operation == IMAGE_STORE) {
        image->coordinates = arguments[1];
        if (image->arrayed) image->layer = arguments[2];
        const uint32_t last = arguments[count - 1u];
        if (image->operation == IMAGE_STORE) image->data = last;
        else if (image->multisampled) image->sample = last;
        else if (!image->storage) image->level = last;
    } else if (image->operation == IMAGE_DIMENSIONS && count == 2u) image->level = arguments[1];
    else if (image_sampling(image->operation)) {
        image->sampler = arguments[1];
        image->coordinates = arguments[2];
        if (image->arrayed) image->layer = arguments[3];
        const uint32_t last = 3u + (image->arrayed ? 1u : 0u);
        if (image->operation == IMAGE_SAMPLE_GRAD) {
            image->gradient_x = arguments[last];
            image->gradient_y = arguments[last + 1u];
        } else if (image->operation == IMAGE_SAMPLE_BIAS) image->bias = arguments[last];
        else if (image_comparison(image->operation)) image->reference = arguments[last];
        else if (!image_implicit(image->operation)) image->level = arguments[last];
        const uint32_t arity = image_sample_arity(t, image->operation);
        if (count == arity + 1u) image->offset = arguments[arity];
    }
    if ((image->operation == IMAGE_LOAD || image->operation == IMAGE_STORE) &&
        (t.dimension == DOE_COMPILER_VIEW_CUBE || t.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY)) {
        hlsl_error(e, "HLSL cube images have no integer texel load/store lowering");
        return false;
    }
    if ((image->operation == IMAGE_LEVELS && (image->storage || image->multisampled)) ||
        (image->operation == IMAGE_LAYERS && !image->arrayed) ||
        (image->operation == IMAGE_SAMPLES && !image->multisampled)) {
        hlsl_error(e, "HLSL image query is not defined for this texture type");
        return false;
    }
    if ((image->operation == IMAGE_STORE && (!image->storage || t.access == DOE_COMPILER_READ)) ||
        (image->operation == IMAGE_LOAD && image->storage && t.access == DOE_COMPILER_WRITE) ||
        ((image_sampling(image->operation)) &&
         (image->storage || image->multisampled || image->type->element != IR_F32 ||
          (image_comparison(image->operation) && !image->depth) ||
          ((image->operation == IMAGE_SAMPLE_GRAD || image->operation == IMAGE_SAMPLE_BIAS) &&
           (image->depth || t.dimension == DOE_COMPILER_VIEW_1D))))) {
        hlsl_error(e, "HLSL image operation violates its texture access or sampling type");
        return false;
    }
    return !e->failed;
}
static uint32_t dimension_width(const HlslImage *image) {
    const uint32_t dimension = image->type->state.image.dimension;
    return dimension == DOE_COMPILER_VIEW_CUBE || dimension == DOE_COMPILER_VIEW_CUBE_ARRAY ? 2u : image->width;
}
static void dimensions(HlslEmitter *e, const HlslImage *image, bool selected_level) {
    hlsl_indent(e);
    hlsl_value(e, image->texture);
    hlsl_text(e, ".GetDimensions(");
    if (!hlsl_image_uav(image->type) && !image->multisampled) {
        if (selected_level) {
            hlsl_text(e, "uint(");
            hlsl_value(e, image->level);
            hlsl_text(e, ")");
        } else hlsl_text(e, "0u");
        hlsl_text(e, ", ");
    }
    hlsl_text(e, "image_x");
    if (dimension_width(image) >= 2u) hlsl_text(e, ", image_y");
    if (dimension_width(image) == 3u) hlsl_text(e, ", image_z");
    if (image->arrayed) hlsl_text(e, ", image_layers");
    if (image->multisampled) hlsl_text(e, ", image_samples");
    else if (!hlsl_image_uav(image->type)) hlsl_text(e, ", image_levels");
    hlsl_text(e, ");\n");
}
static void indexed_value(HlslEmitter *e, uint32_t value, uint32_t lane) {
    hlsl_value(e, value);
    if (type_vector_width(e->module, hlsl_value_type(e, value)) != 1u)
        hlsl_format(e, "[%u]", lane);
}
static void bounded_index(HlslEmitter *e, uint32_t value, uint32_t lane, const char *limit) {
    const TypeId leaf = type_leaf(e->module, hlsl_value_type(e, value));
    if (leaf == IR_I32) {
        indexed_value(e, value, lane);
        hlsl_text(e, " >= 0 && ");
    } else if (leaf != IR_U32) {
        hlsl_error(e, "HLSL image index is not an integer value");
        return;
    }
    hlsl_text(e, "uint(");
    indexed_value(e, value, lane);
    hlsl_format(e, ") < %s", limit);
}
static void texel_coordinates(HlslEmitter *e, const HlslImage *image, bool sampled) {
    const uint32_t width = image->width + (image->arrayed ? 1u : 0u) +
        (sampled && !image->multisampled ? 1u : 0u);
    hlsl_text(e, sampled ? "int" : "uint");
    if (width != 1u) hlsl_format(e, "%u", width);
    hlsl_text(e, "(");
    hlsl_value(e, image->coordinates);
    if (image->arrayed) {
        hlsl_text(e, ", ");
        hlsl_value(e, image->layer);
    }
    if (sampled && !image->multisampled) {
        hlsl_text(e, ", ");
        if (image->storage) hlsl_text(e, "0u");
        else hlsl_value(e, image->level);
    }
    hlsl_text(e, ")");
}
static void texel(HlslEmitter *e, const IrInstruction *ir, const HlslImage *image) {
    static const char *const dimensions_names[] = {"image_x", "image_y", "image_z"};
    hlsl_indent(e);
    hlsl_text(e, "if (");
    for (uint32_t lane = 0u; lane < image->width; ++lane) {
        if (lane != 0u) hlsl_text(e, " && ");
        bounded_index(e, image->coordinates, lane, dimensions_names[lane]);
    }
    if (image->arrayed) {
        hlsl_text(e, " && ");
        bounded_index(e, image->layer, 0u, "image_layers");
    }
    if (image->multisampled) {
        hlsl_text(e, " && ");
        bounded_index(e, image->sample, 0u, "image_samples");
    }
    hlsl_text(e, ") {\n");
    ++e->indent;
    const uint32_t components = hlsl_image_components(e, image->type);
    if (image->operation == IMAGE_LOAD) {
        hlsl_indent(e);
        hlsl_type(e, image->type->element);
        if (components != 1u) hlsl_format(e, "%u", components);
        hlsl_text(e, " image_texel = ");
    } else hlsl_indent(e);
    hlsl_value(e, image->texture);
    if (hlsl_image_uav(image->type)) {
        hlsl_text(e, "[");
        texel_coordinates(e, image, false);
        hlsl_text(e, "]");
    } else {
        hlsl_text(e, ".Load(");
        texel_coordinates(e, image, true);
        if (image->multisampled) {
            hlsl_text(e, ", int(");
            hlsl_value(e, image->sample);
            hlsl_text(e, ")");
        }
        hlsl_text(e, ")");
    }
    if (image->operation == IMAGE_STORE) {
        hlsl_text(e, " = ");
        hlsl_value(e, image->data);
        if (components == 1u) hlsl_text(e, ".x");
        if (components == 2u) hlsl_text(e, ".xy");
        hlsl_text(e, ";\n");
    } else {
        hlsl_text(e, ";\n");
        hlsl_indent(e);
        hlsl_value(e, ir->result);
        hlsl_text(e, " = ");
        if (!image->depth && components < 4u) {
            hlsl_type(e, image->type->element);
            hlsl_text(e, "4(image_texel");
            for (uint32_t i = components; i < 4u; ++i) {
                hlsl_text(e, i == 3u ? ", 1" : ", 0");
            }
            hlsl_text(e, ")");
        } else hlsl_text(e, "image_texel");
        hlsl_text(e, ";\n");
    }
    --e->indent;
    hlsl_line(e, "}");
}
static void sample_level(HlslEmitter *e, const IrInstruction *ir, const HlslImage *image) {
    const TypeId sampler_type = hlsl_value_type(e, image->sampler);
    if (sampler_type < 7u || sampler_type - 7u >= e->module->type_count ||
        e->module->types[sampler_type - 7u].kind != TYPE_SAMPLER ||
        e->module->types[sampler_type - 7u].state.image.flags !=
            (image->reference != NO_ID ? DOE_COMPILER_HANDLE_COMPARISON : 0u)) {
        hlsl_error(e, "HLSL sampling sampler kind differs from the typed operation");
        return;
    }
    const TypeId coordinates = hlsl_value_type(e, image->coordinates);
    const TypeId level = image->level == NO_ID ? IR_VOID : hlsl_value_type(e, image->level);
    if (type_leaf(e->module, coordinates) != IR_F32 || type_vector_width(e->module, coordinates) != image->width ||
        (image->reference != NO_ID && hlsl_value_type(e, image->reference) != IR_F32) ||
        (image->bias != NO_ID && hlsl_value_type(e, image->bias) != IR_F32) ||
        (image->gradient_x != NO_ID && (hlsl_value_type(e, image->gradient_x) != coordinates ||
         hlsl_value_type(e, image->gradient_y) != coordinates)) ||
        (image->level != NO_ID && (image->depth ? level != IR_I32 && level != IR_U32 : level != IR_F32))) {
        hlsl_error(e, "HLSL sampling operand types differ from the typed image operation");
        return;
    }
    if (image->arrayed) {
        hlsl_line(e, "if (image_layers != 0u) {");
        ++e->indent;
        hlsl_indent(e);
        hlsl_text(e, "uint image_layer = ");
        if (type_leaf(e->module, hlsl_value_type(e, image->layer)) == IR_I32) {
            hlsl_value(e, image->layer);
            hlsl_text(e, " < 0 ? 0u : ");
        }
        hlsl_text(e, "min(uint(");
        hlsl_value(e, image->layer);
        hlsl_text(e, "), image_layers - 1u);\n");
    }
    hlsl_indent(e);
    hlsl_value(e, ir->result);
    hlsl_text(e, " = ");
    hlsl_value(e, image->texture);
    hlsl_text(e, image->operation == IMAGE_SAMPLE_GRAD ? ".SampleGrad(" :
        image->operation == IMAGE_SAMPLE_BIAS ? ".SampleBias(" : image_implicit(image->operation) ?
        (image->reference != NO_ID ? ".SampleCmp(" : ".Sample(") :
        (image->reference != NO_ID ? ".SampleCmpLevelZero(" : ".SampleLevel("));
    hlsl_value(e, image->sampler);
    hlsl_text(e, ", ");
    if (image->arrayed) hlsl_format(e, "float%u(", image->width + 1u);
    hlsl_value(e, image->coordinates);
    if (image->arrayed) hlsl_text(e, ", float(image_layer))");
    if (image->gradient_x != NO_ID) {
        hlsl_text(e, ", ");
        hlsl_value(e, image->gradient_x);
        hlsl_text(e, ", ");
        hlsl_value(e, image->gradient_y);
    } else if (image->bias != NO_ID) {
        hlsl_text(e, ", clamp(");
        hlsl_value(e, image->bias);
        hlsl_text(e, ", -16.0f, 15.99f)");
    } else if (image->reference != NO_ID) {
        hlsl_text(e, ", ");
        hlsl_value(e, image->reference);
    } else if (image->level != NO_ID) {
        hlsl_text(e, ", float(");
        hlsl_value(e, image->level);
        hlsl_text(e, ")");
    }
    if (image->offset != NO_ID) {
        if (image->offset == 0u || image->offset > e->module->instruction_count ||
            type_leaf(e->module, hlsl_value_type(e, image->offset)) != IR_I32 ||
            type_vector_width(e->module, hlsl_value_type(e, image->offset)) !=
                image_offset_width(image->type->state.image, image->operation)) {
            hlsl_error(e, "texture offset has no matching signed constant identity");
            return;
        }
        const IrInstruction *offset = &e->module->instructions[image->offset - 1u];
        if (offset->opcode != IR_CONSTANT || offset->operands.scalar.left != 1u) {
            hlsl_error(e, "texture offset has no constant IR identity");
            return;
        }
        hlsl_text(e, ", ");
        hlsl_constant(e, offset->type, offset->operands.scalar.binding, true);
    }
    hlsl_text(e, ");\n");
    if (image->arrayed) {
        --e->indent;
        hlsl_line(e, "}");
    }
}
static void query_result(HlslEmitter *e, const IrInstruction *ir, const HlslImage *image) {
    hlsl_indent(e);
    hlsl_value(e, ir->result);
    hlsl_text(e, " = ");
    switch (image->operation) {
        case IMAGE_DIMENSIONS: {
            const uint32_t width = dimension_width(image);
            if (width != 1u) hlsl_format(e, "uint%u(", width);
            hlsl_text(e, "image_x");
            if (width >= 2u) hlsl_text(e, ", image_y");
            if (width == 3u) hlsl_text(e, ", image_z");
            if (width != 1u) hlsl_text(e, ")");
            break;
        }
        case IMAGE_LEVELS: hlsl_text(e, "image_levels"); break;
        case IMAGE_LAYERS: hlsl_text(e, "image_layers"); break;
        case IMAGE_SAMPLES: hlsl_text(e, "image_samples"); break;
        case IMAGE_LOAD: case IMAGE_STORE: case IMAGE_SAMPLE_GRAD: case IMAGE_SAMPLE_BIAS:
        case IMAGE_SAMPLE: case IMAGE_SAMPLE_COMPARE: case IMAGE_SAMPLE_LEVEL: case IMAGE_SAMPLE_COMPARE_LEVEL:
        default: hlsl_error(e, "HLSL image query has no lowering"); break;
    }
    hlsl_text(e, ";\n");
}
void hlsl_image(HlslEmitter *e, const IrInstruction *ir) {
    HlslImage image;
    if (!decode(e, ir, &image)) return;
    hlsl_line(e, "{");
    ++e->indent;
    if (ir->result != 0u) {
        hlsl_indent(e);
        hlsl_value(e, ir->result);
        hlsl_text(e, " = ");
        hlsl_zero(e, ir->type);
        hlsl_text(e, ";\n");
    }
    hlsl_line(e, "uint image_x = 0u;");
    hlsl_line(e, "uint image_y = 0u;");
    hlsl_line(e, "uint image_z = 0u;");
    hlsl_line(e, "uint image_layers = 0u;");
    hlsl_line(e, "uint image_levels = 0u;");
    hlsl_line(e, "uint image_samples = 0u;");
    dimensions(e, &image, false);
    const bool checked_level = image.level != NO_ID && image.operation != IMAGE_SAMPLE_LEVEL;
    if (checked_level) {
        hlsl_indent(e);
        hlsl_text(e, "if (");
        bounded_index(e, image.level, 0u, "image_levels");
        hlsl_text(e, ") {\n");
        ++e->indent;
        dimensions(e, &image, true);
    }
    switch (image.operation) {
        case IMAGE_LOAD: case IMAGE_STORE: texel(e, ir, &image); break;
        case IMAGE_SAMPLE_GRAD: case IMAGE_SAMPLE_BIAS:
        case IMAGE_SAMPLE: case IMAGE_SAMPLE_COMPARE: case IMAGE_SAMPLE_LEVEL: case IMAGE_SAMPLE_COMPARE_LEVEL: sample_level(e, ir, &image); break;
        case IMAGE_DIMENSIONS: case IMAGE_LEVELS: case IMAGE_LAYERS: case IMAGE_SAMPLES:
            query_result(e, ir, &image);
            break;
        default: hlsl_error(e, "HLSL image operation has no lowering"); break;
    }
    if (checked_level) {
        --e->indent;
        hlsl_line(e, "}");
    }
    --e->indent;
    hlsl_line(e, "}");
}
