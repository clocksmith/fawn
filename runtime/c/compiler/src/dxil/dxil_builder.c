/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "dxil_builder.h"

#include <string.h>

void dxil_builder_init(DxilBuilder *builder) {
    memset(builder, 0, sizeof(*builder));
}

static bool type_eql(const DxilType *a, const DxilType *b) {
    size_t i = 0;
    if (a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
        case DXIL_TKIND_STRUCT:
            /* Structs are always unique in Zig's type_eql. */
            return false;
        case DXIL_TKIND_POINTER:
            return a->pointee == b->pointee;
        case DXIL_TKIND_FUNCTION:
            if (a->return_type != b->return_type) {
                return false;
            }
            if (a->param_count != b->param_count) {
                return false;
            }
            if (a->is_vararg != b->is_vararg) {
                return false;
            }
            for (i = 0; i < a->param_count; i++) {
                if (a->params[i] != b->params[i]) {
                    return false;
                }
            }
            return true;
        case DXIL_TKIND_ARRAY:
            return a->elem == b->elem && a->len == b->len;
        case DXIL_TKIND_VECTOR:
            return a->elem == b->elem && a->len == b->len;
        case DXIL_TKIND_VOID:
        case DXIL_TKIND_I1:
        case DXIL_TKIND_I8:
        case DXIL_TKIND_I16:
        case DXIL_TKIND_I32:
        case DXIL_TKIND_I64:
        case DXIL_TKIND_F16:
        case DXIL_TKIND_F32:
        case DXIL_TKIND_F64:
        case DXIL_TKIND_METADATA:
        case DXIL_TKIND_LABEL:
            return true;
        default:
            return true;
    }
}

DxilStatus dxil_builder_add_type(DxilBuilder *builder, const DxilType *type,
    DxilTypeIndex *out_index) {
    if (builder->type_count >= DXIL_BUILDER_MAX_TYPES) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    builder->types[builder->type_count] = *type;
    *out_index = builder->type_count;
    builder->type_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_find_or_add_type(DxilBuilder *builder,
    const DxilType *type, DxilTypeIndex *out_index) {
    uint32_t i = 0;
    for (i = 0; i < builder->type_count; i++) {
        if (type_eql(&builder->types[i], type)) {
            *out_index = i;
            return DXIL_OK;
        }
    }
    return dxil_builder_add_type(builder, type, out_index);
}

static DxilStatus scalar_type(DxilBuilder *builder, DxilTypeKind kind,
    DxilTypeIndex *out) {
    DxilType type;
    memset(&type, 0, sizeof(type));
    type.kind = kind;
    return dxil_builder_find_or_add_type(builder, &type, out);
}

DxilStatus dxil_builder_type_void(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_VOID, out);
}

DxilStatus dxil_builder_type_i1(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_I1, out);
}

DxilStatus dxil_builder_type_i8(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_I8, out);
}

DxilStatus dxil_builder_type_i32(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_I32, out);
}

DxilStatus dxil_builder_type_i64(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_I64, out);
}

DxilStatus dxil_builder_type_f16(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_F16, out);
}

DxilStatus dxil_builder_type_f32(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_F32, out);
}

DxilStatus dxil_builder_type_pointer(DxilBuilder *builder,
    DxilTypeIndex pointee, DxilTypeIndex *out) {
    DxilType type;
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_TKIND_POINTER;
    type.pointee = pointee;
    return dxil_builder_find_or_add_type(builder, &type, out);
}

DxilStatus dxil_builder_type_function(DxilBuilder *builder,
    DxilTypeIndex return_type, const DxilTypeIndex *params,
    size_t param_count, bool is_vararg, DxilTypeIndex *out) {
    DxilType type;
    size_t i = 0;
    if (param_count > 8) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_TKIND_FUNCTION;
    type.return_type = return_type;
    type.param_count = (uint8_t)param_count;
    type.is_vararg = is_vararg;
    for (i = 0; i < param_count; i++) {
        type.params[i] = params[i];
    }
    return dxil_builder_find_or_add_type(builder, &type, out);
}

