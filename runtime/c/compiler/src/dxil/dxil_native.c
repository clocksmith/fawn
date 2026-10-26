/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_native.h"

#include "dxil_native_internal.h"
#include <errno.h>
#include <stdlib.h>

#include <string.h>

#include "dxil_builder.h"
#include "dxil_container.h"
#include "dxil_mapping.h"
#include "dxil_serialize.h"
#include "dxil_spec.h"



DxilStatus dxil_native_const_value(DxilNativeEmitter *emitter,
    DxilTypeIndex ty, DxilConstKind kind, int64_t int_value,
    uint64_t uint_value, uint64_t float_bits, DxilValueIndex *out) {
    DxilConstantEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.kind = kind;
    entry.int_value = int_value;
    entry.uint_value = uint_value;
    entry.float_bits = float_bits;
    return dxil_builder_add_constant(&emitter->builder, ty, &entry,
        out);
}

DxilStatus dxil_native_const_i32(DxilNativeEmitter *emitter, int32_t value,
    DxilValueIndex *out) {
    return dxil_builder_add_const_i32(&emitter->builder, value, out);
}

DxilStatus dxil_native_const_u32(DxilNativeEmitter *emitter, uint32_t value,
    DxilValueIndex *out) {
    return dxil_builder_add_const_u32(&emitter->builder, value, out);
}

DxilStatus dxil_native_const_f32(DxilNativeEmitter *emitter, float value,
    DxilValueIndex *out) {
    return dxil_builder_add_const_f32(&emitter->builder, value, out);
}

DxilStatus dxil_native_dx_op_call(DxilNativeEmitter *emitter,
    DxilFunctionBody *body, uint32_t opcode,
    const DxilValueIndex *extra_args, size_t extra_count,
    DxilValueIndex *out) {
    DxilBodyOp op;
    DxilValueIndex opcode_const = 0;
    DxilStatus status = dxil_native_const_u32(emitter, opcode, &opcode_const);
    size_t i = 0;
    if (status != DXIL_OK) {
        return status;
    }
    memset(&op, 0, sizeof(op));
    op.kind = DXIL_OP_CALL;
    op.result_type = emitter->i32_ty;
    op.callee =
        emitter->builder.functions[emitter->dx_op_fn].value_index;
    op.args[0] = opcode_const;
    op.arg_count = (uint8_t)(1 + extra_count);
    for (i = 0; i < extra_count; i++) {
        op.args[1 + i] = extra_args[i];
    }
    return dxil_body_add_op(body, &op, emitter->i32_ty, out);
}


static bool resource_type_kind(const DxilIrModule *module,
    DxilIrTypeId ty, DxilResourceTypeKind *out) {
    DxilIrType type;
    if (dxil_ir_type_get(module, ty, &type) != DXIL_OK) {
        return false;
    }
    switch (type.kind) {
        case DXIL_IR_TYPE_ARRAY:
            *out = DXIL_RESTYPE_ARRAY;
            return true;
        case DXIL_IR_TYPE_STRUCT:
            *out = DXIL_RESTYPE_STRUCT;
            return true;
        case DXIL_IR_TYPE_SAMPLER:
            *out = DXIL_RESTYPE_SAMPLER;
            return true;
        case DXIL_IR_TYPE_SAMPLER_COMPARISON:
            *out = DXIL_RESTYPE_SAMPLER_COMPARISON;
            return true;
        case DXIL_IR_TYPE_TEXTURE_1D:
            *out = DXIL_RESTYPE_TEXTURE_1D;
            return true;
        case DXIL_IR_TYPE_TEXTURE_2D:
            *out = DXIL_RESTYPE_TEXTURE_2D;
            return true;
        case DXIL_IR_TYPE_TEXTURE_2D_ARRAY:
            *out = DXIL_RESTYPE_TEXTURE_2D_ARRAY;
            return true;
        case DXIL_IR_TYPE_TEXTURE_DEPTH_2D:
            *out = DXIL_RESTYPE_TEXTURE_DEPTH_2D;
            return true;
        case DXIL_IR_TYPE_TEXTURE_3D:
            *out = DXIL_RESTYPE_TEXTURE_3D;
            return true;
        case DXIL_IR_TYPE_TEXTURE_CUBE:
            *out = DXIL_RESTYPE_TEXTURE_CUBE;
            return true;
        case DXIL_IR_TYPE_TEXTURE_DEPTH_CUBE:
            *out = DXIL_RESTYPE_TEXTURE_DEPTH_CUBE;
            return true;
        case DXIL_IR_TYPE_TEXTURE_MULTISAMPLED_2D:
            *out = DXIL_RESTYPE_TEXTURE_MULTISAMPLED_2D;
            return true;
        case DXIL_IR_TYPE_STORAGE_TEXTURE_2D:
            *out = DXIL_RESTYPE_STORAGE_TEXTURE_2D;
            return true;
        case DXIL_IR_TYPE_SCALAR:
        case DXIL_IR_TYPE_VECTOR:
        case DXIL_IR_TYPE_MATRIX:
        case DXIL_IR_TYPE_ATOMIC:
        case DXIL_IR_TYPE_REF:
        default:
            *out = DXIL_RESTYPE_OTHER;
            return true;
    }
}

