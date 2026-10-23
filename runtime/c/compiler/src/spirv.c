/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "builtin.h"
#include "spirv_decl.h"
#include <string.h>

/* SPIR-V 1.3 numeric enums, from Khronos specification definitions. */
enum {
    OP_NAME = 5, OP_EXT_INST_IMPORT = 11, OP_EXT_INST = 12, OP_MEMORY_MODEL = 14, OP_ENTRY_POINT = 15, OP_EXECUTION_MODE = 16,
    OP_CAPABILITY = 17, OP_TYPE_VOID = 19, OP_TYPE_BOOL = 20, OP_TYPE_INT = 21,
    OP_TYPE_FLOAT = 22, OP_TYPE_VECTOR = 23, OP_TYPE_ARRAY = 28, OP_TYPE_RUNTIME_ARRAY = 29, OP_TYPE_STRUCT = 30,
    OP_TYPE_POINTER = 32, OP_TYPE_FUNCTION = 33, OP_CONSTANT_TRUE = 41, OP_CONSTANT_FALSE = 42,
    OP_CONSTANT = 43, OP_CONSTANT_COMPOSITE = 44, OP_CONSTANT_NULL = 46, OP_FUNCTION = 54, OP_FUNCTION_PARAMETER = 55, OP_FUNCTION_CALL = 57,
    OP_FUNCTION_END = 56, OP_VARIABLE = 59, OP_LOAD = 61, OP_STORE = 62,
    OP_ACCESS_CHAIN = 65, OP_ARRAY_LENGTH = 68, OP_DECORATE = 71,
    OP_MEMBER_DECORATE = 72, OP_VECTOR_EXTRACT_DYNAMIC = 77, OP_VECTOR_SHUFFLE = 79,
    OP_COMPOSITE_CONSTRUCT = 80, OP_COMPOSITE_EXTRACT = 81, OP_COPY_OBJECT = 83,
    OP_CONVERT_F_TO_U = 109, OP_CONVERT_F_TO_S = 110, OP_CONVERT_S_TO_F = 111, OP_CONVERT_U_TO_F = 112,
    OP_BITCAST = 124, OP_SNEGATE = 126, OP_FNEGATE = 127, OP_IADD = 128, OP_FADD = 129,
    OP_ISUB = 130, OP_FSUB = 131, OP_IMUL = 132, OP_FMUL = 133,
    OP_UDIV = 134, OP_SDIV = 135, OP_FDIV = 136, OP_UMOD = 137, OP_SREM = 138, OP_FREM = 140,
    OP_DOT = 148, OP_IS_NAN = 156, OP_LOGICAL_OR = 166, OP_LOGICAL_AND = 167,
    OP_LOGICAL_EQUAL = 164, OP_LOGICAL_NOT_EQUAL = 165, OP_LOGICAL_NOT = 168, OP_SELECT = 169, OP_IEQUAL = 170, OP_INOT_EQUAL = 171,
    OP_UGREATER_THAN = 172, OP_SGREATER_THAN = 173, OP_UGREATER_EQUAL = 174, OP_SGREATER_EQUAL = 175,
    OP_ULESS_THAN = 176, OP_SLESS_THAN = 177, OP_ULESS_EQUAL = 178, OP_SLESS_EQUAL = 179, OP_FORD_EQUAL = 180, OP_FUNORD_NOT_EQUAL = 183,
    OP_FORD_LESS_THAN = 184, OP_FORD_GREATER_THAN = 186, OP_FORD_LESS_EQUAL = 188,
    OP_FORD_GREATER_EQUAL = 190,
    OP_SHIFT_RIGHT_LOGICAL = 194, OP_SHIFT_RIGHT_ARITHMETIC = 195, OP_SHIFT_LEFT = 196,
    OP_BIT_OR = 197, OP_BIT_XOR = 198, OP_BIT_AND = 199, OP_NOT = 200, OP_CONTROL_BARRIER = 224,
    OP_PHI = 245, OP_LOOP_MERGE = 246, OP_SELECTION_MERGE = 247, OP_LABEL = 248,
    OP_BRANCH = 249, OP_BRANCH_CONDITIONAL = 250, OP_RETURN = 253, OP_RETURN_VALUE = 254, OP_UNREACHABLE = 255
};
/* Primitive/guard declarations use the frozen bounded allowance, never D or a
 * semantic-value slot. Aggregate declarations are caller-owned descriptors. */
enum {
    ID_VOID = 1, ID_BOOL, ID_U32, ID_F32, ID_I32, ID_VEC3,
    ID_PTR_INPUT, ID_PTR_INPUT_U32, ID_FN_TYPE, ID_ZERO_U32, ID_ZERO_F32,
    ID_ZERO_I32, ID_FALSE, ID_TRUE, ID_ONE_U32, ID_ONE_I32, ID_ONE_F32,
    ID_MIN_I32, ID_NEG_ONE_I32, ID_SHIFT_MASK, ID_F32_U32_MAX,
    ID_F32_I32_MAX, ID_F32_I32_MIN, ID_TWO_U32, ID_THREE_U32, ID_FOUR_U32,
    FIRST_DYNAMIC_ID
};
enum { WALK_TYPE, WALK_CONSTANT, WALK_ACCESS };
typedef struct {
    Source *source;
    CompilerWorkspace *program;
    uint32_t next_id, id_bound, current_label, inputs[MAX_ENTRY_PARAMETERS];
    uint32_t *values;
    uint32_t local_base, local_start, local_pointers[4][5], math_import;
    uint32_t workgroup_input, barrier_semantics;
    size_t function_end;
    uint32_t constants_used;
    bool hidden_workgroup_input, failed;
} Emitter;
static bool emit_error(Emitter *e, uint32_t offset, const char *message) {
    e->failed = true;
    return source_error(e->source, DOE_COMPILER_INPUT_ERROR, offset, message);
}
static uint32_t reserve_ids(Emitter *e, uint32_t count) {
    if (e->failed) return 0u;
    if (count > e->id_bound - e->next_id) {
        if (e->id_bound == UINT32_MAX - 1u) {
            e->failed = true;
            (void)source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->source->length,
                               "SPIR-V target ID format capacity exceeded");
        } else {
            (void)emit_error(e, (uint32_t)e->source->length, "target ID capacity exceeded");
        }
        return 0u;
    }
    const uint32_t first = e->next_id;
    e->next_id += count;
    return first;
}
static uint32_t scalar_type(TypeId type) {
    switch (type) {
        case IR_VOID: return ID_VOID;
        case IR_BOOL: return ID_BOOL;
        case IR_I32: return ID_I32;
        case IR_U32: return ID_U32;
        case IR_F32: return ID_F32;
        default: return 0u;
    }
}
static uint32_t target_type(const Emitter *e, TypeId type) {
    return type < 7u ? scalar_type(type) : e->program->types[type - 7u].target;
}
static bool instruction_space(Emitter *e, uint32_t opcode, size_t count, uint32_t **arguments) {
    CompilerWorkspace *c = e->program;
    if (e->failed) return false;
    if (count >= UINT16_MAX) {
        e->failed = true;
        return source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->source->length,
                            "SPIR-V instruction word-count format capacity exceeded");
    }
    if (count + 1u > c->limits.words - c->word_count) {
        e->failed = true;
        return source_pool_error(e->source, DOE_COMPILER_POOL_WORDS,
            c->word_count + count + 1u, e->source->length, "internal SPIR-V word budget exceeded");
    }
    c->words[c->word_count++] = ((uint32_t)(count + 1u) << 16u) | opcode;
    *arguments = c->words + c->word_count;
    c->word_count += count;
    return true;
}
static bool instruction(Emitter *e, uint32_t opcode, const uint32_t *arguments, size_t count) {
    uint32_t *words = NULL;
    if (!instruction_space(e, opcode, count, &words)) return false;
    if (count != 0u) memcpy(words, arguments, count * sizeof(*arguments));
    return true;
}
#define EMIT(opcode, ...) do { \
    const uint32_t arguments[] = { __VA_ARGS__ }; \
    if (!instruction(e, opcode, arguments, sizeof(arguments) / sizeof(arguments[0]))) return false; \
} while (0)
static size_t span_words(const Source *source, NameSpan name, uint32_t *words) {
    const size_t count = ((size_t)name.length + 4u) / 4u;
    memset(words, 0, count * sizeof(*words));
    for (uint32_t i = 0u; i < name.length; ++i)
        words[i / 4u] |= (uint32_t)(unsigned char)source->source[name.offset + i] << ((i % 4u) * 8u);
    return count;
}
static bool debug_name(Emitter *e, uint32_t id, NameSpan name) {
    uint32_t arguments[17] = { id };
    if (name.length > 63u || name.offset > e->source->length || name.length > e->source->length - name.offset)
        return emit_error(e, name.offset, "invalid target source name span");
    const size_t count = span_words(e->source, name, arguments + 1u);
    return instruction(e, OP_NAME, arguments, count + 1u);
}
static TargetDeclaration *append_declaration(Emitter *e, uint32_t kind, TypeId type) {
    CompilerWorkspace *c = e->program;
    if (c->target_count == c->limits.targets) {
        e->failed = true;
        (void)source_pool_error(e->source, DOE_COMPILER_POOL_TARGET_DECLARATIONS,
            (size_t)c->target_count + 1u, e->source->length, "target declaration capacity exceeded");
        return NULL;
    }
    TargetDeclaration *d = &c->targets[c->target_count++];
    *d = (TargetDeclaration){ .kind = kind, .id = reserve_ids(e, 1u), .type = type };
    return e->failed ? NULL : d;
}
static bool push_walk(Emitter *e, uint32_t kind, uint32_t object) {
    CompilerWorkspace *c = e->program;
    if (c->walk_count == c->limits.traversal) {
        e->failed = true;
        return source_pool_error(e->source, DOE_COMPILER_POOL_TRAVERSAL,
            (size_t)c->walk_count + 1u, e->source->length, "target traversal capacity exceeded");
    }
    c->walk[c->walk_count++] = (WalkItem){ .kind = kind, .object = object };
    return true;
}
static uint32_t use_constant(Emitter *e, uint32_t id) {
    e->constants_used |= UINT32_C(1) << id;
    return id;
}
static uint32_t unsigned_constant(Emitter *e, uint32_t value) {
    switch (value) {
        case 0u: return use_constant(e, ID_ZERO_U32);
        case 1u: return use_constant(e, ID_ONE_U32);
        case 2u: return use_constant(e, ID_TWO_U32);
        case 3u: return use_constant(e, ID_THREE_U32);
        case 4u: return use_constant(e, ID_FOUR_U32);
        case 31u: return use_constant(e, ID_SHIFT_MASK);
        default: break;
    }
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i)
        if (c->targets[i].kind == DECL_U32 && c->targets[i].payload == value) return c->targets[i].id;
    TargetDeclaration *d = append_declaration(e, DECL_U32, IR_U32);
    if (d == NULL) return 0u;
    d->payload = value;
    return d->id;
}
static uint32_t pointer_type(Emitter *e, TypeId type, uint32_t storage) {
    CompilerWorkspace *c = e->program;
    const uint32_t slot = storage == 7u ? 0u : storage == 2u ? 1u : storage == 12u ? 2u : 3u;
    if (type >= IR_BOOL && type <= IR_F32) {
        if (e->local_pointers[slot][type] == 0u) e->local_pointers[slot][type] = reserve_ids(e, 1u);
        return e->local_pointers[slot][type];
    }
    for (uint32_t i = 0u; i < c->target_count; ++i) {
        const TargetDeclaration *d = &c->targets[i];
        if (d->kind == DECL_POINTER && d->type == type && d->storage == storage) return d->id;
    }
    TargetDeclaration *d = append_declaration(e, DECL_POINTER, type);
    if (d == NULL) return 0u;
    d->storage = storage; d->payload = target_type(e, type);
    return d->id;
}
static uint32_t zero_constant(Emitter *e, TypeId type) {
    if (type < 7u) return use_constant(e, type == IR_BOOL ? ID_FALSE : type == IR_I32 ? ID_ZERO_I32 :
        type == IR_U32 ? ID_ZERO_U32 : type == IR_F32 ? ID_ZERO_F32 : 0u);
    TypeRecord *t = &e->program->types[type - 7u];
    if (t->aux != 0u) return t->aux;
    TargetDeclaration *d = append_declaration(e, DECL_ZERO, type);
    if (d == NULL) return 0u;
    t->aux = d->id;
    return d->id;
}
/* Workgroup initialization uses OpConstantNull even for a scalar root. Those
 * scalar nulls are D descriptors, distinct from the fixed ordinary zero IDs. */
