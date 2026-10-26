/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include <string.h>

#include "dxil_bitcode.h"
#include "dxil_builder.h"
#include "dxil_serialize.h"
#include "dxil_spec.h"
#include "test_util.h"

/* Event-log decoder for serialized bitcode: walks blocks recursively
 * and records ENTER/RECORD/EXIT events in order, so tests can assert
 * exact record order without reimplementing a full LLVM reader. */

#define MAX_EVENTS 256
#define MAX_EVENT_OPS 128

typedef struct DxilTestEvent {
    int kind; /* 0 = enter, 1 = record, 2 = exit */
    uint32_t block_id;
    uint32_t code;
    uint64_t ops[MAX_EVENT_OPS];
    size_t op_count;
} DxilTestEvent;

static DxilTestEvent events[MAX_EVENTS];
static size_t event_count = 0;

static void push_event(int kind, uint32_t block_id, uint32_t code,
    const uint64_t *ops, size_t op_count) {
    DxilTestEvent *event = NULL;
    size_t i = 0;
    if (event_count >= MAX_EVENTS) {
        return;
    }
    event = &events[event_count];
    event_count++;
    event->kind = kind;
    event->block_id = block_id;
    event->code = code;
    event->op_count = op_count < MAX_EVENT_OPS ? op_count : MAX_EVENT_OPS;
    for (i = 0; i < event->op_count; i++) {
        event->ops[i] = ops[i];
    }
}

/* Parses one block's contents (length word already consumed by the
 * caller after ENTER). Records are collected for this block id until
 * its END_BLOCK; nested blocks recurse. */
static void parse_block_contents(DxilTestBitReader *reader,
    uint32_t block_id, unsigned width) {
    for (;;) {
        const uint64_t id = dxil_test_read_bits(reader, width);
        if (id == DXIL_ABBREV_END_BLOCK) {
            dxil_test_align32(reader);
            push_event(2, block_id, 0, NULL, 0);
            return;
        }
        if (id == DXIL_ABBREV_ENTER_SUBBLOCK) {
            const uint32_t sub_id = (uint32_t)dxil_test_read_vbr(reader, 8);
            const unsigned new_width =
                (unsigned)dxil_test_read_vbr(reader, 4);
            dxil_test_align32(reader);
            /* Skip the length word. */
            reader->byte_pos += 4;
            push_event(0, sub_id, 0, NULL, 0);
            parse_block_contents(reader, sub_id, new_width);
            continue;
        }
        if (id == DXIL_ABBREV_UNABBREV_RECORD) {
            const uint32_t code = (uint32_t)dxil_test_read_vbr(reader, 6);
            const uint64_t count = dxil_test_read_vbr(reader, 6);
            uint64_t ops[MAX_EVENT_OPS];
            uint64_t i = 0;
            for (i = 0; i < count && i < MAX_EVENT_OPS; i++) {
                ops[i] = dxil_test_read_vbr(reader, 6);
            }
            /* Drain any operands beyond the capture capacity. */
            for (; i < count; i++) {
                (void)dxil_test_read_vbr(reader, 6);
            }
            push_event(1, block_id, code, ops, (size_t)count);
            continue;
        }
        /* Unknown abbrev id: stop decoding rather than looping. */
        return;
    }
}

static void decode_module(const uint8_t *buf, size_t len) {
    DxilTestBitReader reader;
    uint32_t block_id = 0;
    unsigned width = 0;
    event_count = 0;
    dxil_test_reader_init(&reader, buf, len);
    /* Skip the 4-byte magic. */
    reader.byte_pos = 4;
    CHECK(dxil_test_read_bits(&reader, 2) == DXIL_ABBREV_ENTER_SUBBLOCK);
    block_id = (uint32_t)dxil_test_read_vbr(&reader, 8);
    width = (unsigned)dxil_test_read_vbr(&reader, 4);
    CHECK(block_id == DXIL_BLOCK_MODULE);
    dxil_test_align32(&reader);
    reader.byte_pos += 4;
    push_event(0, block_id, 0, NULL, 0);
    parse_block_contents(&reader, block_id, width);
}

