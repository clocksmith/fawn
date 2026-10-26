/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_hlsl.h"

#include "dxil_hlsl_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


DxilStatus dxil_hlsl_write_text(DxilHlslEmitter *emitter,
    const char *text) {
    size_t len = strlen(text);
    if (emitter->pos + len > emitter->out_len) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memcpy(emitter->out + emitter->pos, text, len);
    emitter->pos += len;
    return DXIL_OK;
}

DxilStatus dxil_hlsl_write_indent(DxilHlslEmitter *emitter) {
    size_t i = 0;
    for (i = 0; i < emitter->indent; i++) {
        DxilStatus status = dxil_hlsl_write_text(emitter, " ");
        if (status != DXIL_OK) {
            return status;
        }
    }
    return DXIL_OK;
}

DxilStatus dxil_hlsl_write_u32(DxilHlslEmitter *emitter, uint32_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", value);
    return dxil_hlsl_write_text(emitter, buf);
}

DxilStatus dxil_hlsl_write_u64(DxilHlslEmitter *emitter, uint64_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return dxil_hlsl_write_text(emitter, buf);
}

/* Zig write_float: shortest decimal with ".0" appended when the text
 * has no '.' and no exponent. The C search tries precisions 1..17
 * with %.*g and keeps the first that round-trips through strtod. */
DxilStatus dxil_hlsl_write_float(DxilHlslEmitter *emitter, double value) {
    char buf[64];
    int precision = 0;
    bool found = false;
    for (precision = 1; precision <= 17; precision++) {
        snprintf(buf, sizeof(buf), "%.*g", precision, value);
        if (strtod(buf, NULL) == value) {
            found = true;
            break;
        }
    }
    if (!found) {
        snprintf(buf, sizeof(buf), "%.17g", value);
    }
    {
        DxilStatus status = dxil_hlsl_write_text(emitter, buf);
        if (status != DXIL_OK) {
            return status;
        }
    }
    if (strchr(buf, '.') == NULL && strchr(buf, 'e') == NULL
        && strchr(buf, 'E') == NULL) {
        return dxil_hlsl_write_text(emitter, ".0");
    }
    return DXIL_OK;
}


static const char *builtin_semantic(DxilIrBuiltin builtin) {
    switch (builtin) {
        case DXIL_IR_BUILTIN_POSITION:
            return "SV_Position";
        case DXIL_IR_BUILTIN_FRAG_DEPTH:
            return "SV_Depth";
        case DXIL_IR_BUILTIN_FRONT_FACING:
            return "SV_IsFrontFace";
        case DXIL_IR_BUILTIN_GLOBAL_INVOCATION_ID:
            return "SV_DispatchThreadID";
        case DXIL_IR_BUILTIN_LOCAL_INVOCATION_ID:
            return "SV_GroupThreadID";
        case DXIL_IR_BUILTIN_LOCAL_INVOCATION_INDEX:
            return "SV_GroupIndex";
        case DXIL_IR_BUILTIN_WORKGROUP_ID:
            return "SV_GroupID";
        case DXIL_IR_BUILTIN_SAMPLE_INDEX:
            return "SV_SampleIndex";
        case DXIL_IR_BUILTIN_SAMPLE_MASK:
            return "SV_Coverage";
        case DXIL_IR_BUILTIN_VERTEX_INDEX:
            return "SV_VertexID";
        case DXIL_IR_BUILTIN_INSTANCE_INDEX:
            return "SV_InstanceID";
        case DXIL_IR_BUILTIN_CLIP_DISTANCES:
            return "SV_ClipDistance";
        case DXIL_IR_BUILTIN_PRIMITIVE_INDEX:
            return "SV_PrimitiveID";
        case DXIL_IR_BUILTIN_NONE:
        case DXIL_IR_BUILTIN_NUM_WORKGROUPS:
        case DXIL_IR_BUILTIN_SUBGROUP_SIZE:
        case DXIL_IR_BUILTIN_SUBGROUP_INVOCATION_ID:
        default:
            return NULL;
    }
}

static bool module_uses_num_workgroups(
    const DxilIrModule *module) {
    uint32_t fi = 0;
    uint32_t pi = 0;
    for (fi = 0; fi < module->function_count; fi++) {
        const DxilIrFunction *function = &module->functions[fi];
        for (pi = 0; pi < function->param_count; pi++) {
            const DxilIrParam *param = &function->params[pi];
            if (param->has_io
                && param->io.builtin
                    == DXIL_IR_BUILTIN_NUM_WORKGROUPS) {
                return true;
            }
        }
    }
    return false;
}