static uint32_t workgroup_zero(Emitter *e, TypeId type) {
    if (type >= 7u) return zero_constant(e, type);
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i)
        if (c->targets[i].kind == DECL_ZERO && c->targets[i].type == type) return c->targets[i].id;
    TargetDeclaration *d = append_declaration(e, DECL_ZERO, type);
    return d == NULL ? 0u : d->id;
}
static bool plan_workgroup_barrier(Emitter *e) {
    (void)use_constant(e, ID_TWO_U32);
    if (e->barrier_semantics == 0u) e->barrier_semantics = reserve_ids(e, 1u);
    return !e->failed;
}
static bool plan_type(Emitter *e, TypeId type) {
    CompilerWorkspace *c = e->program;
    if (type < 7u) return scalar_type(type) != 0u;
    if (c->types[type - 7u].target != 0u) return true;
    if (!push_walk(e, WALK_TYPE, type)) return false;
    c->types[type - 7u].target = NO_ID;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        TypeRecord *t = &c->types[w->object - 7u];
        const uint32_t children = t->kind == TYPE_STRUCT ? t->count : 1u;
        if (w->cursor < children) {
            const TypeId child = type_child(c, w->object, w->cursor++);
            if (child < 7u) {
                if (scalar_type(child) == 0u) return emit_error(e, t->name.offset, "abstract target type dependency");
            } else if (c->types[child - 7u].target == NO_ID) {
                return emit_error(e, t->name.offset, "cyclic target type dependency");
            } else if (c->types[child - 7u].target == 0u) {
                if (!push_walk(e, WALK_TYPE, child)) return false;
                c->types[child - 7u].target = NO_ID;
            }
            continue;
        }
        if (t->kind == TYPE_STRUCT) {
            if (t->count > UINT16_MAX - 2u)
                return source_error(e->source, DOE_COMPILER_UNSUPPORTED, t->name.offset,
                                    "SPIR-V structure instruction word-count format capacity exceeded");
            for (uint32_t i = 0u; i < t->count; ++i)
                if (c->members[t->element + i].offset > UINT32_MAX)
                    return source_error(e->source, DOE_COMPILER_UNSUPPORTED,
                        c->members[t->element + i].name.offset, "SPIR-V member Offset format capacity exceeded");
        } else if ((t->kind == TYPE_ARRAY || t->kind == TYPE_RUNTIME_ARRAY) && t->state.layout.stride > UINT32_MAX) {
            return source_error(e->source, DOE_COMPILER_UNSUPPORTED, t->name.offset,
                                "SPIR-V ArrayStride format capacity exceeded");
        }
        if (t->kind == TYPE_VECTOR && t->element == IR_U32 && t->count == 3u) t->target = ID_VEC3;
        else {
            const uint32_t length = t->kind == TYPE_ARRAY ? unsigned_constant(e, t->count) : 0u;
            TargetDeclaration *d = append_declaration(e, DECL_TYPE, w->object);
            if (d == NULL) return false;
            d->first = length; t->target = d->id;
        }
        --c->walk_count;
    }
    return !e->failed;
}
static uint32_t leaf_constant(Emitter *e, ConstantValue value) {
    if (value.type == IR_BOOL) return use_constant(e, value.bits == 0u ? ID_FALSE : ID_TRUE);
    if (value.bits == 0u) return zero_constant(e, value.type);
    if (value.type == IR_U32) return unsigned_constant(e, (uint32_t)value.bits);
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i) {
        const TargetDeclaration *d = &c->targets[i];
        if (d->kind == DECL_LEAF && d->type == value.type && d->payload == value.bits) return d->id;
    }
    TargetDeclaration *d = append_declaration(e, DECL_LEAF, value.type);
    if (d == NULL) return 0u;
    d->payload = value.bits;
    return d->id;
}
static bool plan_constant(Emitter *e, uint32_t object) {
    CompilerWorkspace *c = e->program;
    if (object >= c->constant_count) return emit_error(e, 0u, "invalid aggregate constant identity");
    if (c->constants[object].target != 0u) return true;
    if (!push_walk(e, WALK_CONSTANT, object)) return false;
    c->constants[object].target = NO_ID;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        ConstantRecord *k = &c->constants[w->object];
        if (target_type(e, k->type) == 0u || type_abstract(c, k->type))
            return emit_error(e, 0u, "aggregate target constant must have concrete type");
        const uint32_t children = k->kind == CONSTANT_ZERO ? 0u : k->kind == CONSTANT_REPEAT ? 1u : k->count;
        if (k->kind != CONSTANT_ZERO && k->count > UINT16_MAX - 3u)
            return source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->source->length,
                                "SPIR-V constant instruction word-count format capacity exceeded");
        if (w->cursor < children) {
            const ConstantValue v = c->constant_edges[k->first + w->cursor++];
            if ((v.flags & CONSTANT_AGGREGATE) == 0u) {
                if (leaf_constant(e, v) == 0u) return false;
            } else {
                const uint32_t child = (uint32_t)v.bits;
                if (child >= c->constant_count) return emit_error(e, 0u, "invalid constant child identity");
                if (c->constants[child].target == NO_ID) return emit_error(e, 0u, "cyclic target constant dependency");
                if (c->constants[child].target == 0u) {
                    if (!push_walk(e, WALK_CONSTANT, child)) return false;
                    c->constants[child].target = NO_ID;
                }
            }
            continue;
        }
        if (k->kind == CONSTANT_ZERO) k->target = zero_constant(e, k->type);
        else {
            TargetDeclaration *d = append_declaration(e, DECL_COMPOSITE, k->type);
            if (d == NULL) return false;
            d->first = w->object; k->target = d->id;
        }
        --c->walk_count;
    }
    return !e->failed;
}
static bool wrapped_resource(const CompilerWorkspace *c, const ResourceRecord *r) {
    return !(r->type >= 7u && c->types[r->type - 7u].kind == TYPE_STRUCT &&
             (c->types[r->type - 7u].flags & TYPE_TAIL) != 0u);
}
static uint32_t resource_storage(const ResourceRecord *r) {
    return r->kind == DOE_COMPILER_UNIFORM_BUFFER ? 2u : 12u;
}
static bool same_signature(const IrProgram *program, const FunctionRecord *a, const FunctionRecord *b) {
    if (a->result_type != b->result_type || a->formal_count != b->formal_count) return false;
    for (uint32_t i = 0u; i < a->formal_count; ++i)
        if (program->formals[a->formal_start + i].type.resolved.type !=
            program->formals[b->formal_start + i].type.resolved.type) return false;
    return true;
}
static void plan_scalar_guards(Emitter *e, const IrInstruction *ir) {
    const TypeId to = type_leaf(e->program, ir->type);
    if (ir->opcode == IR_DIVIDE || ir->opcode == IR_REMAINDER) {
        if (to == IR_I32 || to == IR_U32) {
            (void)zero_constant(e, to);
            (void)use_constant(e, to == IR_I32 ? ID_ONE_I32 : ID_ONE_U32);
            if (to == IR_I32) {
                (void)use_constant(e, ID_MIN_I32);
                (void)use_constant(e, ID_NEG_ONE_I32);
            }
        }
    } else if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT) {
        (void)use_constant(e, ID_SHIFT_MASK);
    } else if (ir->opcode == IR_CONVERT) {
        const TypeId from = type_leaf(e->program, ir->operands.scalar.binding);
        if (from == to) return;
        if (to == IR_BOOL) (void)zero_constant(e, from);
        else if (from == IR_BOOL) {
            (void)zero_constant(e, to);
            (void)use_constant(e, to == IR_I32 ? ID_ONE_I32 : to == IR_F32 ? ID_ONE_F32 : ID_ONE_U32);
        } else if (from == IR_F32) {
            (void)use_constant(e, ID_ZERO_F32);
            if (to == IR_I32) (void)use_constant(e, ID_F32_I32_MIN);
            (void)use_constant(e, to == IR_I32 ? ID_F32_I32_MAX : ID_F32_U32_MAX);
        }
    }
}
static bool plan_module(Emitter *e) {
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->type_count; ++i) { c->types[i].target = 0u; c->types[i].aux = 0u; }
    for (uint32_t i = 0u; i < c->constant_count; ++i) c->constants[i].target = 0u;
    for (uint32_t i = 0u; i < c->type_count; ++i)
        if (!type_abstract(c, i + 7u) && !plan_type(e, i + 7u)) return false;
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        FunctionRecord *function = &c->functions[f];
        function->state.target.function = reserve_ids(e, 1u);
        function->state.target.label = reserve_ids(e, 1u);
        function->state.target.signature = 0u;
        if (function->result_type == IR_VOID && function->formal_count == 0u) function->state.target.signature = ID_FN_TYPE;
        else {
            if (function->formal_count > UINT16_MAX - 3u)
                return source_error(e->source, DOE_COMPILER_UNSUPPORTED, function->name.offset,
                                    "SPIR-V function instruction word-count format capacity exceeded");
            for (uint32_t p = 0u; p < f; ++p)
                if (same_signature(c, function, &c->functions[p])) {
                    function->state.target.signature = c->functions[p].state.target.signature; break;
                }
            if (function->state.target.signature == 0u) function->state.target.signature = reserve_ids(e, 1u);
        }
    }
    for (uint32_t i = 0u; i < c->parameter_count; ++i) e->inputs[i] = reserve_ids(e, 1u);
    if (c->workgroup_count != 0u) {
        for (uint32_t i = 0u; i < c->parameter_count; ++i)
            if (c->parameters[i].kind == INPUT_LOCAL_INDEX) e->workgroup_input = e->inputs[i];
        if (e->workgroup_input == 0u) {
            if (c->parameter_count >= MAX_ENTRY_PARAMETERS)
                return emit_error(e, c->functions[c->entry_function].name.offset,
                                  "workgroup initialization input exceeds target interface capacity");
            e->workgroup_input = reserve_ids(e, 1u);
            e->hidden_workgroup_input = true;
        }
        (void)use_constant(e, ID_ZERO_U32);
        if (!plan_workgroup_barrier(e)) return false;
    }
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        ResourceRecord *r = &c->resources[i];
        if (wrapped_resource(c, r)) {
            (void)use_constant(e, ID_ZERO_U32);
            TargetDeclaration *wrapper = append_declaration(e, DECL_WRAPPER, r->type);
            if (wrapper == NULL) return false;
            const uint32_t wrapper_id = wrapper->id;
            TargetDeclaration *pointer = append_declaration(e, DECL_POINTER, NO_ID);
            if (pointer == NULL) return false;
            pointer->storage = resource_storage(r); pointer->payload = wrapper_id;
            r->reserved = pointer->id;
        } else r->reserved = pointer_type(e, r->type, resource_storage(r));
        r->target = reserve_ids(e, 1u);
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        WorkgroupRoot *root = &c->workgroups[i];
        if (pointer_type(e, root->type, 4u) == 0u || workgroup_zero(e, root->type) == 0u) return false;
        root->target = reserve_ids(e, 1u);
    }
    for (uint32_t i = 0u; i < c->local_count; ++i)
        if (pointer_type(e, c->locals[i].type, 7u) == 0u) return false;
    for (uint32_t i = 0u; i < c->access_count; ++i) {
        const AccessStep *a = &c->accesses[i];
        if (a->kind == ACCESS_INDEX_I32) (void)use_constant(e, ID_ZERO_I32);
        if (a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT) {
            if (unsigned_constant(e, a->index) == 0u) return false;
        } else if (a->input_type >= 7u && c->types[a->input_type - 7u].kind != TYPE_RUNTIME_ARRAY) {
            if (unsigned_constant(e, c->types[a->input_type - 7u].count) == 0u) return false;
        }
    }
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        const IrInstruction *ir = &c->instructions[i];
        plan_scalar_guards(e, ir);
        if (ir->opcode == IR_CONSTANT) {
            if (ir->operands.scalar.left != 0u) {
                if (!plan_constant(e, ir->operands.scalar.binding)) return false;
                e->values[ir->result] = c->constants[ir->operands.scalar.binding].target;
            } else e->values[ir->result] = reserve_ids(e, 1u);
        } else if (ir->opcode == IR_WORKGROUP_BARRIER) {
            if (!plan_workgroup_barrier(e)) return false;
        } else if (ir->opcode == IR_BUILTIN) {
            const NumericBuiltin *builtin = builtin_kind((NumericBuiltinKind)ir->operands.scalar.literal);
            if (builtin == NULL) return emit_error(e, ir->source_offset, "invalid numeric builtin identity");
            if (builtin->extended_instruction != 0u && e->math_import == 0u)
                e->math_import = reserve_ids(e, 1u);
        } else if (ir->opcode == IR_ACCESS_LOAD || ir->opcode == IR_ACCESS_STORE || ir->opcode == IR_VALUE_ACCESS) {
            const uint32_t category = ir->operands.access.category;
            const uint32_t storage = category == ROOT_RESOURCE ? resource_storage(&c->resources[ir->operands.access.root]) :
                category == ROOT_WORKGROUP ? 4u : 7u;
            if (ir->opcode != IR_VALUE_ACCESS || ir->operands.access.value != NO_ID)
                if (pointer_type(e, ir->type, storage) == 0u) return false;
            if (ir->opcode != IR_ACCESS_STORE && zero_constant(e, ir->type) == 0u) return false;
        }
    }
    return !e->failed;
}
static uint32_t builtin_decoration(InputKind kind) {
    switch (kind) {
        case INPUT_NUM_WORKGROUPS: return 24u;
        case INPUT_WORKGROUP: return 26u;
        case INPUT_LOCAL_INVOCATION: return 27u;
        case INPUT_GLOBAL_INVOCATION: return 28u;
        case INPUT_LOCAL_INDEX: return 29u;
    }
    return 0u;
}
static bool annotations(Emitter *e) {
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i) {
        const TargetDeclaration *d = &c->targets[i];
        if (d->kind == DECL_WRAPPER) {
            EMIT(OP_DECORATE, d->id, 2u);
            EMIT(OP_MEMBER_DECORATE, d->id, 0u, 35u, 0u);
        } else if (d->kind == DECL_TYPE) {
            const TypeRecord *t = &c->types[d->type - 7u];
            if (t->kind == TYPE_ARRAY || t->kind == TYPE_RUNTIME_ARRAY) {
                EMIT(OP_DECORATE, d->id, 6u, (uint32_t)t->state.layout.stride);
            } else if (t->kind == TYPE_STRUCT) {
                if ((t->flags & TYPE_TAIL) != 0u) { EMIT(OP_DECORATE, d->id, 2u); }
                for (uint32_t m = 0u; m < t->count; ++m)
                    EMIT(OP_MEMBER_DECORATE, d->id, m, 35u, (uint32_t)c->members[t->element + m].offset);
            }
        }
    }
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const ResourceRecord *r = &c->resources[i];
        EMIT(OP_DECORATE, r->target, 34u, r->group);
        EMIT(OP_DECORATE, r->target, 33u, r->binding);
        if (r->access == DOE_COMPILER_READ) { EMIT(OP_DECORATE, r->target, 24u); }
    }
    for (uint32_t i = 0u; i < c->parameter_count; ++i)
        EMIT(OP_DECORATE, e->inputs[i], 11u, builtin_decoration(c->parameters[i].kind));
    if (e->hidden_workgroup_input) { EMIT(OP_DECORATE, e->workgroup_input, 11u, builtin_decoration(INPUT_LOCAL_INDEX)); }
    return true;
}
static bool primitive_declarations(Emitter *e) {
    EMIT(OP_TYPE_VOID, ID_VOID);
    EMIT(OP_TYPE_BOOL, ID_BOOL);
    EMIT(OP_TYPE_INT, ID_U32, 32u, 0u);
    EMIT(OP_TYPE_INT, ID_I32, 32u, 1u);
    EMIT(OP_TYPE_FLOAT, ID_F32, 32u);
    EMIT(OP_TYPE_VECTOR, ID_VEC3, ID_U32, 3u);
    EMIT(OP_TYPE_POINTER, ID_PTR_INPUT, 1u, ID_VEC3);
    EMIT(OP_TYPE_POINTER, ID_PTR_INPUT_U32, 1u, ID_U32);
    EMIT(OP_TYPE_FUNCTION, ID_FN_TYPE, ID_VOID);
    for (uint32_t slot = 0u; slot < 4u; ++slot)
        for (TypeId type = IR_BOOL; type <= IR_F32; ++type)
            if (e->local_pointers[slot][type] != 0u)
                EMIT(OP_TYPE_POINTER, e->local_pointers[slot][type], slot == 0u ? 7u : slot == 1u ? 2u : slot == 2u ? 12u : 4u,
                     scalar_type(type));
    if ((e->constants_used & (UINT32_C(1) << ID_FALSE)) != 0u) { EMIT(OP_CONSTANT_FALSE, ID_BOOL, ID_FALSE); }
    if ((e->constants_used & (UINT32_C(1) << ID_TRUE)) != 0u) { EMIT(OP_CONSTANT_TRUE, ID_BOOL, ID_TRUE); }
    if ((e->constants_used & (UINT32_C(1) << ID_ZERO_U32)) != 0u) { EMIT(OP_CONSTANT, ID_U32, ID_ZERO_U32, 0u); }
    if ((e->constants_used & (UINT32_C(1) << ID_ZERO_F32)) != 0u) { EMIT(OP_CONSTANT, ID_F32, ID_ZERO_F32, 0u); }
    if ((e->constants_used & (UINT32_C(1) << ID_ZERO_I32)) != 0u) { EMIT(OP_CONSTANT, ID_I32, ID_ZERO_I32, 0u); }
    if ((e->constants_used & (UINT32_C(1) << ID_ONE_U32)) != 0u) { EMIT(OP_CONSTANT, ID_U32, ID_ONE_U32, 1u); }
    if ((e->constants_used & (UINT32_C(1) << ID_ONE_I32)) != 0u) { EMIT(OP_CONSTANT, ID_I32, ID_ONE_I32, 1u); }
    if ((e->constants_used & (UINT32_C(1) << ID_ONE_F32)) != 0u) { EMIT(OP_CONSTANT, ID_F32, ID_ONE_F32, UINT32_C(0x3f800000)); }
    if ((e->constants_used & (UINT32_C(1) << ID_MIN_I32)) != 0u) { EMIT(OP_CONSTANT, ID_I32, ID_MIN_I32, UINT32_C(0x80000000)); }
    if ((e->constants_used & (UINT32_C(1) << ID_NEG_ONE_I32)) != 0u) { EMIT(OP_CONSTANT, ID_I32, ID_NEG_ONE_I32, UINT32_MAX); }
    if ((e->constants_used & (UINT32_C(1) << ID_SHIFT_MASK)) != 0u) { EMIT(OP_CONSTANT, ID_U32, ID_SHIFT_MASK, 31u); }
    if ((e->constants_used & (UINT32_C(1) << ID_F32_U32_MAX)) != 0u) { EMIT(OP_CONSTANT, ID_F32, ID_F32_U32_MAX, UINT32_C(0x4f7fffff)); }
    if ((e->constants_used & (UINT32_C(1) << ID_F32_I32_MAX)) != 0u) { EMIT(OP_CONSTANT, ID_F32, ID_F32_I32_MAX, UINT32_C(0x4effffff)); }
    if ((e->constants_used & (UINT32_C(1) << ID_F32_I32_MIN)) != 0u) { EMIT(OP_CONSTANT, ID_F32, ID_F32_I32_MIN, UINT32_C(0xcf000000)); }
    if ((e->constants_used & (UINT32_C(1) << ID_TWO_U32)) != 0u) { EMIT(OP_CONSTANT, ID_U32, ID_TWO_U32, 2u); }
    if ((e->constants_used & (UINT32_C(1) << ID_THREE_U32)) != 0u) { EMIT(OP_CONSTANT, ID_U32, ID_THREE_U32, 3u); }
    if ((e->constants_used & (UINT32_C(1) << ID_FOUR_U32)) != 0u) { EMIT(OP_CONSTANT, ID_U32, ID_FOUR_U32, 4u); }
    if (e->barrier_semantics != 0u) { EMIT(OP_CONSTANT, ID_U32, e->barrier_semantics, UINT32_C(0x108)); }
    return true;
}
static bool dynamic_declarations(Emitter *e) {
    CompilerWorkspace *c = e->program;
    const uint32_t planned_count = c->target_count;
    for (uint32_t i = 0u; i < planned_count; ++i) {
        const TargetDeclaration *d = &c->targets[i];
        switch (d->kind) {
            case DECL_TYPE: {
                const TypeRecord *t = &c->types[d->type - 7u];
                if (t->kind == TYPE_VECTOR) { EMIT(OP_TYPE_VECTOR, d->id, target_type(e, t->element), t->count); }
                else if (t->kind == TYPE_ARRAY) { EMIT(OP_TYPE_ARRAY, d->id, target_type(e, t->element), d->first); }
                else if (t->kind == TYPE_RUNTIME_ARRAY) { EMIT(OP_TYPE_RUNTIME_ARRAY, d->id, target_type(e, t->element)); }
                else {
                    uint32_t *words = NULL;
                    if (!instruction_space(e, OP_TYPE_STRUCT, (size_t)t->count + 1u, &words)) return false;
                    words[0] = d->id;
                    for (uint32_t m = 0u; m < t->count; ++m) words[m + 1u] = target_type(e, c->members[t->element + m].type);
                }
                break;
            }
            case DECL_POINTER:
                EMIT(OP_TYPE_POINTER, d->id, d->storage, (uint32_t)d->payload);
                break;
            case DECL_WRAPPER:
                EMIT(OP_TYPE_STRUCT, d->id, target_type(e, d->type));
                break;
            case DECL_U32:
            case DECL_LEAF:
                EMIT(OP_CONSTANT, target_type(e, d->type), d->id, (uint32_t)d->payload);
                break;
            case DECL_ZERO:
                EMIT(OP_CONSTANT_NULL, target_type(e, d->type), d->id);
                break;
            case DECL_COMPOSITE: {
                const ConstantRecord *k = &c->constants[d->first];
                uint32_t *words = NULL;
                if (!instruction_space(e, OP_CONSTANT_COMPOSITE, (size_t)k->count + 2u, &words)) return false;
                words[0] = target_type(e, k->type); words[1] = d->id;
                for (uint32_t j = 0u; j < k->count; ++j) {
                    const ConstantValue v = c->constant_edges[k->first + (k->kind == CONSTANT_REPEAT ? 0u : j)];
                    words[j + 2u] = (v.flags & CONSTANT_AGGREGATE) != 0u ? c->constants[(uint32_t)v.bits].target : leaf_constant(e, v);
                }
                break;
            }
            default: return emit_error(e, 0u, "invalid target declaration kind");
        }
    }
    if (c->target_count != planned_count) return emit_error(e, 0u, "target declaration plan changed during serialization");
    return !e->failed;
}
static bool module_declarations(Emitter *e) {
    CompilerWorkspace *c = e->program;
    const FunctionRecord *entry = &c->functions[c->entry_function];
    EMIT(OP_CAPABILITY, 1u);
    if (e->math_import != 0u) {
        const uint32_t imported[] = { e->math_import, UINT32_C(0x4c534c47), UINT32_C(0x6474732e), UINT32_C(0x3035342e), 0u };
        if (!instruction(e, OP_EXT_INST_IMPORT, imported, 5u)) return false;
    }
    EMIT(OP_MEMORY_MODEL, 0u, 1u);
    uint32_t entry_arguments[23] = { 5u, entry->state.target.function };
    const size_t entry_words = span_words(e->source, entry->name, entry_arguments + 2u);
    for (uint32_t i = 0u; i < c->parameter_count; ++i) entry_arguments[entry_words + 2u + i] = e->inputs[i];
    if (e->hidden_workgroup_input) entry_arguments[entry_words + 2u + c->parameter_count] = e->workgroup_input;
    if (!instruction(e, OP_ENTRY_POINT, entry_arguments,
        entry_words + 2u + c->parameter_count + (e->hidden_workgroup_input ? 1u : 0u))) return false;
    EMIT(OP_EXECUTION_MODE, entry->state.target.function, 17u,
         c->local_size[0].value, c->local_size[1].value, c->local_size[2].value);
    if (!debug_name(e, entry->state.target.function, entry->name)) return false;
    for (uint32_t i = 0u; i < c->parameter_count; ++i)
        if (!debug_name(e, e->inputs[i], c->parameters[i].name)) return false;
    for (uint32_t i = 0u; i < c->resource_count; ++i)
        if (!debug_name(e, c->resources[i].target, c->resources[i].name)) return false;
    for (uint32_t i = 0u; i < c->workgroup_count; ++i)
        if (!debug_name(e, c->workgroups[i].target, c->workgroups[i].name)) return false;
    if (!annotations(e) || !primitive_declarations(e) || !dynamic_declarations(e)) return false;
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        const FunctionRecord *fn = &c->functions[f];
        bool emitted = fn->state.target.signature == ID_FN_TYPE;
        for (uint32_t p = 0u; p < f && !emitted; ++p)
            emitted = c->functions[p].state.target.signature == fn->state.target.signature;
        if (emitted) continue;
        uint32_t *words = NULL;
        if (!instruction_space(e, OP_TYPE_FUNCTION, (size_t)fn->formal_count + 2u, &words)) return false;
        words[0] = fn->state.target.signature; words[1] = target_type(e, fn->result_type);
        for (uint32_t p = 0u; p < fn->formal_count; ++p)
            words[p + 2u] = target_type(e, c->formals[fn->formal_start + p].type.resolved.type);
    }
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        const IrInstruction *ir = &c->instructions[i];
        if (ir->opcode != IR_CONSTANT || ir->operands.scalar.left != 0u) continue;
        if (ir->type == IR_BOOL) {
            EMIT(ir->operands.scalar.literal != 0u ? OP_CONSTANT_TRUE : OP_CONSTANT_FALSE, ID_BOOL, e->values[ir->result]);
        } else { EMIT(OP_CONSTANT, target_type(e, ir->type), e->values[ir->result], ir->operands.scalar.literal); }
    }
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const ResourceRecord *r = &c->resources[i];
        EMIT(OP_VARIABLE, r->reserved, r->target, resource_storage(r));
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        const WorkgroupRoot *root = &c->workgroups[i];
        EMIT(OP_VARIABLE, pointer_type(e, root->type, 4u), root->target, 4u);
    }
    for (uint32_t i = 0u; i < c->parameter_count; ++i)
        EMIT(OP_VARIABLE, c->parameters[i].kind == INPUT_LOCAL_INDEX ? ID_PTR_INPUT_U32 : ID_PTR_INPUT, e->inputs[i], 1u);
    if (e->hidden_workgroup_input) { EMIT(OP_VARIABLE, ID_PTR_INPUT_U32, e->workgroup_input, 1u); }
    return true;
}
static uint32_t comparison_opcode(Comparison comparison, TypeId type) {
    if (type == IR_BOOL)
        return comparison == COMPARE_EQUAL ? OP_LOGICAL_EQUAL : OP_LOGICAL_NOT_EQUAL;
    if (type == IR_F32) {
        switch (comparison) {
            case COMPARE_EQUAL: return OP_FORD_EQUAL;
            case COMPARE_NOT_EQUAL: return OP_FUNORD_NOT_EQUAL;
            case COMPARE_LESS: return OP_FORD_LESS_THAN;
            case COMPARE_LESS_EQUAL: return OP_FORD_LESS_EQUAL;
            case COMPARE_GREATER: return OP_FORD_GREATER_THAN;
            case COMPARE_GREATER_EQUAL: return OP_FORD_GREATER_EQUAL;
        }
    }
    switch (comparison) {
        case COMPARE_EQUAL: return OP_IEQUAL;
        case COMPARE_NOT_EQUAL: return OP_INOT_EQUAL;
        case COMPARE_LESS: return type == IR_I32 ? OP_SLESS_THAN : OP_ULESS_THAN;
        case COMPARE_LESS_EQUAL: return type == IR_I32 ? OP_SLESS_EQUAL : OP_ULESS_EQUAL;
        case COMPARE_GREATER: return type == IR_I32 ? OP_SGREATER_THAN : OP_UGREATER_THAN;
        case COMPARE_GREATER_EQUAL: return type == IR_I32 ? OP_SGREATER_EQUAL : OP_UGREATER_EQUAL;
    }
    return 0u;
}
/* At most nine target identities including the result. Invalid integer inputs
 * select a safe denominator before division, never after an invalid operation. */