static void check_string_event(size_t index, const char *expected) {
    const DxilTestEvent *event = &events[index];
    size_t i = 0;
    CHECK_MSG(events[index].kind == 1, "event %zu is not a record",
        index);
    for (i = 0; expected[i] != '\0'; i++) {
        CHECK_MSG(i < event->op_count
            && event->ops[i] == (uint64_t)(uint8_t)expected[i],
            "event %zu byte %zu mismatch", index, i);
    }
    CHECK_MSG(event->op_count == strlen(expected),
        "event %zu length %zu, expected %zu", index, event->op_count,
        strlen(expected));
}

static DxilBodyOp void_op(DxilBodyOpKind kind) {
    DxilBodyOp op;
    memset(&op, 0, sizeof(op));
    op.kind = kind;
    return op;
}

int main(void) {
    /* Zig test parity: builder dedup and counts. */
    {
        DxilBuilder builder;
        DxilTypeIndex void_ty = 0;
        DxilTypeIndex i32_ty = 0;
        DxilTypeIndex f32_ty = 0;
        DxilTypeIndex i32_again = 0;
        DxilTypeIndex fn_ty = 0;
        DxilValueIndex value = 0;
        uint32_t fn_index = 0;
        dxil_builder_init(&builder);
        CHECK_STATUS(dxil_builder_type_void(&builder, &void_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_i32(&builder, &i32_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_f32(&builder, &f32_ty), DXIL_OK);
        CHECK(void_ty != i32_ty);
        CHECK(i32_ty != f32_ty);
        CHECK_STATUS(dxil_builder_type_i32(&builder, &i32_again),
            DXIL_OK);
        CHECK(i32_again == i32_ty);
        CHECK_STATUS(dxil_builder_type_function(&builder, void_ty,
            &i32_ty, 1, false, &fn_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_add_function(&builder, "main", fn_ty,
            DXIL_LINKAGE_EXTERNAL, false, &fn_index), DXIL_OK);
        CHECK(fn_index == 0);
        CHECK(builder.functions[0].value_index == 0);
        CHECK_STATUS(dxil_builder_add_const_i32(&builder, 42, &value),
            DXIL_OK);
        CHECK(value == 1);
        CHECK(builder.function_count == 1);
        CHECK(builder.constant_count == 1);
    }

    /* Struct types are never deduplicated; function/struct/array/
     * vector capacity rules. */
    {
        DxilBuilder builder;
        DxilTypeIndex i32_ty = 0;
        DxilTypeIndex first = 0;
        DxilTypeIndex second = 0;
        DxilTypeIndex out = 0;
        DxilTypeIndex many_params[9];
        DxilTypeIndex many_members[17];
        size_t i = 0;
        dxil_builder_init(&builder);
        CHECK_STATUS(dxil_builder_type_i32(&builder, &i32_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_struct(&builder, &i32_ty, 1, NULL,
            &first), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_struct(&builder, &i32_ty, 1, NULL,
            &second), DXIL_OK);
        CHECK(first != second);
        for (i = 0; i < 9; i++) {
            many_params[i] = i32_ty;
        }
        CHECK_STATUS(dxil_builder_type_function(&builder, i32_ty,
            many_params, 9, false, &out), DXIL_OUTPUT_TOO_LARGE);
        for (i = 0; i < 17; i++) {
            many_members[i] = i32_ty;
        }
        CHECK_STATUS(dxil_builder_type_struct(&builder, many_members, 17,
            NULL, &out), DXIL_OUTPUT_TOO_LARGE);
        CHECK_STATUS(dxil_builder_type_array(&builder, i32_ty, 4, &out),
            DXIL_OK);
        {
            DxilTypeIndex again = 0;
            CHECK_STATUS(dxil_builder_type_array(&builder, i32_ty, 4,
                &again), DXIL_OK);
            CHECK(again == out);
        }
        /* Fill the type table to its 128 cap, then overflow. */
        for (i = builder.type_count; i < DXIL_BUILDER_MAX_TYPES; i++) {
            DxilTypeIndex added = 0;
            CHECK_STATUS(dxil_builder_type_struct(&builder, &i32_ty, 1,
                NULL, &added), DXIL_OK);
        }
        CHECK(builder.type_count == 128);
        CHECK_STATUS(dxil_builder_type_struct(&builder, &i32_ty, 1, NULL,
            &out), DXIL_OUTPUT_TOO_LARGE);
    }

    /* Constant encodings and value-id sequencing. */
    {
        DxilBuilder builder;
        DxilValueIndex v0 = 0;
        DxilValueIndex v1 = 0;
        DxilValueIndex v2 = 0;
        dxil_builder_init(&builder);
        CHECK_STATUS(dxil_builder_add_const_i32(&builder, -1, &v0),
            DXIL_OK);
        CHECK_STATUS(dxil_builder_add_const_u32(&builder, 7, &v1),
            DXIL_OK);
        CHECK_STATUS(dxil_builder_add_const_f32(&builder, 1.5f, &v2),
            DXIL_OK);
        CHECK(v0 == 0);
        CHECK(v1 == 1);
        CHECK(v2 == 2);
        /* add_const_u32 uses the i32 type, like Zig. */
        CHECK(builder.constants[1].type_index
            == builder.constants[0].type_index);
        CHECK(builder.constants[0].kind == DXIL_CONST_INT);
        CHECK(builder.constants[0].int_value == -1);
        CHECK(builder.constants[1].kind == DXIL_CONST_UINT);
        CHECK(builder.constants[1].uint_value == 7);
        CHECK(builder.constants[2].float_bits == 0x3FC00000u);
    }

    /* Function body: add_op advances next_local_value, add_void_op
     * does not; bad function index fails visibly; 1024-op cap. */
    {
        DxilBuilder builder;
        DxilTypeIndex void_ty = 0;
        DxilTypeIndex i32_ty = 0;
        DxilTypeIndex fn_ty = 0;
        uint32_t fn_index = 0;
        DxilFunctionBody body;
        DxilValueIndex value = 0;
        DxilBodyOp op = void_op(DXIL_OP_RET_VOID);
        dxil_builder_init(&builder);
        CHECK_STATUS(dxil_builder_type_void(&builder, &void_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_i32(&builder, &i32_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_function(&builder, void_ty,
            &i32_ty, 1, false, &fn_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_add_function(&builder, "main", fn_ty,
            DXIL_LINKAGE_EXTERNAL, true, &fn_index), DXIL_OK);
        CHECK_STATUS(dxil_builder_create_function_body(&builder,
            fn_index, &body), DXIL_OK);
        CHECK(body.next_local_value == 1);
        CHECK(body.num_blocks == 1);
        op.kind = DXIL_OP_BINOP;
        op.lhs = 0;
        op.rhs = 1;
        op.opcode = DXIL_BINOP_ADD;
        CHECK_STATUS(dxil_body_add_op(&body, &op, i32_ty, &value),
            DXIL_OK);
        CHECK(value == 1);
        CHECK(body.next_local_value == 2);
        op = void_op(DXIL_OP_RET_VOID);
        CHECK_STATUS(dxil_body_add_void_op(&body, &op), DXIL_OK);
        CHECK(body.next_local_value == 2);
        CHECK(body.op_types[1] == DXIL_BUILDER_INVALID_TYPE);
        CHECK_STATUS(dxil_builder_create_function_body(&builder, 99,
            &body), DXIL_INVALID_IR);
    }

    /* Metadata capacity rules. */
    {
        DxilBuilder builder;
        DxilMdIndex index = 0;
        bool present[17];
        DxilMdIndex operands[17];
        DxilMdIndex nodes[17];
        size_t i = 0;
        dxil_builder_init(&builder);
        for (i = 0; i < 17; i++) {
            present[i] = true;
            operands[i] = 0;
            nodes[i] = 0;
        }
        CHECK_STATUS(dxil_builder_add_metadata_node(&builder, present,
            operands, 17, &index), DXIL_OUTPUT_TOO_LARGE);
        CHECK_STATUS(dxil_builder_add_named_metadata(&builder, "n",
            nodes, 17), DXIL_OUTPUT_TOO_LARGE);
    }

    /* Zig test parity: serialize an empty module (void + i32). */
    {
        DxilBuilder builder;
        uint8_t out[4096];
        size_t size = 0;
        uint8_t magic[4];
        DxilTypeIndex void_ty = 0;
        DxilTypeIndex i32_ty = 0;
        dxil_builder_init(&builder);
        CHECK_STATUS(dxil_builder_type_void(&builder, &void_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_i32(&builder, &i32_ty), DXIL_OK);
        memset(out, 0, sizeof(out));
        CHECK_STATUS(dxil_serialize(&builder, NULL, 0, out, sizeof(out),
            &size), DXIL_OK);
        CHECK(size > 4);
        dxil_llvm_magic_bytes(magic);
        CHECK(memcmp(out, magic, 4) == 0);
        decode_module(out, size);
        /* MODULE enter, VERSION, TRIPLE, DATALAYOUT, TYPE enter,
         * NUMENTRY, VOID, INTEGER, TYPE exit, MODULE exit. */
        CHECK(event_count == 10);
        CHECK(events[0].kind == 0 && events[0].block_id == 8);
        CHECK(events[1].kind == 1 && events[1].code == 1);
        CHECK(events[1].op_count == 1 && events[1].ops[0] == 1);
        CHECK(events[2].kind == 1 && events[2].code == 2);
        check_string_event(2, "dxil-ms-dx");
        CHECK(events[3].kind == 1 && events[3].code == 3);
        check_string_event(3, DXIL_TARGET_DATALAYOUT);
        CHECK(events[4].kind == 0 && events[4].block_id == 17);
        CHECK(events[5].kind == 1 && events[5].code == 1);
        CHECK(events[5].op_count == 1 && events[5].ops[0] == 2);
        CHECK(events[6].kind == 1 && events[6].code == 2);
        CHECK(events[6].op_count == 0);
        CHECK(events[7].kind == 1 && events[7].code == 7);
        CHECK(events[7].op_count == 1 && events[7].ops[0] == 32);
        CHECK(events[8].kind == 2 && events[8].block_id == 17);
        CHECK(events[9].kind == 2 && events[9].block_id == 8);
    }

    /* Rich module: decls, constants with SETTYPE, metadata, a body,
     * and a symtab entry, in Zig order. */
    {
        DxilBuilder builder;
        DxilTypeIndex void_ty = 0;
        DxilTypeIndex i32_ty = 0;
        DxilTypeIndex f32_ty = 0;
        DxilTypeIndex ptr_ty = 0;
        DxilTypeIndex fn_ty = 0;
        DxilTypeIndex struct_ty = 0;
        DxilTypeIndex array_ty = 0;
        DxilTypeIndex vector_ty = 0;
        uint32_t dx_op_fn = 0;
        uint32_t entry_fn = 0;
        DxilValueIndex c_neg = 0;
        DxilValueIndex c_seven = 0;
        DxilValueIndex c_float = 0;
        DxilValueIndex md_val = 0;
        DxilMdIndex md_string = 0;
        DxilMdIndex md_value = 0;
        DxilMdIndex md_node = 0;
        DxilFunctionBody body;
        DxilBodyOp op;
        DxilValueIndex bin_result = 0;
        DxilValueIndex cmp_result = 0;
        uint8_t out[8192];
        size_t size = 0;
        size_t e = 0;
        bool saw_settype_i32 = false;
        bool saw_settype_f32 = false;
        bool saw_int_neg = false;
        bool saw_uint_seven = false;
        bool saw_float = false;
        bool saw_struct_named = false;
        bool saw_struct_anon = false;
        bool saw_named_node = false;
        bool saw_call = false;
        bool saw_symtab = false;
        dxil_builder_init(&builder);
        CHECK_STATUS(dxil_builder_type_void(&builder, &void_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_i32(&builder, &i32_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_f32(&builder, &f32_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_pointer(&builder, i32_ty,
            &ptr_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_function(&builder, i32_ty,
            &i32_ty, 1, true, &fn_ty), DXIL_OK);
        {
            const DxilTypeIndex members[2] = { i32_ty, f32_ty };
            CHECK_STATUS(dxil_builder_type_struct(&builder, members, 2,
                "S", &struct_ty), DXIL_OK);
        }
        CHECK_STATUS(dxil_builder_type_array(&builder, i32_ty, 4,
            &array_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_type_vector(&builder, f32_ty, 3,
            &vector_ty), DXIL_OK);
        CHECK_STATUS(dxil_builder_add_function(&builder, "dx.op.i32",
            fn_ty, DXIL_LINKAGE_EXTERNAL, false, &dx_op_fn), DXIL_OK);
        CHECK_STATUS(dxil_builder_add_function(&builder, "main", fn_ty,
            DXIL_LINKAGE_EXTERNAL, true, &entry_fn), DXIL_OK);
        CHECK_STATUS(dxil_builder_add_const_i32(&builder, -1, &c_neg),
            DXIL_OK);
        CHECK_STATUS(dxil_builder_add_const_u32(&builder, 7, &c_seven),
            DXIL_OK);
        CHECK_STATUS(dxil_builder_add_const_f32(&builder, 1.5f, &c_float),
            DXIL_OK);
        CHECK_STATUS(dxil_builder_add_metadata_string(&builder, "hello",
            &md_string), DXIL_OK);
        CHECK_STATUS(dxil_builder_add_metadata_value(&builder, i32_ty,
            c_neg, &md_value), DXIL_OK);
        {
            const bool present[3] = { true, false, true };
            const DxilMdIndex operands[3] = { md_string, 0, md_value };
            CHECK_STATUS(dxil_builder_add_metadata_node(&builder,
                present, operands, 3, &md_node), DXIL_OK);
        }
        CHECK_STATUS(dxil_builder_add_named_metadata(&builder,
            "dx.version", &md_node, 1), DXIL_OK);
        CHECK_STATUS(dxil_builder_create_function_body(&builder,
            entry_fn, &body), DXIL_OK);
        memset(&op, 0, sizeof(op));
        op.kind = DXIL_OP_BINOP;
        op.lhs = c_neg;
        op.rhs = c_seven;
        op.opcode = DXIL_BINOP_ADD;
        CHECK_STATUS(dxil_body_add_op(&body, &op, i32_ty, &bin_result),
            DXIL_OK);
        memset(&op, 0, sizeof(op));
        op.kind = DXIL_OP_CMP;
        op.lhs = bin_result;
        op.rhs = c_seven;
        op.predicate = DXIL_CMP_ICMP_EQ;
        CHECK_STATUS(dxil_body_add_op(&body, &op, i32_ty, &cmp_result),
            DXIL_OK);
        memset(&op, 0, sizeof(op));
        op.kind = DXIL_OP_CALL;
        op.result_type = i32_ty;
        op.callee = builder.functions[dx_op_fn].value_index;
        op.args[0] = c_seven;
        op.arg_count = 1;
        CHECK_STATUS(dxil_body_add_op(&body, &op, i32_ty, &md_val),
            DXIL_OK);
        op = void_op(DXIL_OP_RET_VOID);
        CHECK_STATUS(dxil_body_add_void_op(&body, &op), DXIL_OK);
        CHECK_STATUS(dxil_builder_add_symtab_entry(&builder,
            builder.functions[entry_fn].value_index, "main"), DXIL_OK);
        memset(out, 0, sizeof(out));
        CHECK_STATUS(dxil_serialize(&builder, &body, 1, out,
            sizeof(out), &size), DXIL_OK);
        decode_module(out, size);
        for (e = 0; e < event_count; e++) {
            const DxilTestEvent *event = &events[e];
            if (event->kind == 1 && event->block_id == 11
                && event->code == 1 && event->op_count == 1
                && event->ops[0] == i32_ty) {
                saw_settype_i32 = true;
            }
            if (event->kind == 1 && event->block_id == 11
                && event->code == 1 && event->op_count == 1
                && event->ops[0] == f32_ty) {
                saw_settype_f32 = true;
            }
            if (event->kind == 1 && event->block_id == 11
                && event->code == 4 && event->op_count == 1
                && event->ops[0] == 1) {
                saw_int_neg = true;
            }
            if (event->kind == 1 && event->block_id == 11
                && event->code == 4 && event->op_count == 1
                && event->ops[0] == 14) {
                saw_uint_seven = true;
            }
            if (event->kind == 1 && event->block_id == 11
                && event->code == 6 && event->op_count == 1
                && event->ops[0] == 0x3FC00000u) {
                saw_float = true;
            }
            if (event->kind == 1 && event->block_id == 17
                && event->code == 20) {
                saw_struct_named = true;
                check_string_event(e, "S");
            }
            if (event->kind == 1 && event->block_id == 17
                && event->code == 18 && event->op_count == 3
                && event->ops[0] == 0 && event->ops[1] == i32_ty
                && event->ops[2] == f32_ty) {
                saw_struct_anon = true;
            }
            if (event->kind == 1 && event->block_id == 15
                && event->code == 10) {
                saw_named_node = true;
            }
            if (event->kind == 1 && event->block_id == 12
                && event->code == 34 && event->op_count == 4
                && event->ops[0] == 0 && event->ops[1] == i32_ty) {
                saw_call = true;
            }
            if (event->kind == 1 && event->block_id == 14
                && event->code == 1) {
                saw_symtab = true;
            }
        }
        CHECK(saw_settype_i32);
        CHECK(saw_settype_f32);
        CHECK(saw_int_neg);
        CHECK(saw_uint_seven);
        CHECK(saw_float);
        CHECK(saw_struct_named);
        CHECK(saw_struct_anon);
        CHECK(saw_named_node);
        CHECK(saw_call);
        CHECK(saw_symtab);
        /* Function declarations carry isproto 1 for dx.op (a
         * declaration) and 0 for the entry definition. */
        {
            bool saw_proto = false;
            bool saw_definition = false;
            for (e = 0; e < event_count; e++) {
                const DxilTestEvent *event = &events[e];
                if (event->kind == 1 && event->block_id == 8
                    && event->code == 8 && event->op_count == 15
                    && event->ops[2] == 1) {
                    saw_proto = true;
                }
                if (event->kind == 1 && event->block_id == 8
                    && event->code == 8 && event->op_count == 15
                    && event->ops[2] == 0) {
                    saw_definition = true;
                }
            }
            CHECK(saw_proto);
            CHECK(saw_definition);
        }
        /* Metadata node operands are 1-based, null stays 0. */
        {
            bool saw_node = false;
            for (e = 0; e < event_count; e++) {
                const DxilTestEvent *event = &events[e];
                if (event->kind == 1 && event->block_id == 15
                    && event->code == 3 && event->op_count == 3
                    && event->ops[0] == (uint64_t)md_string + 1
                    && event->ops[1] == 0
                    && event->ops[2] == (uint64_t)md_value + 1) {
                    saw_node = true;
                }
            }
            CHECK(saw_node);
        }
    }

    /* Serialize fails visibly on a too-small buffer. */
    {
        DxilBuilder builder;
        uint8_t out[8];
        size_t size = 0;
        DxilTypeIndex void_ty = 0;
        dxil_builder_init(&builder);
        CHECK_STATUS(dxil_builder_type_void(&builder, &void_ty), DXIL_OK);
        CHECK_STATUS(dxil_serialize(&builder, NULL, 0, out, sizeof(out),
            &size), DXIL_OUTPUT_TOO_LARGE);
    }

    TEST_SUMMARY("test_dxil_builder_serialize");
}