static DxilStatus emit_structs(DxilHlslEmitter *emitter) {
    uint32_t si = 0;
    uint32_t fi = 0;
    DxilStatus status = DXIL_OK;
    for (si = 0; si < emitter->module->struct_count; si++) {
        const DxilIrStructDef *def = &emitter->module->structs[si];
        status = dxil_hlsl_write_text(emitter, "\nstruct ");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, def->name);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, " {\n");
        if (status != DXIL_OK) {
            return status;
        }
        emitter->indent += 4;
        for (fi = 0; fi < def->field_count; fi++) {
            const DxilIrStructField *field = &def->fields[fi];
            DxilIrType field_type;
            status = dxil_hlsl_write_indent(emitter);
            if (status != DXIL_OK) {
                return status;
            }
            if (dxil_ir_type_get(emitter->module, field->ty,
                    &field_type) == DXIL_OK
                && field_type.kind == DXIL_IR_TYPE_ARRAY
                && !field_type.has_len) {
                status = dxil_hlsl_emit_type_only(emitter, field_type.elem);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, " ");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, field->name);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, "[];\n");
                if (status != DXIL_OK) {
                    return status;
                }
                continue;
            }
            status = dxil_hlsl_emit_typed_name(emitter, field->ty, field->name);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, ";\n");
            if (status != DXIL_OK) {
                return status;
            }
        }
        emitter->indent -= 4;
        status = dxil_hlsl_write_text(emitter, "};\n");
        if (status != DXIL_OK) {
            return status;
        }
    }
    return DXIL_OK;
}

static DxilStatus emit_bound_global(DxilHlslEmitter *emitter,
    const DxilIrGlobal *global) {
    DxilStatus status = DXIL_OK;
    DxilIrType type;
    if (!global->has_binding) {
        return DXIL_INVALID_IR;
    }
    if (global->has_addr_space) {
        switch (global->addr_space) {
            case DXIL_IR_ADDR_UNIFORM:
                status = dxil_hlsl_write_text(emitter, "\ncbuffer doe_uniform_");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, global->name);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, " : register(b");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_u32(emitter, global->binding.binding);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, ", space");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_u32(emitter, global->binding.group);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, ") {\n");
                if (status != DXIL_OK) {
                    return status;
                }
                emitter->indent += 4;
                status = dxil_hlsl_write_indent(emitter);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_emit_typed_name(emitter, global->ty,
                    global->name);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, ";\n");
                if (status != DXIL_OK) {
                    return status;
                }
                emitter->indent -= 4;
                return dxil_hlsl_write_text(emitter, "};\n");
            case DXIL_IR_ADDR_STORAGE: {
                bool read = global->has_access
                    && global->access == DXIL_IR_ACCESS_READ;
                DxilIrTypeId elem_ty = global->ty;
                status = dxil_hlsl_write_text(emitter, "\n");
                if (status != DXIL_OK) {
                    return status;
                }
                if (dxil_ir_type_get(emitter->module, global->ty,
                        &type) == DXIL_OK
                    && type.kind == DXIL_IR_TYPE_ARRAY) {
                    elem_ty = type.elem;
                }
                status = dxil_hlsl_write_text(emitter, read
                    ? "StructuredBuffer<"
                    : "RWStructuredBuffer<");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_emit_type_only(emitter, elem_ty);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, "> ");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, global->name);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, " : register(");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, read ? "t" : "u");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_u32(emitter, global->binding.binding);
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_text(emitter, ", space");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_write_u32(emitter, global->binding.group);
                if (status != DXIL_OK) {
                    return status;
                }
                return dxil_hlsl_write_text(emitter, ");\n");
            }
            case DXIL_IR_ADDR_FUNCTION:
            case DXIL_IR_ADDR_PRIVATE:
            case DXIL_IR_ADDR_WORKGROUP:
            case DXIL_IR_ADDR_HANDLE:
            default:
                break;
        }
    }
    if (dxil_ir_type_get(emitter->module, global->ty, &type)
        != DXIL_OK) {
        return DXIL_INVALID_IR;
    }
    switch (type.kind) {
        case DXIL_IR_TYPE_SAMPLER:
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
            status = dxil_hlsl_write_text(emitter, "\nSamplerState ");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, global->name);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, " : register(s");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_u32(emitter, global->binding.binding);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, ", space");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_u32(emitter, global->binding.group);
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_hlsl_write_text(emitter, ");\n");
        case DXIL_IR_TYPE_TEXTURE_1D:
        case DXIL_IR_TYPE_TEXTURE_2D:
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
        case DXIL_IR_TYPE_TEXTURE_CUBE:
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
        case DXIL_IR_TYPE_TEXTURE_3D:
            status = dxil_hlsl_write_text(emitter, "\n");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_emit_type_only(emitter, global->ty);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, " ");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, global->name);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, " : register(t");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_u32(emitter, global->binding.binding);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, ", space");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_u32(emitter, global->binding.group);
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_hlsl_write_text(emitter, ");\n");
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D: {
            bool read = type.access == DXIL_IR_ACCESS_READ;
            status = dxil_hlsl_write_text(emitter, "\n");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, read
                ? "Texture2D<float4> " : "RWTexture2D<float4> ");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, global->name);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, " : register(");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, read ? "t" : "u");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_u32(emitter, global->binding.binding);
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_text(emitter, ", space");
            if (status != DXIL_OK) {
                return status;
            }
            status = dxil_hlsl_write_u32(emitter, global->binding.group);
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_hlsl_write_text(emitter, ");\n");
        }
        case DXIL_IR_TYPE_SCALAR:
        case DXIL_IR_TYPE_VECTOR:
        case DXIL_IR_TYPE_MATRIX:
        case DXIL_IR_TYPE_ARRAY:
        case DXIL_IR_TYPE_ATOMIC:
        case DXIL_IR_TYPE_STRUCT:
        case DXIL_IR_TYPE_REF:
        default:
            return DXIL_INVALID_IR;
    }
}