static bool emit_division(Emitter *e, TypeId value_type, IrOpcode operation,
                          uint32_t result, uint32_t left, uint32_t right) {
    const bool remainder = operation == IR_REMAINDER;
    if (value_type == IR_F32) {
        EMIT(remainder ? OP_FREM : OP_FDIV, ID_F32, result, left, right);
        return true;
    }
    const bool is_signed = value_type == IR_I32;
    const uint32_t type = scalar_type(value_type), zero = is_signed ? ID_ZERO_I32 : ID_ZERO_U32;
    uint32_t invalid = reserve_ids(e, 1u);
    EMIT(OP_IEQUAL, ID_BOOL, invalid, right, zero);
    if (is_signed) {
        const uint32_t minimum = reserve_ids(e, 1u), negative_one = reserve_ids(e, 1u);
        const uint32_t overflow = reserve_ids(e, 1u), combined = reserve_ids(e, 1u);
        EMIT(OP_IEQUAL, ID_BOOL, minimum, left, ID_MIN_I32);
        EMIT(OP_IEQUAL, ID_BOOL, negative_one, right, ID_NEG_ONE_I32);
        EMIT(OP_LOGICAL_AND, ID_BOOL, overflow, minimum, negative_one);
        EMIT(OP_LOGICAL_OR, ID_BOOL, combined, invalid, overflow);
        invalid = combined;
    }
    const uint32_t safe = reserve_ids(e, 1u), quotient = reserve_ids(e, 1u);
    EMIT(OP_SELECT, type, safe, invalid, is_signed ? ID_ONE_I32 : ID_ONE_U32, right);
    EMIT(remainder ? (is_signed ? OP_SREM : OP_UMOD) : (is_signed ? OP_SDIV : OP_UDIV), type, quotient, left, safe);
    EMIT(OP_SELECT, type, result, invalid, remainder ? zero : left, quotient);
    return true;
}
/* Seven target identities including result. Ordered clamps and explicit NaN
 * normalization ensure OpConvertFTo[US] receives only in-range finite input. */