static DxilStatus collect_resources(DxilNativeEmitter *emitter) {
    const DxilIrModule *module = emitter->module;
    uint32_t i = 0;
    for (i = 0; i < module->global_count; i++) {
        const DxilIrGlobal *global = &module->globals[i];
        DxilAddrSpace addr = DXIL_ADDR_OTHER;
        DxilAccess access = DXIL_ACCESS_NONE;
        DxilResourceTypeKind type_kind = DXIL_RESTYPE_OTHER;
        DxilIrType type;
        uint32_t class = 0;
        uint32_t kind = 0;
        if (!global->has_binding) {
            continue;
        }
        if (!global->has_addr_space) {
            continue;
        }
        switch (global->addr_space) {
            case DXIL_IR_ADDR_UNIFORM:
                addr = DXIL_ADDR_UNIFORM;
                break;
            case DXIL_IR_ADDR_STORAGE:
                addr = DXIL_ADDR_STORAGE;
                break;
            case DXIL_IR_ADDR_HANDLE:
                addr = DXIL_ADDR_HANDLE;
                break;
            case DXIL_IR_ADDR_FUNCTION:
            case DXIL_IR_ADDR_PRIVATE:
            case DXIL_IR_ADDR_WORKGROUP:
            default:
                addr = DXIL_ADDR_OTHER;
                break;
        }
        if (global->has_access) {
            switch (global->access) {
                case DXIL_IR_ACCESS_READ:
                    access = DXIL_ACCESS_READ;
                    break;
                case DXIL_IR_ACCESS_WRITE:
                    access = DXIL_ACCESS_WRITE;
                    break;
                case DXIL_IR_ACCESS_READ_WRITE:
                    access = DXIL_ACCESS_READ_WRITE;
                    break;
            }
        } else if (addr == DXIL_ADDR_STORAGE) {
            /* Zig defaults a missing storage access to read_write;
             * the mapping layer's NONE value means exactly that. */
            access = DXIL_ACCESS_NONE;
        }
        /* Storage-texture access comes from the type, not the global:
         * the mapping layer reads the caller-supplied access for
         * storage textures, so substitute the type's access here. */
        if (dxil_ir_type_get(module, global->ty, &type) == DXIL_OK
            && type.kind == DXIL_IR_TYPE_STORAGE_TEXTURE_2D) {
            access = type.access == DXIL_IR_ACCESS_READ
                ? DXIL_ACCESS_READ
                : DXIL_ACCESS_READ_WRITE;
        }
        if (!resource_type_kind(module, global->ty, &type_kind)) {
            continue;
        }
        if (!dxil_classify_resource(addr, access, type_kind, &class,
                &kind)) {
            continue;
        }
        if (emitter->resource_count >= DXIL_NATIVE_MAX_GLOBALS) {
            return DXIL_OUTPUT_TOO_LARGE;
        }
        emitter->resources[emitter->resource_count].class = class;
        emitter->resources[emitter->resource_count].kind = kind;
        emitter->resources[emitter->resource_count].group =
            global->binding.group;
        emitter->resources[emitter->resource_count].binding =
            global->binding.binding;
        emitter->resources[emitter->resource_count].global_index = i;
        emitter->resources[emitter->resource_count].handle_value = 0;
        emitter->resource_count += 1;
    }
    return DXIL_OK;
}

