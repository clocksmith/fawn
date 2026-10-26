/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_serialize.h"

#include <string.h>

#include "dxil_bitcode.h"
#include "dxil_spec.h"

static uint64_t signed_rotate(int64_t value) {
    if (value >= 0) {
        return (uint64_t)value << 1;
    }
    if (value == INT64_MIN) {
        return UINT64_MAX;
    }
    return (((uint64_t)(-(value + 1))) << 1) | 1u;
}

static DxilStatus emit_type_block(DxilBitcodeWriter *writer,
    const DxilBuilder *builder) {
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    uint32_t count = 0;
    status = dxil_bitcode_enter_block(writer, DXIL_BLOCK_TYPE, 4);
    if (status != DXIL_OK) {
        return status;
    }
    count = builder->type_count;
    status = dxil_bitcode_emit_record(writer, DXIL_TYPE_NUMENTRY,
        (const uint64_t[]){ count }, 1);
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < builder->type_count; i++) {
        const DxilType *type = &builder->types[i];
        switch (type->kind) {
            case DXIL_TKIND_VOID:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_VOID,
                    NULL, 0);
                break;
            case DXIL_TKIND_I1:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_INTEGER,
                    (const uint64_t[]){ 1 }, 1);
                break;
            case DXIL_TKIND_I8:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_INTEGER,
                    (const uint64_t[]){ 8 }, 1);
                break;
            case DXIL_TKIND_I16:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_INTEGER,
                    (const uint64_t[]){ 16 }, 1);
                break;
            case DXIL_TKIND_I32:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_INTEGER,
                    (const uint64_t[]){ 32 }, 1);
                break;
            case DXIL_TKIND_I64:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_INTEGER,
                    (const uint64_t[]){ 64 }, 1);
                break;
            case DXIL_TKIND_F16:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_HALF,
                    NULL, 0);
                break;
            case DXIL_TKIND_F32:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_FLOAT,
                    NULL, 0);
                break;
            case DXIL_TKIND_F64:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_DOUBLE,
                    NULL, 0);
                break;
            case DXIL_TKIND_METADATA:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_METADATA,
                    NULL, 0);
                break;
            case DXIL_TKIND_LABEL:
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_LABEL,
                    NULL, 0);
                break;
            case DXIL_TKIND_POINTER: {
                const uint64_t ops[2] = { type->pointee, 0 };
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_POINTER,
                    ops, 2);
                break;
            }
            case DXIL_TKIND_FUNCTION: {
                uint64_t ops[10];
                size_t j = 0;
                ops[0] = type->is_vararg ? 1u : 0u;
                ops[1] = type->return_type;
                for (j = 0; j < type->param_count; j++) {
                    ops[2 + j] = type->params[j];
                }
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_FUNCTION,
                    ops, 2 + type->param_count);
                break;
            }
            case DXIL_TKIND_STRUCT: {
                uint64_t ops[17];
                size_t j = 0;
                if (type->name != NULL) {
                    status = dxil_bitcode_emit_string_record(writer,
                        DXIL_TYPE_STRUCT_NAMED, type->name);
                    if (status != DXIL_OK) {
                        return status;
                    }
                }
                ops[0] = 0;
                for (j = 0; j < type->member_count; j++) {
                    ops[1 + j] = type->members[j];
                }
                /* Zig computes a STRUCT_NAMED code here and discards it;
                 * the record code is always STRUCT_ANON. */
                status = dxil_bitcode_emit_record(writer,
                    DXIL_TYPE_STRUCT_ANON, ops, 1 + type->member_count);
                break;
            }
            case DXIL_TKIND_ARRAY: {
                const uint64_t ops[2] = { type->len, type->elem };
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_ARRAY,
                    ops, 2);
                break;
            }
            case DXIL_TKIND_VECTOR: {
                const uint64_t ops[2] = { type->len, type->elem };
                status = dxil_bitcode_emit_record(writer, DXIL_TYPE_VECTOR,
                    ops, 2);
                break;
            }
        }
        if (status != DXIL_OK) {
            return status;
        }
    }
    return dxil_bitcode_exit_block(writer);
}

