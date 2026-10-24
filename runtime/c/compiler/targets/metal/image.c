/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "private.h"

typedef struct {
    const TypeRecord *type;
    ImageOperation operation;
    uint32_t texture, coordinates, layer, level, sample, sampler, data, reference, offset, width;
    bool storage, multisampled, arrayed, cube, one_dimensional, depth;
} MetalImage;

static bool integer_operand(MetalEmitter *e, uint32_t value, uint32_t width) {
    const TypeId type = metal_value_type(e, value);
    const TypeId leaf = type_leaf(e->module, type);
    if ((leaf == IR_I32 || leaf == IR_U32) && type_vector_width(e->module, type) == width) return true;
    metal_error(e, "Metal image integer operand has an invalid type or width");
    return false;
}
static bool decode(MetalEmitter *e, const IrInstruction *ir, MetalImage *image) {
    const CompilerWorkspace *c = e->module;
    const TypeId type = ir->operands.composite.reserved;
    const uint32_t first = ir->operands.composite.first, count = ir->operands.composite.count;
    if (type < 7u || type - 7u >= c->type_count || c->types[type - 7u].kind != TYPE_TEXTURE ||
        count == 0u || first > c->operand_count || count > c->operand_count - first) {
        metal_error(e, "Metal image type or operand range is invalid");
        return false;
    }
    const uint32_t *args = c->typed_operands + first;
    const TypeRecord *t = &c->types[type - 7u];
    const ImageType shape = t->state.image;
    *image = (MetalImage){.type = t, .operation = (ImageOperation)ir->operands.composite.kind,
        .texture = args[0], .coordinates = NO_ID, .layer = NO_ID, .level = NO_ID,
        .sample = NO_ID, .sampler = NO_ID, .data = NO_ID, .reference = NO_ID, .offset = NO_ID,
        .width = image_coordinate_width(shape.dimension), .storage = shape.format != 0u,
        .depth = (shape.flags & DOE_COMPILER_HANDLE_DEPTH) != 0u,
        .multisampled = (shape.flags & DOE_COMPILER_HANDLE_MULTISAMPLED) != 0u,
        .arrayed = image_arrayed(shape.dimension), .one_dimensional = shape.dimension == DOE_COMPILER_VIEW_1D,
        .cube = shape.dimension == DOE_COMPILER_VIEW_CUBE || shape.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY};
    if (metal_value_type(e, args[0]) != type) {
        metal_error(e, "Metal image texture identity differs from its declared type");
        return false;
    }
    uint32_t expected = 1u;
    switch (image->operation) {
        case IMAGE_LOAD: expected = 2u + (image->arrayed ? 1u : 0u) + (image->storage ? 0u : 1u); break;
        case IMAGE_STORE: expected = 3u + (image->arrayed ? 1u : 0u); break;
        case IMAGE_DIMENSIONS: expected = !image->storage && !image->multisampled && count == 2u ? 2u : 1u; break;
        case IMAGE_LEVELS: case IMAGE_LAYERS: case IMAGE_SAMPLES: break;
        case IMAGE_SAMPLE_LEVEL: case IMAGE_SAMPLE_COMPARE_LEVEL:
            expected = 4u + (image->arrayed ? 1u : 0u);
            if (image->depth && image->width == 2u && count == expected + 1u) ++expected;
            break;
        default: metal_error(e, "Metal image operation has no implemented lowering"); return false;
    }
    if (count != expected) {
        metal_error(e, "Metal image operand count differs from its typed operation");
        return false;
    }
    const bool texel = image->operation == IMAGE_LOAD || image->operation == IMAGE_STORE;
    const TypeId result_scalar = image->operation == IMAGE_STORE ? IR_VOID :
        image->operation == IMAGE_LOAD || image->operation == IMAGE_SAMPLE_LEVEL ||
        image->operation == IMAGE_SAMPLE_COMPARE_LEVEL ? t->element : IR_U32;
    const uint32_t result_width = image->operation == IMAGE_DIMENSIONS ? (image->cube ? 2u : image->width) :
        (image->operation == IMAGE_LOAD || image->operation == IMAGE_SAMPLE_LEVEL) && !image->depth ? 4u : 1u;
    if (type_leaf(c, ir->type) != result_scalar || type_vector_width(c, ir->type) != result_width ||
        (image->operation == IMAGE_STORE ? ir->result != 0u : ir->result == 0u)) {
        metal_error(e, "Metal image result differs from its typed operation");
        return false;
    }
    if ((texel && image->cube) ||
        (image->operation == IMAGE_STORE && (!image->storage || shape.access == DOE_COMPILER_READ)) ||
        (image->operation == IMAGE_LOAD && shape.access == DOE_COMPILER_WRITE) ||
        (image->operation == IMAGE_LEVELS && (image->storage || image->multisampled)) ||
        (image->operation == IMAGE_LAYERS && !image->arrayed) ||
        (image->operation == IMAGE_SAMPLES && !image->multisampled) ||
        ((image->operation == IMAGE_SAMPLE_LEVEL || image->operation == IMAGE_SAMPLE_COMPARE_LEVEL) &&
         (image->storage || image->multisampled || t->element != IR_F32 ||
          (image->operation == IMAGE_SAMPLE_COMPARE_LEVEL && !image->depth)))) {
        metal_error(e, "Metal image operation is invalid for its texture access or shape");
        return false;
    }
    if (texel) {
        image->coordinates = args[1];
        if (!integer_operand(e, image->coordinates, image->width)) return false;
        if (image->arrayed) image->layer = args[2];
        if (image->operation == IMAGE_STORE) image->data = args[count - 1u];
        else if (image->multisampled) image->sample = args[count - 1u];
        else if (!image->storage) image->level = args[count - 1u];
    } else if (image->operation == IMAGE_DIMENSIONS && count == 2u) image->level = args[1];
    else if (image->operation == IMAGE_SAMPLE_LEVEL || image->operation == IMAGE_SAMPLE_COMPARE_LEVEL) {
        image->sampler = args[1];
        image->coordinates = args[2];
        if (image->arrayed) image->layer = args[3];
        const uint32_t last = 3u + (image->arrayed ? 1u : 0u);
        if (image->operation == IMAGE_SAMPLE_COMPARE_LEVEL) image->reference = args[last];
        else image->level = args[last];
        if (count == last + 2u) image->offset = args[last + 1u];
        const TypeId sampler = metal_value_type(e, image->sampler);
        const TypeId coordinates = metal_value_type(e, image->coordinates);
        if (sampler < 7u || sampler - 7u >= c->type_count || c->types[sampler - 7u].kind != TYPE_SAMPLER ||
            c->types[sampler - 7u].state.image.flags !=
                (image->operation == IMAGE_SAMPLE_COMPARE_LEVEL ? DOE_COMPILER_HANDLE_COMPARISON : 0u) || type_leaf(c, coordinates) != IR_F32 ||
            type_vector_width(c, coordinates) != image->width ||
            (image->reference != NO_ID && metal_value_type(e, image->reference) != IR_F32) ||
            (image->level != NO_ID && !image->depth && metal_value_type(e, image->level) != IR_F32) ||
            (image->level != NO_ID && image->depth && !integer_operand(e, image->level, 1u))) {
            metal_error(e, "Metal explicit sampling operand types differ from the typed image operation");
            return false;
        }
    }
    if ((image->layer != NO_ID && !integer_operand(e, image->layer, 1u)) ||
        (image->sample != NO_ID && !integer_operand(e, image->sample, 1u)) ||
        (image->level != NO_ID && image->operation != IMAGE_SAMPLE_LEVEL && !integer_operand(e, image->level, 1u)))
        return false;
    if (image->data != NO_ID && (type_leaf(c, metal_value_type(e, image->data)) != t->element ||
        type_vector_width(c, metal_value_type(e, image->data)) != 4u)) {
        metal_error(e, "Metal texture store requires a matching four-component value");
        return false;
    }
    return !e->failed;
}
static void indexed_value(MetalEmitter *e, uint32_t value, uint32_t lane) {
    metal_value(e, value);
    if (type_vector_width(e->module, metal_value_type(e, value)) != 1u) metal_format(e, "[%u]", lane);
}
static void bounded_index(MetalEmitter *e, uint32_t value, uint32_t lane, const char *limit) {
    if (type_leaf(e->module, metal_value_type(e, value)) == IR_I32) {
        indexed_value(e, value, lane);
        metal_text(e, " >= 0 && ");
    }
    metal_text(e, "uint(");
    indexed_value(e, value, lane);
    metal_format(e, ") < %s", limit);
}
static void query(MetalEmitter *e, const MetalImage *image, const char *method, bool lod) {
    metal_value(e, image->texture);
    metal_format(e, ".%s(", method);
    if (lod && image->level != NO_ID && !image->one_dimensional && !image->storage && !image->multisampled) {
        metal_text(e, "uint(");
        metal_value(e, image->level);
        metal_text(e, ")");
    }
    metal_text(e, ")");
}
static void declare_query(MetalEmitter *e, const MetalImage *image, const char *name, const char *method, bool lod) {
    metal_indent(e);
    metal_format(e, "uint %s = ", name);
    query(e, image, method, lod);
    metal_text(e, ";\n");
}
static void dimensions(MetalEmitter *e, const MetalImage *image) {
    declare_query(e, image, "image_x", "get_width", true);
    if (image->width >= 2u) declare_query(e, image, "image_y", "get_height", true);
    if (image->width == 3u && !image->cube) declare_query(e, image, "image_z", "get_depth", true);
}
static void assign_result(MetalEmitter *e, const IrInstruction *ir) {
    metal_indent(e);
    metal_value(e, ir->result);
    metal_text(e, " = ");
}
static void unsigned_value(MetalEmitter *e, uint32_t value) {
    metal_text(e, "uint(");
    metal_value(e, value);
    metal_text(e, ")");
}
static void texel(MetalEmitter *e, const IrInstruction *ir, const MetalImage *image) {
    static const char *const limits[] = {"image_x", "image_y", "image_z"};
    dimensions(e, image);
    metal_indent(e);
    metal_text(e, "if (");
    for (uint32_t lane = 0u; lane < image->width; ++lane) {
        if (lane != 0u) metal_text(e, " && ");
        bounded_index(e, image->coordinates, lane, limits[lane]);
    }
    if (image->arrayed) {
        metal_text(e, " && ");
        bounded_index(e, image->layer, 0u, "image_layers");
    }
    if (image->multisampled) {
        metal_text(e, " && ");
        bounded_index(e, image->sample, 0u, "image_samples");
    }
    metal_text(e, ") {\n");
    ++e->indent;
    if (image->operation == IMAGE_LOAD) assign_result(e, ir);
    else metal_indent(e);
    metal_value(e, image->texture);
    metal_text(e, image->operation == IMAGE_LOAD ? ".read(" : ".write(");
    if (image->operation == IMAGE_STORE) {
        metal_value(e, image->data);
        metal_text(e, ", ");
    }
    metal_text(e, "uint");
    if (image->width != 1u) metal_format(e, "%u", image->width);
    metal_text(e, "(");
    metal_value(e, image->coordinates);
    metal_text(e, ")");
    if (image->arrayed) {
        metal_text(e, ", ");
        unsigned_value(e, image->layer);
    }
    if (image->sample != NO_ID || (image->level != NO_ID && !image->one_dimensional)) {
        metal_text(e, ", ");
        unsigned_value(e, image->sample != NO_ID ? image->sample : image->level);
    }
    metal_text(e, ");\n");
    --e->indent;
    metal_line(e, "}");
}
static void sample_level(MetalEmitter *e, const IrInstruction *ir, const MetalImage *image) {
    if (image->arrayed) {
        metal_line(e, "if (image_layers != 0u) {");
        ++e->indent;
        metal_indent(e);
        metal_text(e, "uint image_layer = ");
        if (metal_value_type(e, image->layer) == IR_I32) {
            metal_value(e, image->layer);
            metal_text(e, " < 0 ? 0u : ");
        }
        metal_text(e, "min(uint(");
        metal_value(e, image->layer);
        metal_text(e, "), image_layers - 1u);\n");
    }
    assign_result(e, ir);
    metal_value(e, image->texture);
    metal_text(e, image->reference != NO_ID ? ".sample_compare(" : ".sample(");
    metal_value(e, image->sampler);
    metal_text(e, ", ");
    metal_value(e, image->coordinates);
    if (image->arrayed) metal_text(e, ", image_layer");
    /* WebGPU 1D views have one level. MSL has no 1D LOD sample overload;
     * the supplied LOD has already evaluated, and the sole level is selected. */
    if (!image->one_dimensional) {
        if (image->reference != NO_ID) {
            metal_text(e, ", ");
            metal_value(e, image->reference);
            metal_text(e, ", level(0.0f)");
        } else {
            metal_text(e, ", level(float(");
            metal_value(e, image->level);
            metal_text(e, "))");
        }
    }
    if (image->offset != NO_ID) {
        if (image->offset == 0u || image->offset > e->module->instruction_count ||
            type_leaf(e->module, metal_value_type(e, image->offset)) != IR_I32 ||
            type_vector_width(e->module, metal_value_type(e, image->offset)) != 2u) {
            metal_error(e, "texture offset has no signed two-component constant identity");
            return;
        }
        const IrInstruction *offset = &e->module->instructions[image->offset - 1u];
        if (offset->opcode != IR_CONSTANT || offset->operands.scalar.left != 1u) {
            metal_error(e, "texture offset has no constant IR identity");
            return;
        }
        metal_text(e, ", ");
        metal_constant(e, offset->type, offset->operands.scalar.binding, true, 0u);
    }
    metal_text(e, ");\n");
    if (image->arrayed) {
        --e->indent;
        metal_line(e, "}");
    }
}
void metal_image(MetalEmitter *e, const IrInstruction *ir) {
    MetalImage image;
    if (!decode(e, ir, &image)) return;
    metal_line(e, "{");
    ++e->indent;
    if (ir->result != 0u) {
        assign_result(e, ir);
        metal_type(e, ir->type);
        metal_text(e, "{};\n");
    }
    if (image.arrayed) declare_query(e, &image, "image_layers", "get_array_size", false);
    if (image.multisampled) declare_query(e, &image, "image_samples", "get_num_samples", false);
    const bool checked_level = image.level != NO_ID && image.operation != IMAGE_SAMPLE_LEVEL;
    if (checked_level || image.operation == IMAGE_LEVELS) {
        if (image.one_dimensional) metal_line(e, "uint image_levels = 1u;");
        else declare_query(e, &image, "image_levels", "get_num_mip_levels", false);
    }
    if (checked_level) {
        metal_indent(e);
        metal_text(e, "if (");
        bounded_index(e, image.level, 0u, "image_levels");
        metal_text(e, ") {\n");
        ++e->indent;
    }
    switch (image.operation) {
        case IMAGE_LOAD: case IMAGE_STORE: texel(e, ir, &image); break;
        case IMAGE_SAMPLE_LEVEL: case IMAGE_SAMPLE_COMPARE_LEVEL: sample_level(e, ir, &image); break;
        case IMAGE_DIMENSIONS:
            dimensions(e, &image);
            assign_result(e, ir);
            if (image.width == 1u) metal_text(e, "image_x");
            else metal_format(e, "uint%u(image_x, image_y%s)", image.cube ? 2u : image.width,
                              image.width == 3u && !image.cube ? ", image_z" : "");
            metal_text(e, ";\n");
            break;
        case IMAGE_LEVELS: case IMAGE_LAYERS: case IMAGE_SAMPLES:
            assign_result(e, ir);
            metal_text(e, image.operation == IMAGE_LEVELS ? "image_levels;\n" :
                image.operation == IMAGE_LAYERS ? "image_layers;\n" : "image_samples;\n");
            break;
        default: metal_error(e, "Metal image operation has no implemented lowering"); break;
    }
    if (checked_level) {
        --e->indent;
        metal_line(e, "}");
    }
    --e->indent;
    metal_line(e, "}");
}