static DxilStatus emit_globals(DxilHlslEmitter *emitter) {
    uint32_t gi = 0;
    DxilStatus status = DXIL_OK;
    for (gi = 0; gi < emitter->module->global_count; gi++) {
        const DxilIrGlobal *global = &emitter->module->globals[gi];
        if (global->has_binding) {
            status = emit_bound_global(emitter, global);
            if (status != DXIL_OK) {
                return status;
            }
            continue;
        }
        switch (global->class) {
            case DXIL_IR_GLOBAL_VAR: {
                DxilIrAddressSpace addr = DXIL_IR_ADDR_PRIVATE;
                if (!global->has_addr_space) {
                    continue;
                }
                addr = global->addr_space;
                if (addr == DXIL_IR_ADDR_WORKGROUP) {
                    status = dxil_hlsl_write_text(emitter, "\ngroupshared ");
                } else if (addr == DXIL_IR_ADDR_PRIVATE) {
                    status = dxil_hlsl_write_text(emitter, "\nstatic ");
                } else {
                    continue;
                }
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_emit_typed_name(emitter, global->ty,
                    global->name);
                if (status != DXIL_OK) {
                    return status;
                }
                if (global->has_initializer) {
                    status = dxil_hlsl_write_text(emitter, " = ");
                    if (status != DXIL_OK) {
                        return status;
                    }
                    status = dxil_hlsl_emit_constant(emitter, global);
                    if (status != DXIL_OK) {
                        return status;
                    }
                }
                status = dxil_hlsl_write_text(emitter, ";\n");
                if (status != DXIL_OK) {
                    return status;
                }
                break;
            }
            case DXIL_IR_GLOBAL_CONST:
            case DXIL_IR_GLOBAL_OVERRIDE:
                status = dxil_hlsl_write_text(emitter, "\nstatic const ");
                if (status != DXIL_OK) {
                    return status;
                }
                status = dxil_hlsl_emit_typed_name(emitter, global->ty,
                    global->name);
                if (status != DXIL_OK) {
                    return status;
                }
                if (global->has_initializer) {
                    status = dxil_hlsl_write_text(emitter, " = ");
                    if (status != DXIL_OK) {
                        return status;
                    }
                    status = dxil_hlsl_emit_constant(emitter, global);
                    if (status != DXIL_OK) {
                        return status;
                    }
                }
                status = dxil_hlsl_write_text(emitter, ";\n");
                if (status != DXIL_OK) {
                    return status;
                }
                break;
            case DXIL_IR_GLOBAL_INPUT:
            case DXIL_IR_GLOBAL_OUTPUT:
            default:
                break;
        }
    }
    return DXIL_OK;
}

static DxilStatus emit_param(DxilHlslEmitter *emitter,
    const DxilIrParam *param) {
    DxilStatus status = DXIL_OK;
    DxilIrType type;
    if (dxil_ir_type_get(emitter->module, param->ty, &type)
        == DXIL_OK
        && type.kind == DXIL_IR_TYPE_REF) {
        status = dxil_hlsl_write_text(emitter,
            type.access == DXIL_IR_ACCESS_READ ? "in " : "inout ");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_emit_typed_name(emitter, type.elem, param->name);
    } else {
        status = dxil_hlsl_emit_typed_name(emitter, param->ty, param->name);
    }
    if (status != DXIL_OK) {
        return status;
    }
    if (param->has_io) {
        if (param->io.builtin != DXIL_IR_BUILTIN_NONE) {
            const char *semantic = builtin_semantic(param->io.builtin);
            if (semantic == NULL) {
                return DXIL_UNSUPPORTED_BUILTIN;
            }
            status = dxil_hlsl_write_text(emitter, " : ");
            if (status != DXIL_OK) {
                return status;
            }
            return dxil_hlsl_write_text(emitter, semantic);
        }
        if (param->io.has_location) {
            return DXIL_INVALID_IR;
        }
    }
    return DXIL_OK;
}