static DxilStatus emit_function_decls(DxilBitcodeWriter *writer,
    const DxilBuilder *builder) {
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    for (i = 0; i < builder->function_count; i++) {
        const DxilFuncDecl *func = &builder->functions[i];
        const uint64_t ops[15] = {
            func->type_index,
            0,
            func->is_definition ? 0u : 1u,
            func->linkage,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        status = dxil_bitcode_emit_record(writer, DXIL_MODULE_FUNCTION,
            ops, 15);
        if (status != DXIL_OK) {
            return status;
        }
    }
    return DXIL_OK;
}

static DxilStatus emit_constants_block(DxilBitcodeWriter *writer,
    const DxilBuilder *builder) {
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    DxilTypeIndex current_type = DXIL_BUILDER_INVALID_TYPE;
    status = dxil_bitcode_enter_block(writer, DXIL_BLOCK_CONSTANTS, 4);
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < builder->constant_count; i++) {
        const DxilConstantEntry *entry = &builder->constants[i];
        if (entry->type_index != current_type) {
            status = dxil_bitcode_emit_record(writer, DXIL_CONSTANT_SETTYPE,
                (const uint64_t[]){ entry->type_index }, 1);
            if (status != DXIL_OK) {
                return status;
            }
            current_type = entry->type_index;
        }
        switch (entry->kind) {
            case DXIL_CONST_INT:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_CONSTANT_INTEGER,
                    (const uint64_t[]){ signed_rotate(entry->int_value) }, 1);
                break;
            case DXIL_CONST_UINT:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_CONSTANT_INTEGER,
                    (const uint64_t[]){ entry->uint_value << 1 }, 1);
                break;
            case DXIL_CONST_FLOAT:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_CONSTANT_FLOAT,
                    (const uint64_t[]){ entry->float_bits }, 1);
                break;
            case DXIL_CONST_NULL:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_CONSTANT_NULL, NULL, 0);
                break;
            case DXIL_CONST_UNDEF:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_CONSTANT_UNDEF, NULL, 0);
                break;
        }
        if (status != DXIL_OK) {
            return status;
        }
    }
    return dxil_bitcode_exit_block(writer);
}

static DxilStatus emit_metadata_block(DxilBitcodeWriter *writer,
    const DxilBuilder *builder) {
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    uint32_t j = 0;
    status = dxil_bitcode_enter_block(writer, DXIL_BLOCK_METADATA, 4);
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < builder->md_count; i++) {
        const DxilMdNode *md = &builder->metadata[i];
        switch (md->kind) {
            case DXIL_MD_STRING:
                status = dxil_bitcode_emit_string_record(writer,
                    DXIL_METADATA_STRING, md->text);
                break;
            case DXIL_MD_VALUE: {
                const uint64_t ops[2] = { md->type_index, md->value_index };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_METADATA_VALUE, ops, 2);
                break;
            }
            case DXIL_MD_NODE: {
                uint64_t ops[16];
                for (j = 0; j < md->count; j++) {
                    ops[j] = md->operand_present[j]
                        ? (uint64_t)md->operands[j] + 1u : 0u;
                }
                status = dxil_bitcode_emit_record(writer,
                    DXIL_METADATA_NODE, ops, md->count);
                break;
            }
        }
        if (status != DXIL_OK) {
            return status;
        }
    }
    for (i = 0; i < builder->named_md_count; i++) {
        const DxilNamedMd *named = &builder->named_md[i];
        uint64_t ops[16];
        status = dxil_bitcode_emit_string_record(writer,
            DXIL_METADATA_NAME, named->name);
        if (status != DXIL_OK) {
            return status;
        }
        for (j = 0; j < named->count; j++) {
            ops[j] = named->node_indices[j];
        }
        status = dxil_bitcode_emit_record(writer, DXIL_METADATA_NAMED_NODE,
            ops, named->count);
        if (status != DXIL_OK) {
            return status;
        }
    }
    return dxil_bitcode_exit_block(writer);
}

static DxilStatus emit_function_body(DxilBitcodeWriter *writer,
    const DxilFunctionBody *body) {
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    uint32_t j = 0;
    status = dxil_bitcode_enter_block(writer, DXIL_BLOCK_FUNCTION, 4);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_record(writer, DXIL_FUNCTION_DECLAREBLOCKS,
        (const uint64_t[]){ body->num_blocks }, 1);
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < body->op_count; i++) {
        const DxilBodyOp *op = &body->ops[i];
        switch (op->kind) {
            case DXIL_OP_RET_VOID:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_RET, NULL, 0);
                break;
            case DXIL_OP_RET_VALUE:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_RET,
                    (const uint64_t[]){ op->value }, 1);
                break;
            case DXIL_OP_CALL: {
                uint64_t ops[11];
                ops[0] = 0;
                ops[1] = op->result_type;
                ops[2] = op->callee;
                for (j = 0; j < op->arg_count; j++) {
                    ops[3 + j] = op->args[j];
                }
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_CALL, ops, 3 + op->arg_count);
                break;
            }
            case DXIL_OP_BINOP: {
                const uint64_t ops[3] = { op->lhs, op->rhs, op->opcode };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_BINOP, ops, 3);
                break;
            }
            case DXIL_OP_CAST: {
                const uint64_t ops[3] = { op->operand, op->dest_type,
                    op->opcode };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_CAST, ops, 3);
                break;
            }
            case DXIL_OP_LOAD: {
                const uint64_t ops[4] = { op->ptr, op->type_index,
                    op->align_log2, 0 };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_LOAD, ops, 4);
                break;
            }
            case DXIL_OP_STORE: {
                const uint64_t ops[4] = { op->ptr, op->value,
                    op->align_log2, 0 };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_STORE, ops, 4);
                break;
            }
            case DXIL_OP_ALLOCA: {
                const uint64_t ops[4] = { op->type_index, op->type_index,
                    op->count_value, op->align_log2 | (1u << 5) };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_ALLOCA, ops, 4);
                break;
            }
            case DXIL_OP_GEP: {
                uint64_t ops[7];
                ops[0] = op->inbounds ? 1u : 0u;
                ops[1] = op->base_type;
                ops[2] = op->ptr;
                for (j = 0; j < op->index_count; j++) {
                    ops[3 + j] = op->indices[j];
                }
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_GEP, ops, 3 + op->index_count);
                break;
            }
            case DXIL_OP_EXTRACT_VALUE: {
                const uint64_t ops[2] = { op->aggregate, op->index };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_EXTRACTVAL, ops, 2);
                break;
            }
            case DXIL_OP_INSERT_VALUE: {
                const uint64_t ops[3] = { op->aggregate, op->value,
                    op->index };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_INSERTVAL, ops, 3);
                break;
            }
            case DXIL_OP_CMP: {
                const uint64_t ops[3] = { op->lhs, op->rhs, op->predicate };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_CMP, ops, 3);
                break;
            }
            case DXIL_OP_BR:
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_BR,
                    (const uint64_t[]){ op->target }, 1);
                break;
            case DXIL_OP_BR_COND: {
                const uint64_t ops[3] = { op->true_block, op->false_block,
                    op->cond };
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_BR, ops, 3);
                break;
            }
            case DXIL_OP_PHI: {
                uint64_t ops[17];
                ops[0] = op->type_index;
                for (j = 0; j < op->incoming_count; j++) {
                    ops[1 + j * 2] = op->incoming[j].value;
                    ops[2 + j * 2] = op->incoming[j].block;
                }
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_PHI, ops,
                    1 + (size_t)op->incoming_count * 2);
                break;
            }
            case DXIL_OP_SWITCH: {
                uint64_t ops[34];
                ops[0] = op->cond;
                ops[1] = op->default_block;
                for (j = 0; j < op->case_count; j++) {
                    ops[2 + j * 2] = op->cases[j].value;
                    ops[3 + j * 2] = op->cases[j].block;
                }
                status = dxil_bitcode_emit_record(writer,
                    DXIL_FUNCTION_INST_SWITCH, ops,
                    2 + (size_t)op->case_count * 2);
                break;
            }
        }
        if (status != DXIL_OK) {
            return status;
        }
    }
    return dxil_bitcode_exit_block(writer);
}