static bool emit_conversion(Emitter *e, TypeId from, TypeId to, uint32_t result, uint32_t input) {
    if (from == to) { EMIT(OP_COPY_OBJECT, scalar_type(to), result, input); return true; }
    if (to == IR_BOOL) {
        EMIT(from == IR_F32 ? OP_FUNORD_NOT_EQUAL : OP_INOT_EQUAL, ID_BOOL, result, input,
             from == IR_F32 ? ID_ZERO_F32 : from == IR_I32 ? ID_ZERO_I32 : ID_ZERO_U32);
    } else if (from == IR_BOOL) {
        EMIT(OP_SELECT, scalar_type(to), result, input,
             to == IR_F32 ? ID_ONE_F32 : to == IR_I32 ? ID_ONE_I32 : ID_ONE_U32,
             to == IR_F32 ? ID_ZERO_F32 : to == IR_I32 ? ID_ZERO_I32 : ID_ZERO_U32);
    } else if (from == IR_F32) {
        const uint32_t nan = reserve_ids(e, 1u), finite = reserve_ids(e, 1u);
        const uint32_t low = reserve_ids(e, 1u), lower = reserve_ids(e, 1u);
        const uint32_t high = reserve_ids(e, 1u), clamped = reserve_ids(e, 1u);
        const uint32_t minimum = to == IR_I32 ? ID_F32_I32_MIN : ID_ZERO_F32;
        const uint32_t maximum = to == IR_I32 ? ID_F32_I32_MAX : ID_F32_U32_MAX;
        EMIT(OP_IS_NAN, ID_BOOL, nan, input);
        EMIT(OP_SELECT, ID_F32, finite, nan, ID_ZERO_F32, input);
        EMIT(OP_FORD_LESS_THAN, ID_BOOL, low, finite, minimum);
        EMIT(OP_SELECT, ID_F32, lower, low, minimum, finite);
        EMIT(OP_FORD_GREATER_THAN, ID_BOOL, high, lower, maximum);
        EMIT(OP_SELECT, ID_F32, clamped, high, maximum, lower);
        EMIT(to == IR_I32 ? OP_CONVERT_F_TO_S : OP_CONVERT_F_TO_U, scalar_type(to), result, clamped);
    } else if (to == IR_F32) {
        EMIT(from == IR_I32 ? OP_CONVERT_S_TO_F : OP_CONVERT_U_TO_F, ID_F32, result, input);
    } else { EMIT(OP_BITCAST, scalar_type(to), result, input); }
    return true;
}
static TypeId semantic_type(const CompilerWorkspace *c, uint32_t id) {
    if (id > c->limits.instructions) {
        const uint32_t formal = id - c->limits.instructions - 1u;
        return formal < c->formal_count ? c->formals[formal].type.resolved.type : IR_VOID;
    }
    return id != 0u && id <= c->instruction_count ? c->instructions[id - 1u].type : IR_VOID;
}
static bool extract_lane(Emitter *e, TypeId type, uint32_t input, uint32_t lane, uint32_t *result) {
    if (type_vector_width(e->program, type) == 1u) { *result = input; return true; }
    *result = reserve_ids(e, 1u);
    EMIT(OP_COMPOSITE_EXTRACT, scalar_type(type_leaf(e->program, type)), *result, input, lane);
    return true;
}
static bool vector_construct(Emitter *e, TypeId type, uint32_t result, const uint32_t lanes[4], uint32_t width) {
    uint32_t *words = NULL;
    if (!instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)width + 2u, &words)) return false;
    words[0] = target_type(e, type); words[1] = result;
    for (uint32_t i = 0u; i < width; ++i) words[i + 2u] = lanes[i];
    return true;
}
static bool emit_component_builtin(Emitter *e, NumericBuiltinKind kind, TypeId leaf,
                                   uint32_t result, uint32_t left, uint32_t right, uint32_t upper) {
    const uint32_t condition = reserve_ids(e, 1u);
    EMIT(comparison_opcode(kind == BUILTIN_MIN ? COMPARE_LESS : COMPARE_GREATER, leaf),
         ID_BOOL, condition, left, right);
    const uint32_t intermediate = kind == BUILTIN_CLAMP ? reserve_ids(e, 1u) : result;
    EMIT(OP_SELECT, scalar_type(leaf), intermediate, condition, left, right);
    if (kind == BUILTIN_CLAMP) {
        /* Clamp with reversed runtime bounds is not GLSL Clamp: avoid poison
         * by emitting the specified min(max(value, lower), upper) composition. */
        const uint32_t below = reserve_ids(e, 1u);
        EMIT(comparison_opcode(COMPARE_LESS, leaf), ID_BOOL, below, intermediate, upper);
        EMIT(OP_SELECT, scalar_type(leaf), result, below, intermediate, upper);
    }
    return true;
}