DxilStatus dxil_builder_type_struct(DxilBuilder *builder,
    const DxilTypeIndex *members, size_t member_count, const char *name,
    DxilTypeIndex *out) {
    DxilType type;
    size_t i = 0;
    if (member_count > 16) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_TKIND_STRUCT;
    type.member_count = (uint8_t)member_count;
    type.name = name;
    for (i = 0; i < member_count; i++) {
        type.members[i] = members[i];
    }
    /* Zig's type_struct appends directly instead of deduplicating. */
    return dxil_builder_add_type(builder, &type, out);
}

DxilStatus dxil_builder_type_array(DxilBuilder *builder, DxilTypeIndex elem,
    uint64_t len, DxilTypeIndex *out) {
    DxilType type;
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_TKIND_ARRAY;
    type.elem = elem;
    type.len = len;
    return dxil_builder_find_or_add_type(builder, &type, out);
}

DxilStatus dxil_builder_type_vector(DxilBuilder *builder, DxilTypeIndex elem,
    uint32_t len, DxilTypeIndex *out) {
    DxilType type;
    memset(&type, 0, sizeof(type));
    type.kind = DXIL_TKIND_VECTOR;
    type.elem = elem;
    type.len = len;
    return dxil_builder_find_or_add_type(builder, &type, out);
}

DxilStatus dxil_builder_type_metadata(DxilBuilder *builder,
    DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_METADATA, out);
}

DxilStatus dxil_builder_type_label(DxilBuilder *builder, DxilTypeIndex *out) {
    return scalar_type(builder, DXIL_TKIND_LABEL, out);
}