static DxilStatus emit_value_symtab(DxilBitcodeWriter *writer,
    const DxilBuilder *builder) {
    DxilStatus status = DXIL_OK;
    uint32_t i = 0;
    status = dxil_bitcode_enter_block(writer, DXIL_BLOCK_VALUE_SYMTAB, 4);
    if (status != DXIL_OK) {
        return status;
    }
    for (i = 0; i < builder->symtab_count; i++) {
        const DxilSymtabEntry *entry = &builder->symtab[i];
        const uint64_t prefix[1] = { entry->value_index };
        status = dxil_bitcode_emit_record_with_string(writer,
            DXIL_SYMTAB_ENTRY, prefix, 1,
            (const uint8_t *)entry->name, strlen(entry->name));
        if (status != DXIL_OK) {
            return status;
        }
    }
    return dxil_bitcode_exit_block(writer);
}

DxilStatus dxil_serialize(const DxilBuilder *builder,
    const DxilFunctionBody *bodies, size_t body_count,
    uint8_t *out, size_t out_len, size_t *written) {
    DxilBitcodeWriter writer;
    DxilStatus status = DXIL_OK;
    uint8_t magic[4];
    size_t i = 0;
    dxil_bitcode_init(&writer, out, out_len);
    dxil_llvm_magic_bytes(magic);
    status = dxil_bitcode_emit_raw_bytes(&writer, magic, 4);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_enter_block(&writer, DXIL_BLOCK_MODULE, 3);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_record(&writer, DXIL_MODULE_VERSION,
        (const uint64_t[]){ 1 }, 1);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_string_record(&writer, DXIL_MODULE_TRIPLE,
        DXIL_TARGET_TRIPLE_CS);
    if (status != DXIL_OK) {
        return status;
    }
    status = dxil_bitcode_emit_string_record(&writer, DXIL_MODULE_DATALAYOUT,
        DXIL_TARGET_DATALAYOUT);
    if (status != DXIL_OK) {
        return status;
    }
    status = emit_type_block(&writer, builder);
    if (status != DXIL_OK) {
        return status;
    }
    status = emit_function_decls(&writer, builder);
    if (status != DXIL_OK) {
        return status;
    }
    if (builder->constant_count > 0) {
        status = emit_constants_block(&writer, builder);
        if (status != DXIL_OK) {
            return status;
        }
    }
    if (builder->md_count > 0 || builder->named_md_count > 0) {
        status = emit_metadata_block(&writer, builder);
        if (status != DXIL_OK) {
            return status;
        }
    }
    for (i = 0; i < body_count; i++) {
        status = emit_function_body(&writer, &bodies[i]);
        if (status != DXIL_OK) {
            return status;
        }
    }
    if (builder->symtab_count > 0) {
        status = emit_value_symtab(&writer, builder);
        if (status != DXIL_OK) {
            return status;
        }
    }
    status = dxil_bitcode_exit_block(&writer);
    if (status != DXIL_OK) {
        return status;
    }
    *written = dxil_bitcode_finalize(&writer);
    return DXIL_OK;
}