static DxilStatus metadata_value(DxilNativeEmitter *emitter,
    uint32_t value, DxilMdIndex *out) {
    DxilValueIndex constant = 0;
    DxilStatus status = dxil_native_const_u32(emitter, value, &constant);
    if (status != DXIL_OK) {
        return status;
    }
    return dxil_builder_add_metadata_value(&emitter->builder,
        emitter->i32_ty, constant, out);
}

static DxilStatus metadata_node(DxilNativeEmitter *emitter,
    const bool *present, const DxilMdIndex *operands,
    size_t operand_count, DxilMdIndex *out) {
    return dxil_builder_add_metadata_node(&emitter->builder, present,
        operands, operand_count, out);
}

static DxilStatus emit_dxil_metadata(DxilNativeEmitter *emitter,
    const DxilIrFunction *function, const DxilIrEntryPoint *entry) {
    DxilMdIndex ver_major = 0;
    DxilMdIndex ver_minor = 0;
    DxilMdIndex dx_version = 0;
    DxilMdIndex val_major = 0;
    DxilMdIndex val_minor = 0;
    DxilMdIndex val_version = 0;
    DxilMdIndex sm_kind_str = 0;
    DxilMdIndex sm_major = 0;
    DxilMdIndex sm_minor = 0;
    DxilMdIndex shader_model = 0;
    DxilMdIndex entry_name_str = 0;
    DxilMdIndex entry_point = 0;
    DxilStatus status = DXIL_OK;
    const char *prefix = dxil_stage_prefix(
        entry->stage == DXIL_IR_STAGE_COMPUTE
            ? DXIL_STAGE_COMPUTE
            : entry->stage == DXIL_IR_STAGE_VERTEX
                ? DXIL_STAGE_VERTEX
                : DXIL_STAGE_FRAGMENT);
    {
        bool present[2] = { true, true };
        DxilMdIndex operands[2];
        status = metadata_value(emitter, 1, &ver_major);
        if (status != DXIL_OK) {
            return status;
        }
        status = metadata_value(emitter, 0, &ver_minor);
        if (status != DXIL_OK) {
            return status;
        }
        operands[0] = ver_major;
        operands[1] = ver_minor;
        status = metadata_node(emitter, present, operands, 2,
            &dx_version);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_builder_add_named_metadata(&emitter->builder,
            "dx.version", &dx_version, 1);
        if (status != DXIL_OK) {
            return status;
        }
    }
    {
        bool present[2] = { true, true };
        DxilMdIndex operands[2];
        status = metadata_value(emitter, 1, &val_major);
        if (status != DXIL_OK) {
            return status;
        }
        status = metadata_value(emitter, 7, &val_minor);
        if (status != DXIL_OK) {
            return status;
        }
        operands[0] = val_major;
        operands[1] = val_minor;
        status = metadata_node(emitter, present, operands, 2,
            &val_version);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_builder_add_named_metadata(&emitter->builder,
            "dx.valver", &val_version, 1);
        if (status != DXIL_OK) {
            return status;
        }
    }
    {
        bool present[3] = { true, true, true };
        DxilMdIndex operands[3];
        status = dxil_builder_add_metadata_string(&emitter->builder,
            prefix, &sm_kind_str);
        if (status != DXIL_OK) {
            return status;
        }
        status = metadata_value(emitter, 6, &sm_major);
        if (status != DXIL_OK) {
            return status;
        }
        status = metadata_value(emitter, 0, &sm_minor);
        if (status != DXIL_OK) {
            return status;
        }
        operands[0] = sm_kind_str;
        operands[1] = sm_major;
        operands[2] = sm_minor;
        status = metadata_node(emitter, present, operands, 3,
            &shader_model);
        if (status != DXIL_OK) {
            return status;
        }
        status = dxil_builder_add_named_metadata(&emitter->builder,
            "dx.shaderModel", &shader_model, 1);
        if (status != DXIL_OK) {
            return status;
        }
    }
    status = dxil_builder_add_metadata_string(&emitter->builder,
        function->name, &entry_name_str);
    if (status != DXIL_OK) {
        return status;
    }
    if (entry->stage == DXIL_IR_STAGE_COMPUTE) {
        DxilMdIndex x = 0;
        DxilMdIndex y = 0;
        DxilMdIndex z = 0;
        DxilMdIndex num_threads = 0;
        DxilMdIndex tag = 0;
        DxilMdIndex props = 0;
        bool present3[3] = { true, true, true };
        DxilMdIndex operands3[3];
        bool present2[2] = { true, true };
        DxilMdIndex operands2[2];
        bool present4[4] = { true, false, false, true };
        DxilMdIndex operands4[4];
        status = metadata_value(emitter, entry->workgroup_size[0],
            &x);
        if (status != DXIL_OK) {
            return status;
        }
        status = metadata_value(emitter, entry->workgroup_size[1],
            &y);
        if (status != DXIL_OK) {
            return status;
        }
        status = metadata_value(emitter, entry->workgroup_size[2],
            &z);
        if (status != DXIL_OK) {
            return status;
        }
        operands3[0] = x;
        operands3[1] = y;
        operands3[2] = z;
        status = metadata_node(emitter, present3, operands3, 3,
            &num_threads);
        if (status != DXIL_OK) {
            return status;
        }
        status = metadata_value(emitter, DXIL_MD_NUM_THREADS, &tag);
        if (status != DXIL_OK) {
            return status;
        }
        operands2[0] = tag;
        operands2[1] = num_threads;
        status = metadata_node(emitter, present2, operands2, 2,
            &props);
        if (status != DXIL_OK) {
            return status;
        }
        operands4[0] = entry_name_str;
        operands4[1] = 0;
        operands4[2] = 0;
        operands4[3] = props;
        status = metadata_node(emitter, present4, operands4, 4,
            &entry_point);
        if (status != DXIL_OK) {
            return status;
        }
    } else {
        bool present4[4] = { true, false, false, false };
        DxilMdIndex operands4[4];
        operands4[0] = entry_name_str;
        operands4[1] = 0;
        operands4[2] = 0;
        operands4[3] = 0;
        status = metadata_node(emitter, present4, operands4, 4,
            &entry_point);
        if (status != DXIL_OK) {
            return status;
        }
    }
    return dxil_builder_add_named_metadata(&emitter->builder,
        "dx.entryPoints", &entry_point, 1);
}