static bool emit_integer_dot(Emitter *e, TypeId operand_type, uint32_t result,
                             uint32_t left, uint32_t right) {
    const uint32_t width = type_vector_width(e->program, operand_type);
    const uint32_t products = reserve_ids(e, 1u);
    const uint32_t leaf = scalar_type(type_leaf(e->program, operand_type));
    EMIT(OP_IMUL, target_type(e, operand_type), products, left, right);
    uint32_t partial = 0u;
    for (uint32_t i = 0u; i < width; ++i) {
        uint32_t lane;
        if (!extract_lane(e, operand_type, products, i, &lane)) return false;
        if (i == 0u) partial = lane;
        else {
            const uint32_t sum = i + 1u == width ? result : reserve_ids(e, 1u);
            EMIT(OP_IADD, leaf, sum, partial, lane);
            partial = sum;
        }
    }
    return true;
}

static bool emit_numeric_builtin(Emitter *e, const IrInstruction *ir) {
    const NumericBuiltin *builtin = builtin_kind((NumericBuiltinKind)ir->operands.scalar.literal);
    if (builtin == NULL) return emit_error(e, ir->source_offset, "invalid numeric builtin identity");
    const uint32_t result = e->values[ir->result];
    const uint32_t left = e->values[ir->operands.scalar.left];
    const uint32_t right = builtin->arity > 1u ? e->values[ir->operands.scalar.right] : 0u;
    const TypeId operand_type = semantic_type(e->program, ir->operands.scalar.left);
    const TypeId leaf = type_leaf(e->program, operand_type);
    if (builtin->extended_instruction != 0u) {
        if (builtin->arity == 1u) {
            EMIT(OP_EXT_INST, target_type(e, ir->type), result, e->math_import,
                 builtin->extended_instruction, left);
        } else {
            EMIT(OP_EXT_INST, target_type(e, ir->type), result, e->math_import,
                 builtin->extended_instruction, left, right);
        }
        return true;
    }
    if (builtin->kind == BUILTIN_DOT) {
        if (leaf == IR_F32) {
            EMIT(OP_DOT, target_type(e, ir->type), result, left, right);
            return true;
        }
        return emit_integer_dot(e, operand_type, result, left, right);
    }
    const uint32_t upper = builtin->arity > 2u ? e->values[ir->operands.scalar.binding] : 0u;
    const uint32_t width = type_vector_width(e->program, operand_type);
    if (width == 1u)
        return emit_component_builtin(e, builtin->kind, leaf, result, left, right, upper);
    uint32_t lanes[4];
    for (uint32_t i = 0u; i < width; ++i) {
        uint32_t a, b, c = 0u;
        if (!extract_lane(e, operand_type, left, i, &a) ||
            !extract_lane(e, operand_type, right, i, &b)) return false;
        if (builtin->kind == BUILTIN_CLAMP && !extract_lane(e, operand_type, upper, i, &c)) return false;
        lanes[i] = reserve_ids(e, 1u);
        if (!emit_component_builtin(e, builtin->kind, leaf, lanes[i], a, b, c)) return false;
    }
    return vector_construct(e, ir->type, result, lanes, width);
}