static DxilStatus emit_function(DxilHlslEmitter *emitter,
    const DxilIrFunction *function) {
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    bool first_param = true;
    status = dxil_hlsl_write_text(emitter, "\n");
    if (status != DXIL_OK) {
        return status;
    }
    if (function->has_stage) {
        if (function->stage != DXIL_IR_STAGE_COMPUTE) {
            return DXIL_UNSUPPORTED_CONSTRUCT;
        }
        status = dxil_hlsl_write_text(emitter, "[numthreads(");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_u32(emitter, function->workgroup_size[0]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, ", ");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_u32(emitter, function->workgroup_size[1]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, ", ");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_u32(emitter, function->workgroup_size[2]);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, ")]\n");
        if (status != DXIL_OK) {
            return status;
        }
    }
    status = dxil_hlsl_emit_type_only(emitter, function->return_type);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_hlsl_write_text(emitter, " ");
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_hlsl_write_text(emitter, function->name);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_hlsl_write_text(emitter, "(");
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < function->param_count; i++) {
        const DxilIrParam *param = &function->params[i];
        if (param->has_io
            && (dxil_hlsl_builtin_is_intrinsic(param->io.builtin)
                || param->io.builtin
                    == DXIL_IR_BUILTIN_NUM_WORKGROUPS)) {
            continue;
        }
        if (!first_param) {
            status = dxil_hlsl_write_text(emitter, ", ");
            if (status != DXIL_OK) {
                return status;
            }
        }
        status = emit_param(emitter, param);
        if (status != DXIL_OK) {
            return status;
        }
        first_param = false;
    }
    status = dxil_hlsl_write_text(emitter, ") {\n");
    if (status != DXIL_OK) {
        return status;
    }
    emitter->indent += 4;
    for (i = 0; i < function->param_count; i++) {
        const DxilIrParam *param = &function->params[i];
        if (!param->has_io
            || param->io.builtin != DXIL_IR_BUILTIN_NUM_WORKGROUPS) {
            continue;
        }
        status = dxil_hlsl_write_indent(emitter);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, "const uint3 ");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, param->name);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(emitter, " = doe_num_workgroups;\n");
        if (status != DXIL_OK) {
            return status;
        }
    }
    status = dxil_hlsl_emit_block_contents(emitter, function,
        function->root_stmt);
    if (status != DXIL_OK) {
        return status;
    }
    emitter->indent -= 4;
    return dxil_hlsl_write_text(emitter, "}\n");
}

DxilStatus dxil_hlsl_emit(const DxilIrModule *module, char *out,
    size_t out_len, size_t *written) {
    DxilHlslEmitter emitter;
    DxilStatus status = DXIL_OK;
    uint32_t fi = 0;
    if (out == NULL || written == NULL) {
        return DXIL_INVALID_IR;
    }
    memset(&emitter, 0, sizeof(emitter));
    emitter.module = module;
    emitter.out = out;
    emitter.out_len = out_len;
    status = dxil_hlsl_write_text(&emitter,
        "// Generated by doe_wgsl/dxil_hlsl (bounded subset)\n");
    if (status != DXIL_OK) {
        return status;
    }
    if (module_uses_num_workgroups(module)) {
        status = dxil_hlsl_write_text(&emitter,
            "\ncbuffer DoeDispatchInfo : register(b0, space7) {\n");
        if (status != DXIL_OK) {
            return status;
        }
        emitter.indent += 4;
        status = dxil_hlsl_write_indent(&emitter);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(&emitter, "uint3 doe_num_workgroups;\n");
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_indent(&emitter);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_hlsl_write_text(&emitter,
            "uint _doe_num_workgroups_pad;\n");
        if (status != DXIL_OK) {
            return status;
        }
        emitter.indent -= 4;
        status = dxil_hlsl_write_text(&emitter, "};\n");
        if (status != DXIL_OK) {
            return status;
        }
    }
    status = emit_structs(&emitter);
    if (status != DXIL_OK) {
        return status;
    }
    status = emit_globals(&emitter);
    if (status != DXIL_OK) {
        return status;
    }
    for (fi = 0; fi < module->function_count; fi++) {
        status = emit_function(&emitter, &module->functions[fi]);
        if (status != DXIL_OK) {
            return status;
        }
    }
    if (emitter.pos < emitter.out_len) {
        emitter.out[emitter.pos] = '\0';
    }
    *written = emitter.pos;
    return DXIL_OK;
}