DxilStatus dxil_builder_add_function(DxilBuilder *builder, const char *name,
    DxilTypeIndex type_index, uint32_t linkage, bool is_definition,
    uint32_t *out_index) {
    DxilFuncDecl decl;
    if (builder->function_count >= DXIL_BUILDER_MAX_FUNCTIONS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&decl, 0, sizeof(decl));
    decl.name = name;
    decl.type_index = type_index;
    decl.linkage = linkage;
    decl.value_index = builder->next_value_id;
    builder->next_value_id += 1;
    decl.is_definition = is_definition;
    builder->functions[builder->function_count] = decl;
    *out_index = builder->function_count;
    builder->function_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_add_constant(DxilBuilder *builder,
    DxilTypeIndex type_index, const DxilConstantEntry *value,
    DxilValueIndex *out_value) {
    DxilConstantEntry entry;
    if (builder->constant_count >= DXIL_BUILDER_MAX_VALUES) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    entry = *value;
    entry.type_index = type_index;
    builder->constants[builder->constant_count] = entry;
    builder->constant_count += 1;
    *out_value = builder->next_value_id;
    builder->next_value_id += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_add_const_i32(DxilBuilder *builder, int32_t value,
    DxilValueIndex *out_value) {
    DxilTypeIndex type_index = 0;
    DxilConstantEntry entry;
    DxilStatus status = dxil_builder_type_i32(builder, &type_index);
    if (status != DXIL_OK) {
        return status;
    }
    memset(&entry, 0, sizeof(entry));
    entry.kind = DXIL_CONST_INT;
    entry.int_value = value;
    return dxil_builder_add_constant(builder, type_index, &entry, out_value);
}

DxilStatus dxil_builder_add_const_u32(DxilBuilder *builder, uint32_t value,
    DxilValueIndex *out_value) {
    DxilTypeIndex type_index = 0;
    DxilConstantEntry entry;
    DxilStatus status = dxil_builder_type_i32(builder, &type_index);
    if (status != DXIL_OK) {
        return status;
    }
    memset(&entry, 0, sizeof(entry));
    entry.kind = DXIL_CONST_UINT;
    entry.uint_value = value;
    return dxil_builder_add_constant(builder, type_index, &entry, out_value);
}

DxilStatus dxil_builder_add_const_f32(DxilBuilder *builder, float value,
    DxilValueIndex *out_value) {
    DxilTypeIndex type_index = 0;
    DxilConstantEntry entry;
    uint32_t bits = 0;
    DxilStatus status = dxil_builder_type_f32(builder, &type_index);
    if (status != DXIL_OK) {
        return status;
    }
    memcpy(&bits, &value, sizeof(bits));
    memset(&entry, 0, sizeof(entry));
    entry.kind = DXIL_CONST_FLOAT;
    entry.float_bits = bits;
    return dxil_builder_add_constant(builder, type_index, &entry, out_value);
}

DxilStatus dxil_builder_add_metadata_string(DxilBuilder *builder,
    const char *text, DxilMdIndex *out_index) {
    DxilMdNode node;
    if (builder->md_count >= DXIL_BUILDER_MAX_METADATA) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&node, 0, sizeof(node));
    node.kind = DXIL_MD_STRING;
    node.text = text;
    builder->metadata[builder->md_count] = node;
    *out_index = builder->md_count;
    builder->md_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_add_metadata_value(DxilBuilder *builder,
    DxilTypeIndex type_index, DxilValueIndex value_index,
    DxilMdIndex *out_index) {
    DxilMdNode node;
    if (builder->md_count >= DXIL_BUILDER_MAX_METADATA) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&node, 0, sizeof(node));
    node.kind = DXIL_MD_VALUE;
    node.type_index = type_index;
    node.value_index = value_index;
    builder->metadata[builder->md_count] = node;
    *out_index = builder->md_count;
    builder->md_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_add_metadata_node(DxilBuilder *builder,
    const bool *present, const DxilMdIndex *operands, size_t operand_count,
    DxilMdIndex *out_index) {
    DxilMdNode node;
    size_t i = 0;
    if (builder->md_count >= DXIL_BUILDER_MAX_METADATA) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    if (operand_count > 16) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&node, 0, sizeof(node));
    node.kind = DXIL_MD_NODE;
    node.count = (uint8_t)operand_count;
    for (i = 0; i < operand_count; i++) {
        node.operand_present[i] = present[i];
        node.operands[i] = operands[i];
    }
    builder->metadata[builder->md_count] = node;
    *out_index = builder->md_count;
    builder->md_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_add_named_metadata(DxilBuilder *builder,
    const char *name, const DxilMdIndex *nodes, size_t node_count) {
    DxilNamedMd entry;
    size_t i = 0;
    if (builder->named_md_count >= DXIL_BUILDER_MAX_NAMED_MD) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    if (node_count > 16) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&entry, 0, sizeof(entry));
    entry.name = name;
    entry.count = (uint8_t)node_count;
    for (i = 0; i < node_count; i++) {
        entry.node_indices[i] = nodes[i];
    }
    builder->named_md[builder->named_md_count] = entry;
    builder->named_md_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_add_symtab_entry(DxilBuilder *builder,
    DxilValueIndex value_index, const char *name) {
    DxilSymtabEntry entry;
    if (builder->symtab_count >= DXIL_BUILDER_MAX_SYMTAB_ENTRIES) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    memset(&entry, 0, sizeof(entry));
    entry.value_index = value_index;
    entry.name = name;
    builder->symtab[builder->symtab_count] = entry;
    builder->symtab_count += 1;
    return DXIL_OK;
}

DxilStatus dxil_builder_create_function_body(DxilBuilder *builder,
    uint32_t func_index, DxilFunctionBody *body) {
    if (func_index >= builder->function_count) {
        return DXIL_INVALID_IR;
    }
    memset(body, 0, sizeof(*body));
    body->func_index = func_index;
    body->num_blocks = 1;
    body->next_local_value = builder->next_value_id;
    return DXIL_OK;
}

DxilStatus dxil_body_add_op(DxilFunctionBody *body, const DxilBodyOp *op,
    DxilTypeIndex result_type, DxilValueIndex *out_value) {
    DxilValueIndex value = 0;
    if (body->op_count >= DXIL_BUILDER_MAX_BODY_OPS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    body->ops[body->op_count] = *op;
    body->op_types[body->op_count] = result_type;
    body->op_count += 1;
    value = body->next_local_value;
    body->next_local_value += 1;
    *out_value = value;
    return DXIL_OK;
}

DxilStatus dxil_body_add_void_op(DxilFunctionBody *body,
    const DxilBodyOp *op) {
    if (body->op_count >= DXIL_BUILDER_MAX_BODY_OPS) {
        return DXIL_OUTPUT_TOO_LARGE;
    }
    body->ops[body->op_count] = *op;
    body->op_types[body->op_count] = DXIL_BUILDER_INVALID_TYPE;
    body->op_count += 1;
    return DXIL_OK;
}