static bool emit_shift(Emitter *e, TypeId type, IrOpcode opcode, uint32_t result, uint32_t left, uint32_t right) {
    const uint32_t masked = reserve_ids(e, 1u);
    EMIT(OP_BIT_AND, ID_U32, masked, right, ID_SHIFT_MASK);
    EMIT(opcode == IR_SHIFT_LEFT ? OP_SHIFT_LEFT : type == IR_I32 ? OP_SHIFT_RIGHT_ARITHMETIC : OP_SHIFT_RIGHT_LOGICAL,
         scalar_type(type), result, left, masked);
    return true;
}
static bool emit_safe_operation(Emitter *e, const IrInstruction *ir) {
    const uint32_t result = e->values[ir->result];
    const uint32_t left = e->values[ir->operands.scalar.left];
    const uint32_t right = e->values[ir->operands.scalar.right];
    const TypeId leaf = type_leaf(e->program, ir->type);
    const uint32_t width = type_vector_width(e->program, ir->type);
    if (width == 1u) {
        if (ir->opcode == IR_CONVERT) return emit_conversion(e, ir->operands.scalar.binding, ir->type, result, left);
        if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT)
            return emit_shift(e, ir->type, ir->opcode, result, left, right);
        return emit_division(e, ir->type, ir->opcode, result, left, right);
    }
    uint32_t lanes[4] = {0};
    const TypeId left_type = ir->operands.scalar.binding;
    const TypeId right_type = ir->operands.scalar.literal;
    for (uint32_t i = 0u; i < width; ++i) {
        uint32_t a, b;
        if (!extract_lane(e, left_type, left, i, &a)) return false;
        lanes[i] = reserve_ids(e, 1u);
        if (ir->opcode == IR_CONVERT) {
            if (!emit_conversion(e, type_leaf(e->program, left_type), leaf, lanes[i], a)) return false;
        } else {
            if (!extract_lane(e, right_type, right, i, &b)) return false;
            if (ir->opcode == IR_SHIFT_LEFT || ir->opcode == IR_SHIFT_RIGHT) {
                if (!emit_shift(e, leaf, ir->opcode, lanes[i], a, b)) return false;
            } else if (!emit_division(e, leaf, ir->opcode, lanes[i], a, b)) return false;
        }
    }
    return vector_construct(e, ir->type, result, lanes, width);
}
static bool emit_arithmetic(Emitter *e, const IrInstruction *ir) {
    const bool floating = type_leaf(e->program, ir->type) == IR_F32;
    const uint32_t opcode = ir->opcode == IR_SUBTRACT ? (floating ? OP_FSUB : OP_ISUB) :
        ir->opcode == IR_MULTIPLY ? (floating ? OP_FMUL : OP_IMUL) : floating ? OP_FADD : OP_IADD;
    uint32_t left = e->values[ir->operands.scalar.left], right = e->values[ir->operands.scalar.right];
    const uint32_t width = type_vector_width(e->program, ir->type);
    if (width > 1u) {
        const bool scalar_left = type_vector_width(e->program, ir->operands.scalar.binding) == 1u;
        const bool scalar_right = type_vector_width(e->program, ir->operands.scalar.literal) == 1u;
        if (scalar_left || scalar_right) {
            const uint32_t scalar = scalar_left ? left : right, splat = reserve_ids(e, 1u);
            const uint32_t lanes[4] = {scalar, scalar, scalar, scalar};
            if (!vector_construct(e, ir->type, splat, lanes, width)) return false;
            if (scalar_left) left = splat; else right = splat;
        }
    }
    EMIT(opcode, target_type(e, ir->type), e->values[ir->result], left, right);
    return true;
}
static bool emit_composite(Emitter *e, const IrInstruction *ir) {
    const CompilerWorkspace *c = e->program;
    const uint32_t first = ir->operands.composite.first, count = ir->operands.composite.count;
    if (first > c->operand_count || count > c->operand_count - first)
        return emit_error(e, ir->source_offset, "invalid typed constructor range");
    if (ir->operands.composite.kind == 0u) {
        uint32_t *words = NULL;
        if (!instruction_space(e, OP_COMPOSITE_CONSTRUCT, (size_t)count + 2u, &words)) return false;
        words[0] = target_type(e, ir->type); words[1] = e->values[ir->result];
        for (uint32_t i = 0u; i < count; ++i) words[i + 2u] = e->values[c->typed_operands[first + i]];
        return true;
    }
    const uint32_t width = type_vector_width(c, ir->type);
    uint32_t lanes[4] = {0}, used = 0u;
    if (ir->operands.composite.kind == 2u) {
        if (count != 1u) return emit_error(e, ir->source_offset, "invalid vector splat operand count");
        for (; used < width; ++used) lanes[used] = e->values[c->typed_operands[first]];
    } else {
        for (uint32_t i = 0u; i < count; ++i) {
            const uint32_t operand = c->typed_operands[first + i];
            const TypeId type = semantic_type(c, operand);
            const uint32_t n = type_vector_width(c, type);
            if (n > width - used) return emit_error(e, ir->source_offset, "invalid vector component count");
            for (uint32_t j = 0u; j < n; ++j)
                if (!extract_lane(e, type, e->values[operand], j, &lanes[used++])) return false;
        }
    }
    if (used != width) return emit_error(e, ir->source_offset, "incomplete vector components");
    return vector_construct(e, ir->type, e->values[ir->result], lanes, width);
}
static bool emit_swizzle(Emitter *e, const IrInstruction *ir) {
    const uint32_t width = ir->operands.scalar.right, input = e->values[ir->operands.scalar.left];
    if (width == 1u) {
        EMIT(OP_COMPOSITE_EXTRACT, target_type(e, ir->type), e->values[ir->result], input, ir->operands.scalar.literal & 3u);
    } else {
        uint32_t *words = NULL;
        if (!instruction_space(e, OP_VECTOR_SHUFFLE, (size_t)width + 4u, &words)) return false;
        words[0] = target_type(e, ir->type); words[1] = e->values[ir->result]; words[2] = input; words[3] = input;
        for (uint32_t i = 0u; i < width; ++i) words[i + 4u] = (ir->operands.scalar.literal >> (2u * i)) & 3u;
    }
    return true;
}
static bool emit_call(Emitter *e, const IrProgram *program, const IrInstruction *ir) {
    const uint32_t callee = ir->operands.scalar.binding, first = ir->operands.scalar.left, count = ir->operands.scalar.right;
    if (callee >= program->function_count || first > program->operand_count || count > program->operand_count - first)
        return emit_error(e, ir->source_offset, "invalid typed call range");
    uint32_t *words = NULL;
    if (!instruction_space(e, OP_FUNCTION_CALL, (size_t)count + 3u, &words)) return false;
    words[0] = target_type(e, ir->type);
    words[1] = ir->result == 0u ? reserve_ids(e, 1u) : e->values[ir->result];
    words[2] = program->functions[callee].state.target.function;
    for (uint32_t i = 0u; i < count; ++i) words[3u + i] = e->values[program->typed_operands[first + i]];
    return !e->failed;
}
/* Access traversal uses V, not a local array proportional to type/path depth.
 * The reverse chain is retained until its one guarded pointer/extract is emitted. */
static bool access_walk(Emitter *e, uint32_t path) {
    CompilerWorkspace *c = e->program;
    c->walk_count = 0u;
    while (path != NO_ID) {
        if (path >= c->access_count) return emit_error(e, 0u, "invalid typed access path");
        if (!push_walk(e, WALK_ACCESS, path)) return false;
        const uint32_t parent = c->accesses[path].parent;
        if (parent != NO_ID && parent >= path) return emit_error(e, c->accesses[path].source_offset, "invalid access parent order");
        path = parent;
    }
    return true;
}
static bool literal_index(const CompilerWorkspace *c, uint32_t semantic, uint32_t *value) {
    if (semantic == 0u || semantic > c->instruction_count) return false;
    const IrInstruction *ir = &c->instructions[semantic - 1u];
    if (ir->opcode != IR_CONSTANT || ir->operands.scalar.left != 0u || (ir->type != IR_U32 && ir->type != IR_I32)) return false;
    if (ir->type == IR_I32 && ir->operands.scalar.literal > INT32_MAX) return false;
    *value = ir->operands.scalar.literal;
    return true;
}
static bool access_condition(Emitter *e, const IrInstruction *ir, uint32_t *condition) {
    CompilerWorkspace *c = e->program;
    *condition = 0u;
    for (uint32_t i = c->walk_count; i != 0u; --i) {
        WalkItem *w = &c->walk[i - 1u];
        const AccessStep *a = &c->accesses[w->object];
        if (a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT) {
            w->cursor = unsigned_constant(e, a->index); continue;
        }
        const TypeRecord *input = &c->types[a->input_type - 7u];
        uint32_t length;
        if (input->kind == TYPE_RUNTIME_ARRAY) {
            if (ir->operands.access.category != ROOT_RESOURCE)
                return emit_error(e, a->source_offset, "runtime array access without resource root");
            const ResourceRecord *r = &c->resources[ir->operands.access.root];
            const uint32_t member = wrapped_resource(c, r) ? 0u : c->types[r->type - 7u].count - 1u;
            length = reserve_ids(e, 1u);
            EMIT(OP_ARRAY_LENGTH, ID_U32, length, r->target, member);
        } else length = unsigned_constant(e, input->count);
        uint32_t index = e->values[a->index], check;
        if (a->kind == ACCESS_INDEX_I32) {
            const uint32_t nonnegative = reserve_ids(e, 1u), converted = reserve_ids(e, 1u), upper = reserve_ids(e, 1u);
            check = reserve_ids(e, 1u);
            EMIT(OP_SGREATER_EQUAL, ID_BOOL, nonnegative, index, ID_ZERO_I32);
            EMIT(OP_BITCAST, ID_U32, converted, index);
            EMIT(OP_ULESS_THAN, ID_BOOL, upper, converted, length);
            EMIT(OP_LOGICAL_AND, ID_BOOL, check, nonnegative, upper);
            index = converted;
        } else {
            check = reserve_ids(e, 1u);
            EMIT(OP_ULESS_THAN, ID_BOOL, check, index, length);
        }
        w->cursor = index;
        if (*condition != 0u) {
            const uint32_t combined = reserve_ids(e, 1u);
            EMIT(OP_LOGICAL_AND, ID_BOOL, combined, *condition, check);
            *condition = combined;
        } else *condition = check;
    }
    return !e->failed;
}
static bool emit_pointer_access(Emitter *e, const IrInstruction *ir) {
    CompilerWorkspace *c = e->program;
    const uint32_t root = ir->operands.access.root, category = ir->operands.access.category;
    const bool store = ir->opcode == IR_ACCESS_STORE;
    const ResourceRecord *resource = category == ROOT_RESOURCE ? &c->resources[root] : NULL;
    const uint32_t storage = resource != NULL ? resource_storage(resource) : category == ROOT_WORKGROUP ? 4u : 7u;
    const bool wrapped = resource != NULL && wrapped_resource(c, resource);
    const uint32_t local = category == ROOT_VALUE ? ir->operands.access.value : root;
    const uint32_t base = resource != NULL ? resource->target : category == ROOT_WORKGROUP ? c->workgroups[root].target :
        e->local_base + (local - e->local_start);
    if (category == ROOT_VALUE) { EMIT(OP_STORE, base, e->values[root]); }
    if (!access_walk(e, ir->operands.access.path)) return false;
    uint32_t condition;
    if (!access_condition(e, ir, &condition)) return false;
    const uint32_t predecessor = e->current_label;
    uint32_t body = 0u, merge = 0u;
    if (condition != 0u) {
        body = reserve_ids(e, 1u); merge = reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, condition, body, merge);
        EMIT(OP_LABEL, body);
    }
    uint32_t pointer = base;
    if (c->walk_count != 0u || wrapped) {
        pointer = reserve_ids(e, 1u);
        uint32_t *words = NULL;
        if (!instruction_space(e, OP_ACCESS_CHAIN, (size_t)c->walk_count + (wrapped ? 1u : 0u) + 3u, &words)) return false;
        words[0] = pointer_type(e, ir->type, storage); words[1] = pointer; words[2] = base;
        uint32_t output = 3u;
        if (wrapped) words[output++] = ID_ZERO_U32;
        for (uint32_t i = c->walk_count; i != 0u; --i) words[output++] = c->walk[i - 1u].cursor;
    }
    const uint32_t loaded = store ? 0u : condition == 0u ? e->values[ir->result] : reserve_ids(e, 1u);
    if (store) { EMIT(OP_STORE, pointer, e->values[ir->operands.access.value]); }
    else { EMIT(OP_LOAD, target_type(e, ir->type), loaded, pointer); }
    if (condition != 0u) {
        EMIT(OP_BRANCH, merge);
        EMIT(OP_LABEL, merge);
        if (!store) { EMIT(OP_PHI, target_type(e, ir->type), e->values[ir->result], loaded, body, zero_constant(e, ir->type), predecessor); }
        e->current_label = merge;
    }
    c->walk_count = 0u;
    return !e->failed;
}
static bool emit_value_access(Emitter *e, const IrInstruction *ir) {
    if (ir->operands.access.value != NO_ID) return emit_pointer_access(e, ir);
    CompilerWorkspace *c = e->program;
    if (!access_walk(e, ir->operands.access.path)) return false;
    uint32_t value = e->values[ir->operands.access.root];
    if (c->walk_count == 0u) {
        EMIT(OP_COPY_OBJECT, target_type(e, ir->type), e->values[ir->result], value);
        return true;
    }
    /* Literal prefixes may extract aggregates without materializing storage.
     * A dynamic vector step is necessarily final because its result is scalar. */
    for (uint32_t i = c->walk_count; i != 0u; --i) {
        const AccessStep *a = &c->accesses[c->walk[i - 1u].object];
        uint32_t index = a->index;
        const bool literal = a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT || literal_index(c, a->index, &index);
        const uint32_t result = i == 1u ? e->values[ir->result] : reserve_ids(e, 1u);
        if (literal) {
            EMIT(OP_COMPOSITE_EXTRACT, target_type(e, a->output_type), result, value, index);
        } else {
            if (i != 1u || c->types[a->input_type - 7u].kind != TYPE_VECTOR)
                return emit_error(e, a->source_offset, "dynamic array value access requires Function temporary");
            /* The selected one-step suffix retains the same common guard owner. */
            const uint32_t saved_count = c->walk_count;
            c->walk_count = 1u;
            uint32_t condition;
            if (!access_condition(e, ir, &condition)) return false;
            c->walk_count = saved_count;
            const uint32_t predecessor = e->current_label;
            const uint32_t body = reserve_ids(e, 1u), merge = reserve_ids(e, 1u), extracted = reserve_ids(e, 1u);
            EMIT(OP_SELECTION_MERGE, merge, 0u);
            EMIT(OP_BRANCH_CONDITIONAL, condition, body, merge);
            EMIT(OP_LABEL, body);
            EMIT(OP_VECTOR_EXTRACT_DYNAMIC, target_type(e, a->output_type), extracted, value, c->walk[0].cursor);
            EMIT(OP_BRANCH, merge);
            EMIT(OP_LABEL, merge);
            EMIT(OP_PHI, target_type(e, ir->type), result, extracted, body, zero_constant(e, ir->type), predecessor);
            e->current_label = merge;
        }
        value = result;
    }
    c->walk_count = 0u;
    return !e->failed;
}
typedef struct EmitLoop EmitLoop;
struct EmitLoop {
    EmitLoop *parent;
    uint32_t header, continuing, merge;
    bool continue_live;
};
static bool emit_block(Emitter *e, const IrProgram *program, size_t *cursor,
                       unsigned depth, EmitLoop *loop, bool *live);
