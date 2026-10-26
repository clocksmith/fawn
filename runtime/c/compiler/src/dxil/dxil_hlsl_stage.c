/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl_internal.h"
#include <stdio.h>

#define TRY(call) do { const DxilStatus result_ = (call); if (result_ != DXIL_OK) return result_; } while (0)

static const DxilIrStructDef *structure(const DxilHlslEmitter *e, DxilIrTypeId id) {
    if (id >= e->module->type_count) return NULL;
    const DxilIrType *type = &e->module->types[id];
    return type->kind == DXIL_IR_TYPE_STRUCT && type->struct_id < e->module->struct_count ?
        &e->module->structs[type->struct_id] : NULL;
}
static DxilStatus interpolation(DxilHlslEmitter *e, const DxilIrIoAttr *io) {
    if (io->builtin != DXIL_IR_BUILTIN_NONE || !io->has_interpolation) return DXIL_OK;
    switch (io->interpolation) {
        case DXIL_IR_INTERP_FLAT: return dxil_hlsl_write_text(e, "nointerpolation ");
        case DXIL_IR_INTERP_LINEAR: TRY(dxil_hlsl_write_text(e, "noperspective ")); break;
        case DXIL_IR_INTERP_PERSPECTIVE: break;
    }
    if (io->has_sampling) switch (io->sampling) {
        case DXIL_IR_SAMPLING_CENTER: break;
        case DXIL_IR_SAMPLING_FIRST: case DXIL_IR_SAMPLING_EITHER: return DXIL_INVALID_IR;
        case DXIL_IR_SAMPLING_CENTROID: TRY(dxil_hlsl_write_text(e, "centroid ")); break;
        case DXIL_IR_SAMPLING_SAMPLE: TRY(dxil_hlsl_write_text(e, "sample ")); break;
    }
    return DXIL_OK;
}
static DxilStatus semantic(DxilHlslEmitter *e, const DxilIrIoAttr *io,
    DxilIrStage stage, bool output) {
    if (io->has_location) {
        TRY(dxil_hlsl_write_text(e, output && stage == DXIL_IR_STAGE_FRAGMENT ? "SV_Target" : "TEXCOORD"));
        return dxil_hlsl_write_u32(e, io->has_blend_src ? io->blend_src : io->location);
    }
    const char *name = dxil_hlsl_builtin_semantic(io->builtin);
    return name == NULL ? DXIL_UNSUPPORTED_BUILTIN : dxil_hlsl_write_text(e, name);
}
static DxilStatus field(DxilHlslEmitter *e, DxilIrTypeId type, const char *name,
    const DxilIrIoAttr *io, DxilIrStage stage, bool output) {
    TRY(interpolation(e, io));
    TRY(dxil_hlsl_emit_typed_name(e, type, name));
    TRY(dxil_hlsl_write_text(e, " : "));
    return semantic(e, io, stage, output);
}
static DxilStatus output_structure(DxilHlslEmitter *e, const DxilIrFunction *f) {
    TRY(dxil_hlsl_write_text(e, "\nstruct "));
    TRY(dxil_hlsl_write_text(e, f->name));
    TRY(dxil_hlsl_write_text(e, "_stage_out {\n"));
    e->indent += 4u;
    if (f->has_return_io) {
        TRY(dxil_hlsl_write_indent(e));
        TRY(field(e, f->return_type, "value", &f->return_io, f->stage, true));
        TRY(dxil_hlsl_write_text(e, ";\n"));
    } else {
        const DxilIrStructDef *s = structure(e, f->return_type);
        if (s == NULL) return DXIL_INVALID_IR;
        for (uint32_t i = 0u; i < s->field_count; ++i) {
            const DxilIrStructField *member = &s->fields[i];
            if (!member->has_io) continue;
            TRY(dxil_hlsl_write_indent(e));
            TRY(field(e, member->ty, member->name, &member->io, f->stage, true));
            TRY(dxil_hlsl_write_text(e, ";\n"));
        }
    }
    e->indent -= 4u;
    return dxil_hlsl_write_text(e, "};\n");
}
static void input_name(char buffer[48], uint32_t parameter, uint32_t member) {
    (void)snprintf(buffer, 48u, "_doe_input_%u_%u", parameter, member);
}
static DxilStatus wrapper_parameters(DxilHlslEmitter *e, const DxilIrFunction *f) {
    bool first = true;
    for (uint32_t i = 0u; i < f->param_count; ++i) {
        const DxilIrParam *p = &f->params[i];
        const DxilIrStructDef *s = structure(e, p->ty);
        if (s == NULL) {
            if (!p->has_io) return DXIL_INVALID_IR;
            if (dxil_hlsl_builtin_is_intrinsic(p->io.builtin)) continue;
            if (!first) TRY(dxil_hlsl_write_text(e, ", "));
            TRY(field(e, p->ty, p->name, &p->io, f->stage, false));
            first = false;
        } else for (uint32_t j = 0u; j < s->field_count; ++j) {
            const DxilIrStructField *m = &s->fields[j];
            if (!m->has_io || dxil_hlsl_builtin_is_intrinsic(m->io.builtin)) continue;
            if (!first) TRY(dxil_hlsl_write_text(e, ", "));
            char name[48]; input_name(name, i, j);
            TRY(field(e, m->ty, name, &m->io, f->stage, false));
            first = false;
        }
    }
    return DXIL_OK;
}
static DxilStatus reconstruct(DxilHlslEmitter *e, const DxilIrFunction *f) {
    for (uint32_t i = 0u; i < f->param_count; ++i) {
        const DxilIrParam *p = &f->params[i];
        const DxilIrStructDef *s = structure(e, p->ty);
        if (s == NULL) continue;
        TRY(dxil_hlsl_write_indent(e));
        TRY(dxil_hlsl_emit_typed_name(e, p->ty, p->name));
        TRY(dxil_hlsl_write_text(e, ";\n"));
        for (uint32_t j = 0u; j < s->field_count; ++j) {
            const DxilIrStructField *m = &s->fields[j];
            if (!m->has_io) continue;
            char name[48]; input_name(name, i, j);
            const char *intrinsic = dxil_hlsl_builtin_intrinsic(m->io.builtin);
            TRY(dxil_hlsl_write_indent(e));
            TRY(dxil_hlsl_write_text(e, p->name));
            TRY(dxil_hlsl_write_text(e, "."));
            TRY(dxil_hlsl_write_text(e, m->name));
            TRY(dxil_hlsl_write_text(e, " = "));
            TRY(dxil_hlsl_write_text(e, intrinsic != NULL ? intrinsic : name));
            TRY(dxil_hlsl_write_text(e, ";\n"));
        }
    }
    return DXIL_OK;
}
static DxilStatus arguments(DxilHlslEmitter *e, const DxilIrFunction *f) {
    for (uint32_t i = 0u; i < f->param_count; ++i) {
        if (i != 0u) TRY(dxil_hlsl_write_text(e, ", "));
        const DxilIrParam *p = &f->params[i];
        const char *intrinsic = p->has_io ? dxil_hlsl_builtin_intrinsic(p->io.builtin) : NULL;
        TRY(dxil_hlsl_write_text(e, intrinsic == NULL ? p->name : intrinsic));
    }
    return DXIL_OK;
}
DxilStatus dxil_hlsl_emit_stage_return(DxilHlslEmitter *e, const DxilIrFunction *f,
    DxilIrExprId value) {
    if (value >= f->expr_count || e->stage_array_length == 0u || e->stage_array_length > 8u) return DXIL_INVALID_IR;
    TRY(dxil_hlsl_write_indent(e));
    TRY(dxil_hlsl_write_text(e, "{ "));
    TRY(dxil_hlsl_emit_typed_name(e, f->return_type, "_doe_stage_snapshot"));
    TRY(dxil_hlsl_write_text(e, " = "));
    const DxilIrExpr *expression = &f->exprs[value];
    if (expression->kind == DXIL_IR_EXPR_CONSTRUCT) {
        if (expression->args.len != e->stage_array_length) return DXIL_INVALID_IR;
        TRY(dxil_hlsl_write_text(e, "{"));
        TRY(dxil_hlsl_emit_expr_list(e, f, expression->args));
        TRY(dxil_hlsl_write_text(e, "}"));
    } else TRY(dxil_hlsl_emit_expr(e, f, value));
    TRY(dxil_hlsl_write_text(e, "; "));
    for (uint32_t i = 0u; i < e->stage_array_length; ++i) {
        TRY(dxil_hlsl_write_text(e, "_doe_stage_result["));
        TRY(dxil_hlsl_write_u32(e, i));
        TRY(dxil_hlsl_write_text(e, "] = _doe_stage_snapshot["));
        TRY(dxil_hlsl_write_u32(e, i));
        TRY(dxil_hlsl_write_text(e, "]; "));
    }
    return dxil_hlsl_write_text(e, "return; }\n");
}
DxilStatus dxil_hlsl_emit_stage(DxilHlslEmitter *e, const DxilIrFunction *f) {
    const DxilIrStructDef *result = structure(e, f->return_type);
    if (!f->has_return_io && (result == NULL ||
        !dxil_ir_type_has_io_struct_field(e->module, f->return_type))) return DXIL_INVALID_IR;
    e->stage_array_return = f->has_return_io && f->return_io.builtin == DXIL_IR_BUILTIN_CLIP_DISTANCES;
    if (e->stage_array_return) {
        if (f->return_type >= e->module->type_count) return DXIL_INVALID_IR;
        const DxilIrType *type = &e->module->types[f->return_type];
        if (type->kind != DXIL_IR_TYPE_ARRAY || !type->has_len || type->array_len == 0u || type->array_len > 8u)
            return DXIL_INVALID_IR;
        e->stage_array_length = type->array_len;
    }
    TRY(dxil_hlsl_emit_plain_function(e, f, true));
    const bool array_result = e->stage_array_return;
    e->stage_array_return = false;
    e->stage_array_length = 0u;
    TRY(output_structure(e, f));
    TRY(dxil_hlsl_write_text(e, "\n"));
    TRY(dxil_hlsl_write_text(e, f->name));
    TRY(dxil_hlsl_write_text(e, "_stage_out "));
    TRY(dxil_hlsl_write_text(e, f->name));
    TRY(dxil_hlsl_write_text(e, "("));
    TRY(wrapper_parameters(e, f));
    TRY(dxil_hlsl_write_text(e, ") {\n"));
    e->indent += 4u;
    TRY(reconstruct(e, f));
    TRY(dxil_hlsl_write_indent(e));
    TRY(dxil_hlsl_write_text(e, f->name));
    TRY(dxil_hlsl_write_text(e, "_stage_out _doe_out;\n"));
    TRY(dxil_hlsl_write_indent(e));
    if (!f->has_return_io) {
        TRY(dxil_hlsl_emit_typed_name(e, f->return_type, "_doe_result"));
        TRY(dxil_hlsl_write_text(e, " = "));
    } else if (!array_result) TRY(dxil_hlsl_write_text(e, "_doe_out.value = "));
    TRY(dxil_hlsl_write_text(e, f->name));
    TRY(dxil_hlsl_write_text(e, "_impl("));
    if (array_result) {
        TRY(dxil_hlsl_write_text(e, "_doe_out.value"));
        if (f->param_count != 0u) TRY(dxil_hlsl_write_text(e, ", "));
    }
    TRY(arguments(e, f));
    TRY(dxil_hlsl_write_text(e, ");\n"));
    if (!f->has_return_io) for (uint32_t i = 0u; i < result->field_count; ++i) {
        const DxilIrStructField *member = &result->fields[i];
        if (!member->has_io) continue;
        TRY(dxil_hlsl_write_indent(e));
        TRY(dxil_hlsl_write_text(e, "_doe_out."));
        TRY(dxil_hlsl_write_text(e, member->name));
        TRY(dxil_hlsl_write_text(e, " = _doe_result."));
        TRY(dxil_hlsl_write_text(e, member->name));
        TRY(dxil_hlsl_write_text(e, ";\n"));
    }
    TRY(dxil_hlsl_write_indent(e));
    TRY(dxil_hlsl_write_text(e, "return _doe_out;\n"));
    e->indent -= 4u;
    return dxil_hlsl_write_text(e, "}\n");
}