typedef struct DxilNativeScratch {
    DxilNativeEmitter emitter;
    DxilFunctionBody body;
    uint8_t bitcode[DXIL_NATIVE_MAX_BITCODE_SIZE];
    uint8_t part[DXIL_NATIVE_MAX_DXIL_PART_SIZE];
} DxilNativeScratch;

static DxilStatus native_emit(const DxilIrModule *module, uint8_t *out,
    size_t out_len, size_t *written, DxilNativeScratch *scratch) {
    DxilNativeEmitter *emitter = &scratch->emitter;
    const DxilIrEntryPoint *entry = NULL;
    const DxilIrFunction *function = NULL;
    DxilFunctionBody *body = &scratch->body;
    DxilTypeIndex entry_fn_ty = 0;
    DxilTypeIndex dx_op_ty = 0;
    uint32_t entry_fn_idx = 0;
    DxilStatus status = DXIL_OK;
    uint8_t *bitcode_buf = scratch->bitcode;
    uint8_t *dxil_part_buf = scratch->part;
    uint8_t isgn_buf[64];
    uint8_t osgn_buf[64];
    uint8_t sfi0_buf[16];
    size_t bitcode_size = 0;
    size_t dxil_part_size = 0;
    size_t isgn_size = 0;
    size_t osgn_size = 0;
    size_t sfi0_size = 0;
    uint32_t shader_kind = 0;
    DxilPart parts[4];
    if (out == NULL || written == NULL) {
        return DXIL_INVALID_IR;
    }
    if (module->entry_point_count == 0) {
        return DXIL_INVALID_IR;
    }
    entry = &module->entry_points[0];
    if (entry->function >= module->function_count) {
        return DXIL_INVALID_IR;
    }
    function = &module->functions[entry->function];
    memset(emitter, 0, sizeof(*emitter));
    emitter->module = module;
    dxil_builder_init(&emitter->builder);
    memset(bitcode_buf, 0, DXIL_NATIVE_MAX_BITCODE_SIZE);
    status = dxil_builder_type_void(&emitter->builder, &emitter->void_ty);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_type_i1(&emitter->builder, &emitter->i1_ty);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_type_i32(&emitter->builder, &emitter->i32_ty);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_type_f32(&emitter->builder, &emitter->f32_ty);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_type_function(&emitter->builder,
        emitter->i32_ty, &emitter->i32_ty, 1, true, &dx_op_ty);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_add_function(&emitter->builder, "dx.op.i32",
        dx_op_ty, DXIL_LINKAGE_EXTERNAL, false, &emitter->dx_op_fn);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_type_function(&emitter->builder,
        emitter->void_ty, NULL, 0, false, &entry_fn_ty);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_add_function(&emitter->builder, function->name,
        entry_fn_ty, DXIL_LINKAGE_EXTERNAL, true, &entry_fn_idx);
    if (status != DXIL_OK) {
        return status;
    }
    status = collect_resources(emitter);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_create_function_body(&emitter->builder,
        entry_fn_idx, body);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_native_emit_entry_body(emitter, body, function, entry->stage);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_builder_add_symtab_entry(&emitter->builder,
        emitter->builder.functions[entry_fn_idx].value_index,
        function->name);
    if (status != DXIL_OK) {
        return status;
    }
    status = emit_dxil_metadata(emitter, function, entry);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_serialize(&emitter->builder, body, 1, bitcode_buf,
        DXIL_NATIVE_MAX_BITCODE_SIZE, &bitcode_size);
    if (status != DXIL_OK) {
        return status;
    }
    shader_kind = dxil_stage_shader_kind(
        entry->stage == DXIL_IR_STAGE_COMPUTE
            ? DXIL_STAGE_COMPUTE
            : entry->stage == DXIL_IR_STAGE_VERTEX
                ? DXIL_STAGE_VERTEX
                : DXIL_STAGE_FRAGMENT);
    status = dxil_write_program_part(shader_kind, 6, 0, bitcode_buf,
        bitcode_size, dxil_part_buf, DXIL_NATIVE_MAX_DXIL_PART_SIZE,
        &dxil_part_size);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_write_empty_signature(isgn_buf, sizeof(isgn_buf),
        &isgn_size);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_write_empty_signature(osgn_buf, sizeof(osgn_buf),
        &osgn_size);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_write_feature_flags(sfi0_buf, sizeof(sfi0_buf),
        &sfi0_size);
    if (status != DXIL_OK) {
        return status;
    }
    parts[0].fourcc = DXIL_PART_FOURCC_SFI0;
    parts[0].data = sfi0_buf;
    parts[0].len = sfi0_size;
    parts[1].fourcc = DXIL_PART_FOURCC_ISGN;
    parts[1].data = isgn_buf;
    parts[1].len = isgn_size;
    parts[2].fourcc = DXIL_PART_FOURCC_OSGN;
    parts[2].data = osgn_buf;
    parts[2].len = osgn_size;
    parts[3].fourcc = DXIL_PART_FOURCC_DXIL;
    parts[3].data = dxil_part_buf;
    parts[3].len = dxil_part_size;
    return dxil_write_container(parts, 4, out, out_len, written);
}

DxilStatus dxil_native_emit(const DxilIrModule *module, uint8_t *out,
    size_t out_len, size_t *written) {
    const int saved_errno = errno;
    if (module == NULL || out == NULL || written == NULL ||
        dxil_ir_validate(module) != DXIL_OK || module->entry_point_count == 0u ||
        module->entry_points[0].function >= module->function_count) return DXIL_INVALID_IR;
    DxilNativeScratch *scratch = malloc(sizeof(*scratch));
    if (scratch == NULL) { errno = saved_errno; return DXIL_OUT_OF_MEMORY; }
    const DxilStatus status = native_emit(module, out, out_len, written, scratch);
    free(scratch);
    errno = saved_errno;
    return status;
}
