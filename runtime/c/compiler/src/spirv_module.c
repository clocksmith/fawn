/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "spirv_internal.h"
#include <string.h>

bool spirv_emit_error(Emitter *e, uint32_t offset, const char *message) {
    e->failed = true;
    return source_error(e->source, DOE_COMPILER_INPUT_ERROR, offset, message);
}
uint32_t spirv_reserve_ids(Emitter *e, uint32_t count) {
    if (e->failed) return 0u;
    if (count > e->id_bound - e->next_id) {
        if (e->id_bound == UINT32_MAX - 1u) {
            e->failed = true;
            (void)source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->source->length,
                               "SPIR-V target ID format capacity exceeded");
        } else {
            (void)spirv_emit_error(e, (uint32_t)e->source->length, "target ID capacity exceeded");
        }
        return 0u;
    }
    const uint32_t first = e->next_id;
    e->next_id += count;
    return first;
}
bool spirv_runtime_length(Emitter *e, uint32_t resource, uint32_t result) {
    const CompilerWorkspace *c = e->program;
    if (resource >= c->resource_count || c->resources[resource].tail_type == NO_ID)
        return spirv_emit_error(e, 0u, "runtime length requires a runtime-tail storage resource");
    const ResourceRecord *r = &c->resources[resource];
    const uint32_t member = spirv_wrapped_resource(c, r) ? 0u : c->types[r->type - 7u].count - 1u;
    const uint32_t words[] = { ID_U32, result, r->target, member };
    return spirv_instruction(e, OP_ARRAY_LENGTH, words, 4u);
}
uint32_t spirv_scalar_type(TypeId type) {
    switch (type) {
        case IR_VOID: return ID_VOID;
        case IR_BOOL: return ID_BOOL;
        case IR_I32: return ID_I32;
        case IR_U32: return ID_U32;
        case IR_F32: return ID_F32;
        default: return 0u;
    }
}
uint32_t spirv_target_type(const Emitter *e, TypeId type) {
    return type < 7u ? spirv_scalar_type(type) : e->program->types[type - 7u].target;
}
bool spirv_instruction_space(Emitter *e, uint32_t opcode, size_t count, uint32_t **arguments) {
    CompilerWorkspace *c = e->program;
    if (e->failed) return false;
    if (count >= UINT16_MAX) {
        e->failed = true;
        return source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->source->length,
                            "SPIR-V spirv_instruction word-count format capacity exceeded");
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
bool spirv_instruction(Emitter *e, uint32_t opcode, const uint32_t *arguments, size_t count) {
    uint32_t *words = NULL;
    if (!spirv_instruction_space(e, opcode, count, &words)) return false;
    if (count != 0u) memcpy(words, arguments, count * sizeof(*arguments));
    return true;
}
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
        return spirv_emit_error(e, name.offset, "invalid target source name span");
    const size_t count = span_words(e->source, name, arguments + 1u);
    return spirv_instruction(e, OP_NAME, arguments, count + 1u);
}
TargetDeclaration *spirv_append_declaration(Emitter *e, uint32_t kind, TypeId type) {
    CompilerWorkspace *c = e->program;
    if (c->target_count == c->limits.targets) {
        e->failed = true;
        (void)source_pool_error(e->source, DOE_COMPILER_POOL_TARGET_DECLARATIONS,
            (size_t)c->target_count + 1u, e->source->length, "target declaration capacity exceeded");
        return NULL;
    }
    TargetDeclaration *d = &c->targets[c->target_count++];
    *d = (TargetDeclaration){ .kind = kind, .id = spirv_reserve_ids(e, 1u), .type = type };
    return e->failed ? NULL : d;
}
bool spirv_push_walk(Emitter *e, uint32_t kind, uint32_t object) {
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
uint32_t spirv_unsigned_constant(Emitter *e, uint32_t value) {
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
    TargetDeclaration *d = spirv_append_declaration(e, DECL_U32, IR_U32);
    if (d == NULL) return 0u;
    d->payload = value;
    return d->id;
}
uint32_t spirv_pointer_type(Emitter *e, TypeId type, uint32_t storage) {
    CompilerWorkspace *c = e->program;
    if (type >= 7u && c->types[type - 7u].kind == TYPE_ATOMIC) type = c->types[type - 7u].element;
    const uint32_t slot = storage == 7u ? 0u : storage == 2u ? 1u : storage == 12u ? 2u : 3u;
    if (type >= IR_BOOL && type <= IR_F32 && (storage == 7u || storage == 2u || storage == 12u || storage == 4u)) {
        if (e->local_pointers[slot][type] == 0u) e->local_pointers[slot][type] = spirv_reserve_ids(e, 1u);
        return e->local_pointers[slot][type];
    }
    for (uint32_t i = 0u; i < c->target_count; ++i) {
        const TargetDeclaration *d = &c->targets[i];
        if (d->kind == DECL_POINTER && d->type == type && d->storage == storage) return d->id;
    }
    TargetDeclaration *d = spirv_append_declaration(e, DECL_POINTER, type);
    if (d == NULL) return 0u;
    d->storage = storage;
    d->payload = storage == 4u ? spirv_workgroup_type(e, type) : spirv_target_type(e, type);
    return d->id;
}
uint32_t spirv_zero_constant(Emitter *e, TypeId type) {
    if (type < 7u) return use_constant(e, type == IR_BOOL ? ID_FALSE : type == IR_I32 ? ID_ZERO_I32 :
        type == IR_U32 ? ID_ZERO_U32 : type == IR_F32 ? ID_ZERO_F32 : 0u);
    TypeRecord *t = &e->program->types[type - 7u];
    if (t->aux != 0u) return t->aux;
    TargetDeclaration *d = spirv_append_declaration(e, DECL_ZERO, type);
    if (d == NULL) return 0u;
    t->aux = d->id;
    return d->id;
}
static bool plan_workgroup_barrier(Emitter *e) {
    (void)use_constant(e, ID_TWO_U32);
    if (e->barrier_semantics == 0u) e->barrier_semantics = spirv_reserve_ids(e, 1u);
    return !e->failed;
}
static bool plan_type(Emitter *e, TypeId type) {
    CompilerWorkspace *c = e->program;
    if (type < 7u) return spirv_scalar_type(type) != 0u;
    if ((c->types[type - 7u].flags & TYPE_DEFERRED) != 0u)
        return source_error(e->source, DOE_COMPILER_INPUT_ERROR, e->source->length,
                            "deferred type reached executable target planning");
    if (c->types[type - 7u].target != 0u) return true;
    if (type_handle(c, type)) {
        const TypeRecord *current = &c->types[type - 7u];
        for (uint32_t i = 0u; i < type - 7u; ++i) {
            const TypeRecord *prior = &c->types[i];
            if (prior->kind != current->kind || prior->target == 0u) continue;
            if (current->kind == TYPE_SAMPLER || (prior->element == current->element &&
                prior->state.image.dimension == current->state.image.dimension &&
                prior->state.image.format == current->state.image.format && prior->state.image.flags == current->state.image.flags)) {
                c->types[type - 7u].target = prior->target;
                return true;
            }
        }
    }
    if (!spirv_push_walk(e, WALK_TYPE, type)) return false;
    c->types[type - 7u].target = NO_ID;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        TypeRecord *t = &c->types[w->object - 7u];
        const uint32_t children = type_handle(c, w->object) ? 0u : (t->kind == TYPE_STRUCT || t->kind == TYPE_ATOMIC_RESULT) ? t->count : 1u;
        if (w->cursor < children) {
            const TypeId child = type_child(c, w->object, w->cursor++);
            if (child < 7u) {
                if (spirv_scalar_type(child) == 0u) return spirv_emit_error(e, t->name.offset, "abstract target type dependency");
            } else if (c->types[child - 7u].target == NO_ID) {
                return spirv_emit_error(e, t->name.offset, "cyclic target type dependency");
            } else if (c->types[child - 7u].target == 0u) {
                if (!spirv_push_walk(e, WALK_TYPE, child)) return false;
                c->types[child - 7u].target = NO_ID;
            }
            continue;
        }
        if (t->kind == TYPE_STRUCT) {
            if (t->count > UINT16_MAX - 2u)
                return source_error(e->source, DOE_COMPILER_UNSUPPORTED, t->name.offset,
                                    "SPIR-V structure spirv_instruction word-count format capacity exceeded");
            for (uint32_t i = 0u; i < t->count; ++i)
                if (c->members[t->element + i].offset > UINT32_MAX)
                    return source_error(e->source, DOE_COMPILER_UNSUPPORTED,
                        c->members[t->element + i].name.offset, "SPIR-V member Offset format capacity exceeded");
        } else if ((t->kind == TYPE_ARRAY || t->kind == TYPE_RUNTIME_ARRAY) && t->state.layout.stride > UINT32_MAX) {
            return source_error(e->source, DOE_COMPILER_UNSUPPORTED, t->name.offset,
                                "SPIR-V ArrayStride format capacity exceeded");
        }
        if (t->kind == TYPE_ATOMIC) t->target = spirv_scalar_type(t->element);
        else if (t->kind == TYPE_POINTER) t->target = spirv_pointer_type(e, t->element, 7u);
        else if (t->kind == TYPE_VECTOR && t->element == IR_U32 && t->count == 3u) t->target = ID_VEC3;
        else {
            const uint32_t length = t->kind == TYPE_ARRAY ? spirv_unsigned_constant(e, t->count) : 0u;
            TargetDeclaration *d = spirv_append_declaration(e, DECL_TYPE, w->object);
            if (d == NULL) return false;
            d->first = length; t->target = d->id;
        }
        --c->walk_count;
    }
    return !e->failed;
}
static uint32_t leaf_constant(Emitter *e, ConstantValue value) {
    if (value.type == IR_BOOL) return use_constant(e, value.bits == 0u ? ID_FALSE : ID_TRUE);
    if (value.bits == 0u) return spirv_zero_constant(e, value.type);
    if (value.type == IR_U32) return spirv_unsigned_constant(e, (uint32_t)value.bits);
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i) {
        const TargetDeclaration *d = &c->targets[i];
        if (d->kind == DECL_LEAF && d->type == value.type && d->payload == value.bits) return d->id;
    }
    TargetDeclaration *d = spirv_append_declaration(e, DECL_LEAF, value.type);
    if (d == NULL) return 0u;
    d->payload = value.bits;
    return d->id;
}
static bool plan_constant(Emitter *e, uint32_t object) {
    CompilerWorkspace *c = e->program;
    if (object >= c->constant_count) return spirv_emit_error(e, 0u, "invalid aggregate constant identity");
    if (c->constants[object].target != 0u) return true;
    if (!spirv_push_walk(e, WALK_CONSTANT, object)) return false;
    c->constants[object].target = NO_ID;
    while (c->walk_count != 0u) {
        WalkItem *w = &c->walk[c->walk_count - 1u];
        ConstantRecord *k = &c->constants[w->object];
        if (spirv_target_type(e, k->type) == 0u || type_abstract(c, k->type))
            return spirv_emit_error(e, 0u, "aggregate target constant must have concrete type");
        const uint32_t children = k->kind == CONSTANT_ZERO ? 0u : k->kind == CONSTANT_REPEAT ? 1u : k->count;
        if (k->kind != CONSTANT_ZERO && k->count > UINT16_MAX - 3u)
            return source_error(e->source, DOE_COMPILER_UNSUPPORTED, e->source->length,
                                "SPIR-V constant spirv_instruction word-count format capacity exceeded");
        if (w->cursor < children) {
            const ConstantValue v = c->constant_edges[k->first + w->cursor++];
            if ((v.flags & CONSTANT_AGGREGATE) == 0u) {
                if (leaf_constant(e, v) == 0u) return false;
            } else {
                const uint32_t child = (uint32_t)v.bits;
                if (child >= c->constant_count) return spirv_emit_error(e, 0u, "invalid constant child identity");
                if (c->constants[child].target == NO_ID) return spirv_emit_error(e, 0u, "cyclic target constant dependency");
                if (c->constants[child].target == 0u) {
                    if (!spirv_push_walk(e, WALK_CONSTANT, child)) return false;
                    c->constants[child].target = NO_ID;
                }
            }
            continue;
        }
        if (k->kind == CONSTANT_ZERO) k->target = spirv_zero_constant(e, k->type);
        else {
            TargetDeclaration *d = spirv_append_declaration(e, DECL_COMPOSITE, k->type);
            if (d == NULL) return false;
            d->first = w->object; k->target = d->id;
        }
        --c->walk_count;
    }
    return !e->failed;
}
uint32_t spirv_sampled_type(Emitter *e, TypeId type) {
    TypeRecord *t = &e->program->types[type - 7u];
    if (t->aux != 0u) return t->aux;
    TargetDeclaration *d = spirv_append_declaration(e, DECL_SAMPLED_IMAGE, type);
    if (d == NULL) return 0u;
    t->aux = d->id;
    return d->id;
}
bool spirv_wrapped_resource(const CompilerWorkspace *c, const ResourceRecord *r) {
    return !type_handle(c, r->type) && !(r->type >= 7u && c->types[r->type - 7u].kind == TYPE_STRUCT &&
             (c->types[r->type - 7u].flags & TYPE_TAIL) != 0u);
}
uint32_t spirv_resource_storage(const ResourceRecord *r) {
    return r->kind >= DOE_COMPILER_SAMPLED_TEXTURE ? 0u : r->kind == DOE_COMPILER_UNIFORM_BUFFER ? 2u : 12u;
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
            (void)spirv_zero_constant(e, to);
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
        if (to == IR_BOOL) (void)spirv_zero_constant(e, from);
        else if (from == IR_BOOL) {
            (void)spirv_zero_constant(e, to);
            (void)use_constant(e, to == IR_I32 ? ID_ONE_I32 : to == IR_F32 ? ID_ONE_F32 : ID_ONE_U32);
        } else if (from == IR_F32) {
            (void)use_constant(e, ID_ZERO_F32);
            if (to == IR_I32) (void)use_constant(e, ID_F32_I32_MIN);
            (void)use_constant(e, to == IR_I32 ? ID_F32_I32_MAX : ID_F32_U32_MAX);
        }
    }
}
bool spirv_plan_module(Emitter *e) {
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->type_count; ++i) { c->types[i].target = 0u; c->types[i].aux = 0u; }
    for (uint32_t i = 0u; i < c->constant_count; ++i) c->constants[i].target = 0u;
    for (uint32_t i = 0u; i < c->type_count; ++i) {
        if ((c->types[i].flags & TYPE_DEFERRED) == 0u &&
            !type_abstract(c, i + 7u) && !plan_type(e, i + 7u)) return false;
        const TypeRecord *t = &c->types[i];
        if (t->kind != TYPE_TEXTURE) continue;
        const ImageType image = t->state.image;
        if (image.dimension == DOE_COMPILER_VIEW_1D) e->image_capabilities |= image.format != 0u ? 4u : 2u;
        if (image.dimension == DOE_COMPILER_VIEW_CUBE_ARRAY) e->image_capabilities |= 8u;
        if (image.format == DOE_COMPILER_FORMAT_RG32FLOAT ||
            image.format == DOE_COMPILER_FORMAT_RG32SINT ||
            image.format == DOE_COMPILER_FORMAT_RG32UINT) e->image_capabilities |= 16u;
    }
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        FunctionRecord *function = &c->functions[f];
        function->state.target.function = spirv_reserve_ids(e, 1u);
        function->state.target.label = spirv_reserve_ids(e, 1u);
        function->state.target.signature = 0u;
        if (stage_graphics(function) || (function->result_type == IR_VOID && function->formal_count == 0u)) function->state.target.signature = ID_FN_TYPE;
        else {
            if (function->formal_count > UINT16_MAX - 3u)
                return source_error(e->source, DOE_COMPILER_UNSUPPORTED, function->name.offset,
                                    "SPIR-V function spirv_instruction word-count format capacity exceeded");
            for (uint32_t p = 0u; p < f; ++p)
                if (!stage_graphics(&c->functions[p]) && same_signature(c, function, &c->functions[p])) {
                    function->state.target.signature = c->functions[p].state.target.signature; break;
                }
            if (function->state.target.signature == 0u) function->state.target.signature = spirv_reserve_ids(e, 1u);
        }
    }
    if (!spirv_stage_plan(e)) return false;
    for (uint32_t i=0u;i<c->entry_input_count;++i) {
        c->entry_inputs[i].target=spirv_reserve_ids(e,1u);
        if (input_subgroup(c->entry_inputs[i].kind)) e->subgroup_capabilities |= 2u;
    }
    for (uint32_t j=0u;j<c->entry_count;++j) {
        EntryRecord *entry=&c->entries[j];
        if (entry->state.resolved.workgroup_count==0u) continue;
        for (uint32_t i=entry->input_first;i<entry->input_first+entry->input_count;++i)
            if (c->entry_inputs[i].kind==INPUT_LOCAL_INDEX)
                entry->state.resolved.local_index_target=c->entry_inputs[i].target;
        if (entry->state.resolved.local_index_target==0u)
            entry->state.resolved.local_index_target=spirv_reserve_ids(e,1u);
        (void)use_constant(e,ID_ZERO_U32);
        if (!plan_workgroup_barrier(e)) return false;
    }
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        ResourceRecord *r = &c->resources[i];
        bool raw;
        if (!spirv_uniform_plan(e, r, &raw)) return false;
        if (raw) { r->target = spirv_reserve_ids(e, 1u); continue; }
        if (spirv_wrapped_resource(c, r)) {
            (void)use_constant(e, ID_ZERO_U32);
            TargetDeclaration *wrapper = spirv_append_declaration(e, DECL_WRAPPER, r->type);
            if (wrapper == NULL) return false;
            const uint32_t wrapper_id = wrapper->id;
            TargetDeclaration *pointer = spirv_append_declaration(e, DECL_POINTER, NO_ID);
            if (pointer == NULL) return false;
            pointer->storage = spirv_resource_storage(r); pointer->payload = wrapper_id;
            r->reserved = pointer->id;
        } else r->reserved = spirv_pointer_type(e, r->type, spirv_resource_storage(r));
        r->target = spirv_reserve_ids(e, 1u);
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        WorkgroupRoot *root = &c->workgroups[i];
        if (!spirv_workgroup_plan(e, root->type)) return false;
        if (spirv_pointer_type(e, root->type, 4u) == 0u || spirv_workgroup_zero(e, root->type) == 0u) return false;
        root->target = spirv_reserve_ids(e, 1u);
    }
    for (uint32_t i = 0u; i < c->local_count; ++i)
        if (spirv_pointer_type(e, c->locals[i].type, 7u) == 0u) return false;
    for (uint32_t i = 0u; i < c->access_count; ++i) {
        const AccessStep *a = &c->accesses[i];
        if (a->kind == ACCESS_INDEX_I32) (void)use_constant(e, ID_ZERO_I32);
        if (a->kind == ACCESS_MEMBER || a->kind == ACCESS_COMPONENT) {
            if (spirv_unsigned_constant(e, a->index) == 0u) return false;
        } else if (a->input_type >= 7u && c->types[a->input_type - 7u].kind != TYPE_RUNTIME_ARRAY) {
            if (spirv_unsigned_constant(e, c->types[a->input_type - 7u].count) == 0u) return false;
        }
    }
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        const IrInstruction *ir = &c->instructions[i];
        plan_scalar_guards(e, ir);
        if (ir->opcode == IR_CONSTANT) {
            if (ir->operands.scalar.left != 0u) {
                if (!plan_constant(e, ir->operands.scalar.binding)) return false;
                e->values[ir->result] = c->constants[ir->operands.scalar.binding].target;
            } else e->values[ir->result] = spirv_reserve_ids(e, 1u);
        } else if (atomic_opcode(ir->opcode)) {
            const uint32_t storage = ir->operands.access.category == ROOT_WORKGROUP ? 4u : 12u;
            if (spirv_pointer_type(e, atomic_element(c, ir), storage) == 0u ||
                (ir->type != IR_VOID && spirv_zero_constant(e, ir->type) == 0u)) return false;
            (void)spirv_unsigned_constant(e, ir->operands.access.category == ROOT_WORKGROUP ? 2u : 1u);
            (void)spirv_unsigned_constant(e, 0u);
        } else if (ir->opcode == IR_IMAGE) {
            if (!spirv_image_plan(e, ir)) return false;
        } else if (ir->opcode == IR_DISCARD) {
            e->demote_to_helper = true;
        } else if (ir->opcode == IR_WORKGROUP_BARRIER) {
            if (!plan_workgroup_barrier(e)) return false;
        } else if (ir->opcode == IR_BUILTIN) {
            const NumericBuiltin *builtin = builtin_kind((NumericBuiltinKind)ir->operands.scalar.literal);
            if (builtin == NULL) return spirv_emit_error(e, ir->source_offset, "invalid numeric builtin identity");
            if (builtin->kind == BUILTIN_UNPACK4X_U8) {
                (void)spirv_unsigned_constant(e, 255u);
                for (uint32_t lane = 0u; lane < 4u; ++lane)
                    (void)spirv_unsigned_constant(e, lane * 8u);
            }
            if (builtin_subgroup(builtin)) {
                e->subgroup_capabilities |= 1u;
                (void)use_constant(e, ID_THREE_U32);
            }
            if (builtin_extended_instruction(builtin, type_leaf(c, ir->type)) != 0u && e->math_import == 0u)
                e->math_import = spirv_reserve_ids(e, 1u);
        } else if (ir->opcode == IR_ACCESS_LOAD || ir->opcode == IR_ACCESS_STORE || ir->opcode == IR_VALUE_ACCESS) {
            if (!spirv_uniform_access_plan(e, ir)) return false;
            const uint32_t category = ir->operands.access.category;
            const uint32_t storage = category == ROOT_RESOURCE ? spirv_resource_storage(&c->resources[ir->operands.access.root]) :
                category == ROOT_WORKGROUP ? 4u : 7u;
            if (ir->opcode != IR_VALUE_ACCESS || ir->operands.access.value != NO_ID)
                if (spirv_pointer_type(e, ir->type, storage) == 0u) return false;
            if (ir->opcode != IR_ACCESS_STORE && spirv_zero_constant(e, ir->type) == 0u) return false;
            if (category == ROOT_WORKGROUP && spirv_workgroup_zero(e, ir->type) == 0u) return false;
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
        case INPUT_SUBGROUP_SIZE: return 36u;
        case INPUT_SUBGROUP_INVOCATION: return 41u;
        case INPUT_NUM_SUBGROUPS: return 38u;
        case INPUT_SUBGROUP_ID: return 40u;
    }
    return 0u;
}
bool spirv_hidden_initialization_input(const CompilerWorkspace *c, const EntryRecord *entry) {
    if (entry->state.resolved.local_index_target==0u) return false;
    for (uint32_t i=entry->input_first;i<entry->input_first+entry->input_count;++i)
        if (c->entry_inputs[i].kind==INPUT_LOCAL_INDEX) return false;
    return true;
}
static bool annotations(Emitter *e) {
    CompilerWorkspace *c = e->program;
    for (uint32_t i = 0u; i < c->target_count; ++i) {
        const TargetDeclaration *d = &c->targets[i];
        if (d->kind == DECL_RAW_UNIFORM) {
            if (!spirv_uniform_annotations(e, d)) return false;
        } else if (d->kind == DECL_WRAPPER) {
            EMIT(OP_DECORATE, d->id, 2u);
            EMIT(OP_MEMBER_DECORATE, d->id, 0u, 35u, 0u);
            if (!spirv_matrix_member(e, d->id, 0u, d->type)) return false;
        } else if (d->kind == DECL_TYPE) {
            const TypeRecord *t = &c->types[d->type - 7u];
            if (t->kind == TYPE_ARRAY || t->kind == TYPE_RUNTIME_ARRAY) {
                EMIT(OP_DECORATE, d->id, 6u, (uint32_t)t->state.layout.stride);
            } else if (t->kind == TYPE_STRUCT) {
                if ((t->flags & TYPE_TAIL) != 0u) { EMIT(OP_DECORATE, d->id, 2u); }
                for (uint32_t m = 0u; m < t->count; ++m) {
                    EMIT(OP_MEMBER_DECORATE, d->id, m, 35u, (uint32_t)c->members[t->element + m].offset);
                    if (!spirv_matrix_member(e, d->id, m, c->members[t->element + m].type)) return false;
                }
            }
        }
    }
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const ResourceRecord *r = &c->resources[i];
        EMIT(OP_DECORATE, r->target, 34u, r->group);
        EMIT(OP_DECORATE, r->target, 33u, r->binding);
        if (r->access == DOE_COMPILER_READ && (r->kind == DOE_COMPILER_STORAGE_BUFFER || r->kind == DOE_COMPILER_STORAGE_TEXTURE)) { EMIT(OP_DECORATE, r->target, 24u); }
        if (r->access == DOE_COMPILER_WRITE) { EMIT(OP_DECORATE, r->target, 25u); }
    }
    for (uint32_t i=0u;i<c->entry_input_count;++i)
        EMIT(OP_DECORATE,c->entry_inputs[i].target,11u,builtin_decoration(c->entry_inputs[i].kind));
    for (uint32_t i=0u;i<c->entry_count;++i)
        if (spirv_hidden_initialization_input(c,&c->entries[i])) {
            EMIT(OP_DECORATE,c->entries[i].state.resolved.local_index_target,11u,builtin_decoration(INPUT_LOCAL_INDEX));
        }
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
                     spirv_scalar_type(type));
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
            case DECL_WORKGROUP_TYPE:
            case DECL_TYPE: {
                const TypeRecord *t = &c->types[d->type - 7u];
                const bool workgroup = d->kind == DECL_WORKGROUP_TYPE;
                if (t->kind == TYPE_TEXTURE || t->kind == TYPE_SAMPLER) {
                    if (!spirv_image_type(e, t, d->id)) return false;
                } else if (t->kind == TYPE_VECTOR) { EMIT(OP_TYPE_VECTOR, d->id, spirv_target_type(e, t->element), t->count); }
                else if (t->kind == TYPE_MATRIX) { EMIT(OP_TYPE_MATRIX, d->id, spirv_target_type(e, t->element), t->count); }
                else if (t->kind == TYPE_ARRAY) {
                    EMIT(OP_TYPE_ARRAY, d->id, workgroup ? spirv_workgroup_type(e, t->element) : spirv_target_type(e, t->element), d->first);
                }
                else if (t->kind == TYPE_RUNTIME_ARRAY) { EMIT(OP_TYPE_RUNTIME_ARRAY, d->id, spirv_target_type(e, t->element)); }
                else {
                    uint32_t *words = NULL;
                    if (!spirv_instruction_space(e, OP_TYPE_STRUCT, (size_t)t->count + 1u, &words)) return false;
                    words[0] = d->id;
                    for (uint32_t m = 0u; m < t->count; ++m) {
                        const TypeId child = type_child(c, d->type, m);
                        words[m + 1u] = workgroup ? spirv_workgroup_type(e, child) : spirv_target_type(e, child);
                    }
                }
                break;
            }
            case DECL_RAW_UNIFORM:
            case DECL_RAW_VECTOR:
                if (!spirv_uniform_declaration(e, d)) return false;
                break;
            case DECL_SAMPLED_IMAGE:
                EMIT(OP_TYPE_SAMPLED_IMAGE, d->id, spirv_target_type(e, d->type));
                break;
            case DECL_POINTER:
                EMIT(OP_TYPE_POINTER, d->id, d->storage, (uint32_t)d->payload);
                break;
            case DECL_WRAPPER:
                EMIT(OP_TYPE_STRUCT, d->id, spirv_target_type(e, d->type));
                break;
            case DECL_U32:
            case DECL_LEAF:
                EMIT(OP_CONSTANT, spirv_target_type(e, d->type), d->id, (uint32_t)d->payload);
                break;
            case DECL_ZERO:
                EMIT(OP_CONSTANT_NULL, spirv_target_type(e, d->type), d->id);
                break;
            case DECL_WORKGROUP_ZERO:
                EMIT(OP_CONSTANT_NULL, spirv_workgroup_type(e, d->type), d->id);
                break;
            case DECL_COMPOSITE: {
                const ConstantRecord *k = &c->constants[d->first];
                uint32_t *words = NULL;
                if (!spirv_instruction_space(e, OP_CONSTANT_COMPOSITE, (size_t)k->count + 2u, &words)) return false;
                words[0] = spirv_target_type(e, k->type); words[1] = d->id;
                for (uint32_t j = 0u; j < k->count; ++j) {
                    const ConstantValue v = c->constant_edges[k->first + (k->kind == CONSTANT_REPEAT ? 0u : j)];
                    words[j + 2u] = (v.flags & CONSTANT_AGGREGATE) != 0u ? c->constants[(uint32_t)v.bits].target : leaf_constant(e, v);
                }
                break;
            }
            default: return spirv_emit_error(e, 0u, "invalid target declaration kind");
        }
    }
    if (c->target_count != planned_count) return spirv_emit_error(e, 0u, "target declaration plan changed during serialization");
    return !e->failed;
}
bool spirv_module_declarations(Emitter *e) {
    CompilerWorkspace *c = e->program;
    EMIT(OP_CAPABILITY, 1u);
    if (e->sample_rate_shading) { EMIT(OP_CAPABILITY, 35u); }
    if ((e->image_capabilities & 1u) != 0u) { EMIT(OP_CAPABILITY, 50u); }
    if ((e->image_capabilities & 2u) != 0u) { EMIT(OP_CAPABILITY, 43u); }
    if ((e->image_capabilities & 4u) != 0u) { EMIT(OP_CAPABILITY, 44u); }
    if ((e->image_capabilities & 8u) != 0u) { EMIT(OP_CAPABILITY, 45u); }
    if ((e->image_capabilities & 16u) != 0u) { EMIT(OP_CAPABILITY, 49u); }
    if (e->subgroup_capabilities != 0u) {
        EMIT(OP_CAPABILITY, 61u); /* GroupNonUniform, SPIR-V 1.3. */
        if ((e->subgroup_capabilities & 1u) != 0u) { EMIT(OP_CAPABILITY, 63u); }
    }
    if (e->demote_to_helper) {
        EMIT(OP_CAPABILITY, 5379u);
        const char extension[] = "SPV_EXT_demote_to_helper_invocation";
        const size_t count = (sizeof(extension) + 3u) / 4u;
        uint32_t *words = NULL;
        if (!spirv_instruction_space(e, OP_EXTENSION, count, &words)) return false;
        memset(words, 0, count * sizeof(*words));
        memcpy(words, extension, sizeof(extension));
    }
    if (e->math_import != 0u) {
        const uint32_t imported[] = { e->math_import, UINT32_C(0x4c534c47), UINT32_C(0x6474732e), UINT32_C(0x3035342e), 0u };
        if (!spirv_instruction(e, OP_EXT_INST_IMPORT, imported, 5u)) return false;
    }
    EMIT(OP_MEMORY_MODEL, 0u, 1u);
    for (uint32_t i=0u;i<c->entry_count;++i) {
        const FunctionRecord *function=&c->functions[c->entries[i].function];
        const size_t name_words=function->name.length/4u+1u;
        uint32_t *arguments=NULL;
        if (!spirv_instruction_space(e,OP_ENTRY_POINT,2u+name_words,&arguments)) return false;
        arguments[0]=(function->flags & FUNCTION_VERTEX) != 0u ? 0u : (function->flags & FUNCTION_FRAGMENT) != 0u ? 4u : 5u;
        arguments[1]=function->state.target.function;
        (void)span_words(e->source,function->name,arguments+2u);
    }
    for (uint32_t i=0u;i<c->entry_count;++i) {
        const EntryRecord *entry=&c->entries[i];
        if (stage_graphics(&c->functions[entry->function])) {
            if (!spirv_stage_modes(e, i)) return false;
            continue;
        }
        EMIT(OP_EXECUTION_MODE,c->functions[entry->function].state.target.function,17u,
            entry->state.resolved.workgroup_size[0],entry->state.resolved.workgroup_size[1],entry->state.resolved.workgroup_size[2]);
    }
    for (uint32_t i=0u;i<c->entry_count;++i) {
        const FunctionRecord *function=&c->functions[c->entries[i].function];
        if (!debug_name(e,function->state.target.function,function->name)) return false;
    }
    for (uint32_t i=0u;i<c->entry_input_count;++i)
        if (!debug_name(e,c->entry_inputs[i].target,c->entry_inputs[i].name)) return false;
    for (uint32_t i = 0u; i < c->resource_count; ++i)
        if (!debug_name(e, c->resources[i].target, c->resources[i].name)) return false;
    for (uint32_t i = 0u; i < c->workgroup_count; ++i)
        if (!debug_name(e, c->workgroups[i].target, c->workgroups[i].name)) return false;
    if (!annotations(e) || !spirv_stage_annotations(e) || !primitive_declarations(e) || !dynamic_declarations(e)) return false;
    for (uint32_t f = 0u; f < c->function_count; ++f) {
        const FunctionRecord *fn = &c->functions[f];
        bool emitted = fn->state.target.signature == ID_FN_TYPE;
        for (uint32_t p = 0u; p < f && !emitted; ++p)
            emitted = c->functions[p].state.target.signature == fn->state.target.signature;
        if (emitted) continue;
        uint32_t *words = NULL;
        if (!spirv_instruction_space(e, OP_TYPE_FUNCTION, (size_t)fn->formal_count + 2u, &words)) return false;
        words[0] = fn->state.target.signature; words[1] = spirv_target_type(e, fn->result_type);
        for (uint32_t p = 0u; p < fn->formal_count; ++p)
            words[p + 2u] = spirv_target_type(e, c->formals[fn->formal_start + p].type.resolved.type);
    }
    for (uint32_t i = 0u; i < c->instruction_count; ++i) {
        const IrInstruction *ir = &c->instructions[i];
        if (ir->opcode != IR_CONSTANT || ir->operands.scalar.left != 0u) continue;
        if (ir->type == IR_BOOL) {
            EMIT(ir->operands.scalar.literal != 0u ? OP_CONSTANT_TRUE : OP_CONSTANT_FALSE, ID_BOOL, e->values[ir->result]);
        } else { EMIT(OP_CONSTANT, spirv_target_type(e, ir->type), e->values[ir->result], ir->operands.scalar.literal); }
    }
    for (uint32_t i = 0u; i < c->resource_count; ++i) {
        const ResourceRecord *r = &c->resources[i];
        EMIT(OP_VARIABLE, r->reserved, r->target, spirv_resource_storage(r));
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        const WorkgroupRoot *root = &c->workgroups[i];
        EMIT(OP_VARIABLE, spirv_pointer_type(e, root->type, 4u), root->target, 4u);
    }
    for (uint32_t i=0u;i<c->entry_input_count;++i)
        EMIT(OP_VARIABLE,input_scalar(c->entry_inputs[i].kind)?ID_PTR_INPUT_U32:ID_PTR_INPUT,c->entry_inputs[i].target,1u);
    for (uint32_t i=0u;i<c->entry_count;++i)
        if (spirv_hidden_initialization_input(c,&c->entries[i])) {
            EMIT(OP_VARIABLE,ID_PTR_INPUT_U32,c->entries[i].state.resolved.local_index_target,1u);
        }
    return spirv_stage_variables(e);
}

#undef EMIT