static bool emit_selection(Emitter *e, const IrProgram *program, size_t *cursor,
                           unsigned depth, EmitLoop *loop, bool *live) {
    const IrInstruction *test = &program->instructions[(*cursor)++];
    if (depth >= e->source->limits->control_depth + e->source->limits->expression_depth)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, test->source_offset,
                            "internal structured IR depth exceeded");
    const bool active = *live;
    uint32_t yes = 0u, no = 0u, merge = 0u;
    if (active) {
        yes = reserve_ids(e, 1u);
        no = reserve_ids(e, 1u);
        merge = reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, e->values[test->operands.scalar.left], yes, no);
        EMIT(OP_LABEL, yes);
        e->current_label = yes;
    }
    bool yes_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, loop, &yes_live)) return false;
    if (*cursor == program->instruction_count || program->instructions[*cursor].opcode != IR_ELSE)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, test->source_offset,
                            "internal structured IR missing else boundary");
    ++*cursor;
    if (yes_live) { EMIT(OP_BRANCH, merge); }
    if (active) {
        EMIT(OP_LABEL, no);
        e->current_label = no;
    }
    bool no_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, loop, &no_live)) return false;
    if (*cursor == program->instruction_count || program->instructions[*cursor].opcode != IR_END_IF)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, test->source_offset,
                            "internal structured IR missing merge boundary");
    ++*cursor;
    if (no_live) { EMIT(OP_BRANCH, merge); }
    *live = yes_live || no_live;
    if (active) {
        EMIT(OP_LABEL, merge);
        e->current_label = merge;
        /* A syntactically required merge with no predecessors cannot fall through. */
        if (!*live && !instruction(e, OP_UNREACHABLE, NULL, 0u)) return false;
    }
    return true;
}
static bool expect_marker(Emitter *e, const IrProgram *program, size_t cursor, IrOpcode opcode) {
    if (cursor < program->instruction_count && program->instructions[cursor].opcode == opcode) return true;
    return source_error(e->source, DOE_COMPILER_INPUT_ERROR, e->source->length,
                        "internal structured IR boundary mismatch");
}
static bool emit_short_circuit(Emitter *e, const IrProgram *program, size_t *cursor,
                               unsigned depth, EmitLoop *loop, bool *live) {
    const IrInstruction *begin = &program->instructions[(*cursor)++];
    const bool active = *live;
    uint32_t rhs = 0u, merge = 0u, predecessor = e->current_label;
    if (active) {
        rhs = reserve_ids(e, 1u); merge = reserve_ids(e, 1u);
        EMIT(OP_SELECTION_MERGE, merge, 0u);
        EMIT(OP_BRANCH_CONDITIONAL, e->values[begin->operands.scalar.left],
             begin->operands.scalar.literal != 0u ? merge : rhs, begin->operands.scalar.literal != 0u ? rhs : merge);
        EMIT(OP_LABEL, rhs);
        e->current_label = rhs;
    }
    bool rhs_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, loop, &rhs_live) ||
        !expect_marker(e, program, *cursor, IR_SHORT_END)) return false;
    const IrInstruction *end = &program->instructions[(*cursor)++];
    if (active) {
        const uint32_t rhs_predecessor = e->current_label;
        EMIT(OP_BRANCH, merge);
        EMIT(OP_LABEL, merge);
        e->values[end->result] = reserve_ids(e, 1u);
        EMIT(OP_PHI, ID_BOOL, e->values[end->result], e->values[end->operands.scalar.left], predecessor,
             e->values[end->operands.scalar.right], rhs_predecessor);
        e->current_label = merge;
    }
    return true;
}
static bool emit_loop(Emitter *e, const IrProgram *program, size_t *cursor,
                      unsigned depth, EmitLoop *outer, bool *live) {
    const IrInstruction *begin = &program->instructions[(*cursor)++];
    const bool active = *live;
    EmitLoop loop = { .parent = outer };
    uint32_t test_label = 0u, body_label = 0u;
    if (active) {
        loop.header = reserve_ids(e, 1u); test_label = reserve_ids(e, 1u);
        body_label = reserve_ids(e, 1u); loop.continuing = reserve_ids(e, 1u);
        loop.merge = reserve_ids(e, 1u);
        EMIT(OP_BRANCH, loop.header);
        EMIT(OP_LABEL, loop.header);
        EMIT(OP_LOOP_MERGE, loop.merge, loop.continuing, 0u);
        EMIT(OP_BRANCH, test_label);
        EMIT(OP_LABEL, test_label);
        e->current_label = test_label;
    }
    bool test_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, &loop, &test_live) ||
        !expect_marker(e, program, *cursor, IR_LOOP_TEST)) return false;
    const IrInstruction *test = &program->instructions[(*cursor)++];
    if (active) {
        if (test->operands.scalar.left != 0u) { EMIT(OP_BRANCH_CONDITIONAL, e->values[test->operands.scalar.left], body_label, loop.merge); }
        else { EMIT(OP_BRANCH, body_label); }
        EMIT(OP_LABEL, body_label);
        e->current_label = body_label;
    }
    bool body_live = active;
    if (!emit_block(e, program, cursor, depth + 1u, &loop, &body_live) ||
        !expect_marker(e, program, *cursor, IR_LOOP_CONTINUING)) return false;
    ++*cursor;
    if (body_live) { EMIT(OP_BRANCH, loop.continuing); loop.continue_live = true; }
    if (active) {
        EMIT(OP_LABEL, loop.continuing);
        e->current_label = loop.continuing;
        /* A predecessorless continuing block still supplies the one structural
         * back-edge required by SPIR-V. Its source operations remain suppressed. */
    }
    bool tail_live = active && loop.continue_live;
    if (!emit_block(e, program, cursor, depth + 1u, &loop, &tail_live) ||
        !expect_marker(e, program, *cursor, IR_END_LOOP)) return false;
    ++*cursor;
    if (tail_live || (active && !loop.continue_live)) { EMIT(OP_BRANCH, loop.header); }
    *live = active && ((begin->operands.scalar.literal & 1u) != 0u);
    if (active) {
        EMIT(OP_LABEL, loop.merge);
        e->current_label = loop.merge;
        if (!*live && !instruction(e, OP_UNREACHABLE, NULL, 0u)) return false;
    }
    return true;
}
static bool emit_workgroup_barrier(Emitter *e) {
    /* AcquireRelease | WorkgroupMemory. This does not fence descriptor storage. */
    EMIT(OP_CONTROL_BARRIER, ID_TWO_U32, ID_TWO_U32, e->barrier_semantics);
    return true;
}
static bool emit_block(Emitter *e, const IrProgram *program, size_t *cursor,
                       unsigned depth, EmitLoop *loop, bool *live) {
    while (*cursor < e->function_end) {
        const IrInstruction *ir = &program->instructions[*cursor];
        if (ir->opcode == IR_ELSE || ir->opcode == IR_END_IF || ir->opcode == IR_SHORT_END ||
            ir->opcode == IR_LOOP_TEST || ir->opcode == IR_LOOP_CONTINUING || ir->opcode == IR_END_LOOP) return true;
        if (ir->opcode == IR_IF) {
            if (!emit_selection(e, program, cursor, depth, loop, live)) return false;
            continue;
        }
        if (ir->opcode == IR_SHORT_BEGIN) {
            if (!emit_short_circuit(e, program, cursor, depth, loop, live)) return false;
            continue;
        }
        if (ir->opcode == IR_LOOP_BEGIN) {
            if (!emit_loop(e, program, cursor, depth, loop, live)) return false;
            continue;
        }
        ++*cursor;
        if (!*live) continue;
        if (ir->result != 0u && ir->opcode != IR_CONSTANT) e->values[ir->result] = reserve_ids(e, 1u);
        const uint32_t result = e->values[ir->result];
        switch (ir->opcode) {
            case IR_INPUT: {
                const uint32_t input = e->inputs[ir->operands.scalar.binding];
                if (program->parameters[ir->operands.scalar.binding].kind == INPUT_LOCAL_INDEX) {
                    EMIT(OP_LOAD, ID_U32, result, input);
                } else if (ir->operands.scalar.literal == NO_ID) {
                    EMIT(OP_LOAD, ID_VEC3, result, input);
                } else {
                    const uint32_t vector = reserve_ids(e, 1u);
                    EMIT(OP_LOAD, ID_VEC3, vector, input);
                    EMIT(OP_COMPOSITE_EXTRACT, ID_U32, result, vector, ir->operands.scalar.literal);
                }
                break;
            }
            case IR_ACCESS_LOAD:
            case IR_ACCESS_STORE:
                if (!emit_pointer_access(e, ir)) return false;
                break;
            case IR_VALUE_ACCESS:
                if (!emit_value_access(e, ir)) return false;
                break;
            case IR_COMPOSITE:
                if (!emit_composite(e, ir)) return false;
                break;
            case IR_SWIZZLE:
                if (!emit_swizzle(e, ir)) return false;
                break;
            case IR_ADD:
            case IR_SUBTRACT:
            case IR_MULTIPLY:
                if (!emit_arithmetic(e, ir)) return false;
                break;
            case IR_DIVIDE:
            case IR_REMAINDER:
            case IR_SHIFT_LEFT:
            case IR_SHIFT_RIGHT:
            case IR_CONVERT:
                if (!emit_safe_operation(e, ir)) return false;
                break;
            case IR_BITCAST:
            case IR_NEGATE:
            case IR_COMPLEMENT:
                EMIT(ir->opcode == IR_BITCAST ? OP_BITCAST : ir->opcode == IR_COMPLEMENT ? OP_NOT :
                     type_leaf(program, ir->type) == IR_F32 ? OP_FNEGATE : OP_SNEGATE,
                     target_type(e, ir->type), result, e->values[ir->operands.scalar.left]);
                break;
            case IR_BIT_AND:
            case IR_BIT_OR:
            case IR_BIT_XOR: {
                const uint32_t opcode = type_leaf(program, ir->type) == IR_BOOL ?
                    (ir->opcode == IR_BIT_AND ? OP_LOGICAL_AND : OP_LOGICAL_OR) :
                    ir->opcode == IR_BIT_AND ? OP_BIT_AND : ir->opcode == IR_BIT_OR ? OP_BIT_OR : OP_BIT_XOR;
                EMIT(opcode, target_type(e, ir->type), result,
                     e->values[ir->operands.scalar.left], e->values[ir->operands.scalar.right]);
                break;
            }
            case IR_CALL:
                if (!emit_call(e, program, ir)) return false;
                break;
            case IR_BUILTIN:
                if (!emit_numeric_builtin(e, ir)) return false;
                break;
            case IR_WORKGROUP_BARRIER:
                if (!emit_workgroup_barrier(e)) return false;
                break;
            case IR_COMPARE:
                EMIT(comparison_opcode((Comparison)ir->operands.scalar.literal, type_leaf(program, ir->operands.scalar.binding)),
                     target_type(e, ir->type), result, e->values[ir->operands.scalar.left], e->values[ir->operands.scalar.right]);
                break;
            case IR_NOT:
                EMIT(OP_LOGICAL_NOT, target_type(e, ir->type), result, e->values[ir->operands.scalar.left]);
                break;
            case IR_LOCAL_LOAD:
                EMIT(OP_LOAD, target_type(e, ir->type), result, e->local_base + (ir->operands.scalar.binding - e->local_start));
                break;
            case IR_LOCAL_STORE:
                EMIT(OP_STORE, e->local_base + (ir->operands.scalar.binding - e->local_start), e->values[ir->operands.scalar.right]);
                break;
            case IR_BREAK:
            case IR_CONTINUE:
            case IR_BREAK_IF:
                if (loop == NULL) return emit_error(e, ir->source_offset, "internal loop target is missing");
                if (ir->opcode == IR_BREAK_IF) {
                    EMIT(OP_BRANCH_CONDITIONAL, e->values[ir->operands.scalar.left], loop->merge, loop->header);
                } else {
                    EMIT(OP_BRANCH, ir->opcode == IR_BREAK ? loop->merge : loop->continuing);
                    if (ir->opcode == IR_CONTINUE) loop->continue_live = true;
                }
                *live = false;
                break;
            case IR_RETURN:
                if (ir->type == IR_VOID) {
                    if (!instruction(e, OP_RETURN, NULL, 0u)) return false;
                } else { EMIT(OP_RETURN_VALUE, e->values[ir->operands.scalar.left]); }
                *live = false;
                break;
            case IR_CONSTANT:
                break;
            case IR_UNIFORM_LOAD:
            case IR_ROBUST_LOAD:
            case IR_ROBUST_STORE:
            case IR_MAX:
                return emit_error(e, ir->source_offset, "obsolete scalar-only IR operation");
            case IR_SHORT_BEGIN:
            case IR_SHORT_END:
            case IR_LOOP_BEGIN:
            case IR_LOOP_TEST:
            case IR_LOOP_CONTINUING:
            case IR_END_LOOP:
            case IR_IF:
            case IR_ELSE:
            case IR_END_IF:
                return emit_error(e, ir->source_offset, "unexpected internal structured IR marker");
        }
    }
    return !e->failed;
}
static bool emit_workgroup_initialization(Emitter *e) {
    const uint32_t lane = reserve_ids(e, 1u), first_lane = reserve_ids(e, 1u);
    const uint32_t initialize = reserve_ids(e, 1u), merge = reserve_ids(e, 1u);
    EMIT(OP_LOAD, ID_U32, lane, e->workgroup_input);
    EMIT(OP_IEQUAL, ID_BOOL, first_lane, lane, ID_ZERO_U32);
    EMIT(OP_SELECTION_MERGE, merge, 0u);
    EMIT(OP_BRANCH_CONDITIONAL, first_lane, initialize, merge);
    EMIT(OP_LABEL, initialize);
    for (uint32_t i = 0u; i < e->program->workgroup_count; ++i) {
        const WorkgroupRoot *root = &e->program->workgroups[i];
        EMIT(OP_STORE, root->target, workgroup_zero(e, root->type));
    }
    EMIT(OP_BRANCH, merge);
    EMIT(OP_LABEL, merge);
    e->current_label = merge;
    return emit_workgroup_barrier(e);
}
static bool function_body(Emitter *e, const IrProgram *program, const FunctionRecord *function) {
    EMIT(OP_FUNCTION, target_type(e, function->result_type), function->state.target.function, 0u, function->state.target.signature);
    for (uint32_t p = 0u; p < function->formal_count; ++p) {
        const uint32_t index = function->formal_start + p;
        const uint32_t identity = e->program->limits.instructions + 1u + index;
        e->values[identity] = reserve_ids(e, 1u);
        EMIT(OP_FUNCTION_PARAMETER, target_type(e, program->formals[index].type.resolved.type), e->values[identity]);
    }
    e->current_label = function->state.target.label;
    e->function_end = function->instruction_end;
    EMIT(OP_LABEL, e->current_label);
    e->local_base = reserve_ids(e, function->local_count);
    e->local_start = function->local_start;
    for (uint32_t i = 0u; i < function->local_count; ++i)
        EMIT(OP_VARIABLE, pointer_type(e, program->locals[function->local_start + i].type, 7u), e->local_base + i, 7u);
    if (program->workgroup_count != 0u && function == &program->functions[program->entry_function])
        if (!emit_workgroup_initialization(e)) return false;
    size_t cursor = function->instruction_start;
    bool live = true;
    if (!emit_block(e, program, &cursor, 0u, NULL, &live)) return false;
    if (cursor != function->instruction_end) return emit_error(e, (uint32_t)e->source->length, "unexpected top-level structured IR boundary");
    if (live) {
        if (function->result_type != IR_VOID) return emit_error(e, (uint32_t)e->source->length, "missing typed function return");
        if (!instruction(e, OP_RETURN, NULL, 0u)) return false;
    }
    return instruction(e, OP_FUNCTION_END, NULL, 0u);
}
#undef EMIT
bool emit_spirv(Source *source, CompilerWorkspace *program) {
    program->word_count = 0u; program->target_count = 0u; program->walk_count = 0u;
    if (program->limits.words < 5u)
        return source_pool_error(source, DOE_COMPILER_POOL_WORDS, 5u, source->length,
                                 "SPIR-V staging capacity is smaller than the header");
    const uint64_t ceiling = 256u + (uint64_t)program->limits.targets + 64u * (uint64_t)program->limits.instructions +
        3u * (uint64_t)(program->limits.tokens / 6u) + program->limits.tokens / 4u +
        (uint64_t)program->limits.statements + 1u + 11u * (uint64_t)program->limits.accesses + program->limits.resources +
        program->limits.tokens / 8u + 8u;
    const uint32_t bound = ceiling < UINT32_MAX ? (uint32_t)ceiling : UINT32_MAX - 1u;
    const size_t values = (size_t)program->limits.instructions + program->limits.tokens / 4u + 1u;
    memset(program->values, 0, values * sizeof(*program->values));
    program->words[0] = UINT32_C(0x07230203); program->words[1] = UINT32_C(0x00010300);
    program->words[2] = 0u; program->words[3] = 0u; program->words[4] = 0u;
    program->word_count = 5u;
    Emitter emitter = { .source = source, .program = program, .next_id = FIRST_DYNAMIC_ID,
                        .id_bound = bound, .values = program->values };
    if (!plan_module(&emitter) || !module_declarations(&emitter)) return false;
    const uint32_t planned = program->target_count, constants_planned = emitter.constants_used;
    for (uint32_t f = 0u; f < program->function_count; ++f)
        if (!function_body(&emitter, program, &program->functions[f])) return false;
    if (emitter.failed || program->target_count != planned || emitter.constants_used != constants_planned)
        return emit_error(&emitter, (uint32_t)source->length, "target declaration plan changed during body emission");
    program->words[3] = emitter.next_id;
    return true;
}
