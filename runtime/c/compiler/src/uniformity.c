/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include "stage.h"
#include <string.h>

/* One Boolean sensitivity run represents reachability from one distinguished
 * source of the dated WGSL uniformity graph. No source numerics are executed. */
enum { SUMMARY_REQUIRED=1u, SUMMARY_RETURN=2u };
enum { MAP_INCOMING, MAP_FIRST, MAP_HEAD, MAP_CONTINUE, MAP_BREAK, MAP_TEMP, MAP_COUNT };
enum { FRAME_BREAK=1u, FRAME_CONTINUE=2u, FRAME_SWITCH=4u };
typedef struct {
    uint32_t statement, parent_loop, incoming_cf, first_cf, head_cf, break_cf, continue_cf, flags;
} UniformityFrame;
typedef struct {
    Source *source;
    CompilerWorkspace *compiler;
    LocalScope scope;
    UniformityBudget budget;
    unsigned char *expression_bits, *current_local, *formal_summary, *function_summary, *frame_maps;
    UniformityFrame *frames;
    size_t local_slots, frame_capacity;
    uint32_t frame_count, loop, seed, cf, required, returned, required_offset;
    bool subgroup, pointers;
    size_t pointer_bytes;
    uint32_t *pointer_offsets;
    unsigned char *pointer_summaries, *pointer_returns, *pointer_state, *pointer_frame_maps;
} Uniformity;

bool uniformity_charge(Source *source,UniformityBudget *budget,uint64_t count) {
    if (count>budget->limit-budget->used)
        return source_error(source,DOE_COMPILER_UNSUPPORTED,budget->offset,"uniformity analysis work limit exceeded");
    budget->used+=count; return true;
}
static bool charge(Uniformity *a,uint64_t count) {return uniformity_charge(a->source,&a->budget,count);}
static bool reset_bytes(Uniformity *a,unsigned char *to,size_t count) {
    if (!charge(a,count)) return false;
    memset(to,0,count); return true;
}
static unsigned char *pointer_map(const Uniformity *a,const unsigned char *map) {
    if (map == a->current_local) return a->pointer_state;
    return a->pointer_frame_maps+(size_t)(map-a->frame_maps)/a->local_slots*a->compiler->formal_count;
}
static bool copy_map(Uniformity *a,unsigned char *to,const unsigned char *from) {
    if (!charge(a,a->local_slots)) return false;
    memcpy(to,from,a->local_slots);
    if (a->pointers) {
        if (!charge(a,a->compiler->formal_count)) return false;
        memcpy(pointer_map(a,to),pointer_map(a,from),a->compiler->formal_count);
    }
    return true;
}
static bool join_map(Uniformity *a,unsigned char *to,const unsigned char *from) {
    for (size_t i=0u;i<a->local_slots;++i) {
        if (!charge(a,1u)) return false;
        to[i]|=from[i];
    }
    if (a->pointers) {
        unsigned char *target=pointer_map(a,to), *source=pointer_map(a,from);
        for (uint32_t i=0u;i<a->compiler->formal_count;++i) {
            if (!charge(a,1u)) return false;
            target[i] |= source[i];
        }
    }
    return true;
}
static bool equal_map(Uniformity *a,const unsigned char *x,const unsigned char *y,bool *same) {
    *same=true;
    for (size_t i=0u;i<a->local_slots;++i) {
        if (!charge(a,1u)) return false;
        if (x[i]!=y[i]) *same=false;
    }
    if (a->pointers) {
        const unsigned char *left=pointer_map(a,x), *right=pointer_map(a,y);
        for (uint32_t i=0u;i<a->compiler->formal_count;++i) {
            if (!charge(a,1u)) return false;
            if (left[i] != right[i]) *same=false;
        }
    }
    return true;
}
static unsigned char *frame_map(Uniformity *a,uint32_t frame,uint32_t map) {
    return a->frame_maps+((size_t)frame*MAP_COUNT+map)*a->local_slots;
}
static bool enter_frame(Uniformity *a,uint32_t statement,uint32_t *index) {
    if (a->frame_count>=a->frame_capacity)
        return source_error(a->source,DOE_COMPILER_INPUT_ERROR,a->budget.offset,"uniformity control scratch capacity exceeded");
    if (!charge(a,8u)) return false;
    *index=a->frame_count++;
    a->frames[*index]=(UniformityFrame){.statement=statement,.parent_loop=a->loop,.incoming_cf=a->cf};
    return true;
}
static bool require_uniform(Uniformity *a,uint32_t bit,uint32_t offset) {
    if (!charge(a,1u)) return false;
    if (bit!=0u && a->required==0u) a->required_offset=offset;
    a->required|=bit; return true;
}
static uint32_t varying(const Uniformity *a) {return a->seed==NO_ID?1u:0u;}
static bool uniform_input(const Uniformity *a, InputKind kind) {
    return kind == INPUT_WORKGROUP || kind == INPUT_NUM_WORKGROUPS ||
        kind == INPUT_SUBGROUP_SIZE || kind == INPUT_NUM_SUBGROUPS ||
        (a->subgroup && kind == INPUT_SUBGROUP_ID);
}
static bool name_bit(Uniformity *a,NameSpan name,uint32_t cf,uint32_t *out) {
    ValueIdentity identity;
    if (!sema_value_identity(a->source,&a->scope,name,&a->budget,&identity) || !charge(a,1u)) return false;
    *out=cf;
    switch (identity.kind) {
        case IDENTITY_LOCAL:
            *out|=a->current_local[a->scope.values[identity.index].local_id]; return true;
        case IDENTITY_FORMAL:
            *out|=a->seed==(a->pointers?2u*identity.index:identity.index)?1u:0u; return true;
        case IDENTITY_CONSTANT: return true;
        case IDENTITY_ALIAS:
            /* The fixed adapter's compact alias denotes a validated input x. */
            for (uint32_t i=0u;i<a->compiler->entry_input_count;++i) {
                if (!charge(a,1u)) return false;
                if (source_spans_equal(a->source,a->compiler->alias_source,a->compiler->entry_inputs[i].name)) {
                    const InputKind kind=a->compiler->entry_inputs[i].kind;
                    *out=uniform_input(a,kind)?cf:varying(a); return true;
                }
            }
            break;
        case IDENTITY_INPUT: {
            const InputKind kind=a->compiler->entry_inputs[identity.index].kind;
            *out=uniform_input(a,kind)?cf:varying(a); return true;
        }
        case IDENTITY_RESOURCE:
            if (!type_handle(a->compiler, a->compiler->resources[identity.index].type) &&
                a->compiler->resources[identity.index].access==DOE_COMPILER_READ_WRITE) *out=varying(a);
            return true;
        case IDENTITY_STAGE_INPUT:
            if (a->source->stages != NULL) {
                for (uint32_t i = 0u; i < a->source->stages->interface_count; ++i) {
                    if (!charge(a,1u)) return false;
                    const StageInterface *leaf = &a->source->stages->interfaces[i];
                    if (leaf->formal == identity.index && leaf->member == NO_ID &&
                        leaf->builtin == DOE_COMPILER_BUILTIN_SUBGROUP_SIZE) return true;
                }
            }
            *out=varying(a); return true;
        case IDENTITY_WORKGROUP:
            if (a->compiler->workgroups[identity.index].state.resolved.address_space != MODULE_IMMEDIATE) *out=varying(a);
            return true;
        case IDENTITY_NONE: break;
    }
    return source_error(a->source,DOE_COMPILER_INPUT_ERROR,name.offset,"validated uniformity value identity is missing");
}
static bool expression_bit(Uniformity *,uint32_t,uint32_t,uint32_t *);
/* Resolve already-validated pointer identities without evaluating indices a
 * second time. Local roots here are source statement identities, deliberately
 * distinct from the later target-local numbering. */
static bool pointer_name(Uniformity *a, NameSpan name, bool reference, PointerValue *out, bool *pointer) {
    ValueIdentity identity;
    if (!sema_value_identity(a->source,&a->scope,name,&a->budget,&identity)) return false;
    *pointer = false;
    if (identity.kind == IDENTITY_LOCAL) {
        const LocalBinding *local = &a->scope.values[identity.index];
        *pointer = local->value == 1u;
        if (*pointer) { *out = local->pointer_value; return true; }
        if (reference) { *out = (PointerValue){.category=ROOT_LOCAL,.root=local->local_id,.path=NO_ID}; return true; }
    } else if (identity.kind == IDENTITY_FORMAL &&
        type_pointer(a->compiler,a->compiler->formals[identity.index].type.resolved.type)) {
        *pointer = true;
        *out = (PointerValue){.category=ROOT_POINTER,.root=identity.index,.path=NO_ID}; return true;
    } else if (reference && (identity.kind == IDENTITY_RESOURCE || identity.kind == IDENTITY_WORKGROUP)) {
        *out = (PointerValue){.category=identity.kind == IDENTITY_RESOURCE ? ROOT_RESOURCE : ROOT_WORKGROUP,
            .root=identity.index,.path=NO_ID}; return true;
    }
    return true;
}
static bool pointer_origin(Uniformity *a, uint32_t id, bool reference, PointerValue *out) {
    bool partial = false;
    *out = (PointerValue){.category=ROOT_VALUE,.path=NO_ID};
    for (;;) {
        if (!charge(a,1u)) return false;
        const Expression *e = &a->compiler->expressions[id];
        if (e->kind == EXPRESSION_ADDRESS) { reference = true; id = e->left; continue; }
        if (e->kind == EXPRESSION_DEREFERENCE) { reference = false; id = e->left; continue; }
        NameSpan name = {0};
        if (e->kind == EXPRESSION_NAME) name = e->value.name;
        else if (reference && (e->kind == EXPRESSION_LOAD || e->kind == EXPRESSION_MEMBER)) {
            partial = true;
            if (e->left != NO_ID) { id = e->left; continue; }
            name = e->kind == EXPRESSION_LOAD ? e->value.reference.storage : e->value.member.object;
        } else return source_error(a->source,DOE_COMPILER_INPUT_ERROR,e->source_offset,"validated pointer uniformity identity is missing");
        bool pointer;
        if (!pointer_name(a,name,reference,out,&pointer)) return false;
        if (out->category == ROOT_VALUE)
            return source_error(a->source,DOE_COMPILER_INPUT_ERROR,e->source_offset,"validated pointer uniformity root is missing");
        if (partial) out->path = 0u;
        return true;
    }
}
static uint32_t pointer_content(const Uniformity *a, PointerValue pointer) {
    if (pointer.category == ROOT_LOCAL) return a->current_local[pointer.root];
    if (pointer.category == ROOT_POINTER) return a->pointer_state[pointer.root];
    if (pointer.category == ROOT_RESOURCE)
        return a->compiler->resources[pointer.root].access == DOE_COMPILER_READ_WRITE ? varying(a) : 0u;
    if (pointer.category == ROOT_WORKGROUP)
        return a->compiler->workgroups[pointer.root].state.resolved.address_space == MODULE_IMMEDIATE ? 0u : varying(a);
    return 0u;
}
static void pointer_write(Uniformity *a, PointerValue pointer, uint32_t bit, bool compound) {
    if (pointer.category != ROOT_LOCAL && pointer.category != ROOT_POINTER) return;
    unsigned char *slot = pointer.category == ROOT_LOCAL ? &a->current_local[pointer.root] : &a->pointer_state[pointer.root];
    if (compound || pointer.path != NO_ID) bit |= *slot;
    *slot = (unsigned char)bit;
}
static bool address_bits(Uniformity *a, uint32_t id, uint32_t cf, uint32_t *out) {
    *out = cf;
    while (id != NO_ID) {
        if (!charge(a,1u)) return false;
        const Expression *e = &a->compiler->expressions[id];
        uint32_t part;
        if (e->kind == EXPRESSION_DEREFERENCE) {
            if (!expression_bit(a,e->left,cf,&part)) return false;
            *out |= part; return true;
        }
        if (e->kind == EXPRESSION_LOAD) {
            if (!expression_bit(a,e->value.reference.index,cf,&part)) return false;
            *out |= part;
        }
        if (e->kind == EXPRESSION_NAME || e->left == NO_ID) {
            const NameSpan name = e->kind == EXPRESSION_NAME ? e->value.name :
                e->kind == EXPRESSION_MEMBER ? e->value.member.object : e->value.reference.storage;
            PointerValue origin = {.category=ROOT_VALUE}; bool pointer;
            if (!pointer_name(a,name,true,&origin,&pointer)) return false;
            if (pointer) { if (!name_bit(a,name,cf,&part)) return false; *out |= part; }
            return true;
        }
        id = e->left;
    }
    return true;
}
static unsigned char *pointer_summary(Uniformity *a, uint32_t function, uint32_t run) {
    return a->pointer_summaries+a->pointer_offsets[function]+(size_t)run*(a->compiler->functions[function].formal_count+1u);
}
static bool pointer_return(Uniformity *a) {
    const FunctionRecord *f = &a->compiler->functions[a->compiler->current_function];
    for (uint32_t i = 0u; i < f->formal_count; ++i) {
        if (!charge(a,1u)) return false;
        if (type_pointer(a->compiler,a->compiler->formals[f->formal_start+i].type.resolved.type))
            a->pointer_returns[f->formal_start+i] |= a->pointer_state[f->formal_start+i];
    }
    return true;
}
static bool pointer_call(Uniformity *a, uint32_t id, uint32_t function, uint32_t cf, uint32_t *out) {
    const Expression *e = &a->compiler->expressions[id];
    const FunctionRecord *fn = &a->compiler->functions[function];
    uint32_t link = e->left;
    /* Evaluate every argument once before observing any pointee contents. */
    for (uint32_t i = 0u; i < e->right; ++i) {
        uint32_t bit;
        if (!expression_bit(a,a->compiler->arguments[link].expression,cf,&bit)) return false;
        link = a->compiler->arguments[link].next;
    }
    uint32_t required = 0u;
    *out = cf;
    for (uint32_t run = 0u; run < 2u*fn->formal_count+2u; ++run) {
        if (!charge(a,1u)) return false;
        uint32_t bit = run == 0u ? varying(a) : cf;
        if (run >= 2u) {
            const uint32_t parameter = (run-2u)/2u;
            link = e->left;
            for (uint32_t j = 0u; j < parameter; ++j) {
                if (!charge(a,1u)) return false;
                link = a->compiler->arguments[link].next;
            }
            const uint32_t expression = a->compiler->arguments[link].expression;
            bit = a->expression_bits[expression] & 1u;
            if ((run & 1u) != 0u) {
                bit = 0u;
                if (type_pointer(a->compiler,a->compiler->formals[fn->formal_start+parameter].type.resolved.type)) {
                    PointerValue pointer;
                    if (!pointer_origin(a,expression,false,&pointer)) return false;
                    bit = pointer_content(a,pointer);
                }
            }
        }
        const unsigned char *summary = pointer_summary(a,function,run);
        if ((summary[0] & SUMMARY_REQUIRED) != 0u) required |= bit;
        if ((summary[0] & SUMMARY_RETURN) != 0u) *out |= bit;
        link = e->left;
        for (uint32_t j = 0u; j < fn->formal_count; ++j) {
            if (!charge(a,1u)) return false;
            const uint32_t expression = a->compiler->arguments[link].expression;
            if (run == 0u) a->expression_bits[expression] &= 1u;
            if (summary[j+1u] != 0u) a->expression_bits[expression] |= (unsigned char)(bit<<1u);
            link = a->compiler->arguments[link].next;
        }
    }
    if (!require_uniform(a,required,e->source_offset)) return false;
    link = e->left;
    for (uint32_t i = 0u; i < fn->formal_count; ++i) {
        const uint32_t expression = a->compiler->arguments[link].expression;
        if (type_pointer(a->compiler,a->compiler->formals[fn->formal_start+i].type.resolved.type)) {
            PointerValue pointer;
            if (!pointer_origin(a,expression,false,&pointer)) return false;
            pointer_write(a,pointer,a->expression_bits[expression]>>1u,false);
        }
        a->expression_bits[expression] &= 1u;
        link = a->compiler->arguments[link].next;
    }
    return true;
}

static bool call_bit(Uniformity *a,uint32_t id,uint32_t cf,uint32_t *out) {
    const Expression *e=&a->compiler->expressions[id];
    if (!charge(a,1u)) return false;
    const SourceReference reference=a->compiler->expression_references[id];
    const uint32_t callee=reference.kind==SOURCE_FUNCTION?reference.index+1u:0u;
    *out=cf;
    if (reference.kind == SOURCE_ATOMIC) *out |= varying(a);
    if (reference.kind == SOURCE_IMAGE && (reference.index & SOURCE_IMAGE_VARYING) != 0u) *out |= varying(a);
    if ((reference.kind == SOURCE_DERIVATIVE ||
        (reference.kind == SOURCE_IMAGE && (reference.index & SOURCE_IMAGE_DERIVATIVE) != 0u)) &&
        !a->subgroup && !require_uniform(a,cf,e->source_offset)) return false;
    if (reference.kind==SOURCE_BARRIER) {
        if (!a->subgroup && !require_uniform(a,cf,e->source_offset)) return false;
        if (e->right != 0u) {
            uint32_t address;
            if (!expression_bit(a,a->compiler->arguments[e->left].expression,cf,&address)) return false;
            if (!a->subgroup && !require_uniform(a,address,e->source_offset)) return false;
        }
        return true;
    }
    const NumericBuiltin *collective = reference.kind == SOURCE_SUBGROUP ?
        builtin_kind((NumericBuiltinKind)reference.index) : NULL;
    if (collective != NULL) {
        if ((a->subgroup || collective->kind >= BUILTIN_QUAD_BROADCAST) && !require_uniform(a, cf, e->source_offset)) return false;
        if (!a->subgroup || !builtin_subgroup_uniform(collective)) *out |= varying(a);
    }
    const FunctionRecord *function=callee==0u?NULL:&a->compiler->functions[callee-1u];
    if (function != NULL && a->pointers) return pointer_call(a,id,callee-1u,cf,out);
    if (function!=NULL) {
        if (!charge(a,1u)) return false;
        const uint32_t summary=a->function_summary[callee-1u];
        if ((summary&SUMMARY_REQUIRED)!=0u && !require_uniform(a,cf,e->source_offset)) return false;
        if ((summary&SUMMARY_RETURN)!=0u) *out|=varying(a);
    }
    uint32_t link=e->left;
    for (uint32_t i=0u;i<e->right;++i) {
        if (!charge(a,1u)) return false;
        const ArgumentSyntax arg=a->compiler->arguments[link];
        uint32_t bit;
        if (!expression_bit(a,arg.expression,cf,&bit)) return false;
        if (collective != NULL && a->subgroup && i == 1u &&
            (collective->kind == BUILTIN_SUBGROUP_SHUFFLE_XOR || collective->kind == BUILTIN_SUBGROUP_SHUFFLE_UP ||
             collective->kind == BUILTIN_SUBGROUP_SHUFFLE_DOWN) && !require_uniform(a, bit, e->source_offset)) return false;
        if (function==NULL) {
            /* WGSL 15.2.7: subgroup reduction function tags may be uniform,
             * but their default parameter-return dependency still applies. */
            *out|=bit;
        }
        else {
            if (!charge(a,1u)) return false;
            const uint32_t summary=a->formal_summary[function->formal_start+i];
            if ((summary&SUMMARY_REQUIRED)!=0u && !require_uniform(a,bit,e->source_offset)) return false;
            if ((summary&SUMMARY_RETURN)!=0u) *out|=bit;
        }
        link=arg.next;
    }
    return true;
}
static bool expression_bit(Uniformity *a,uint32_t id,uint32_t cf,uint32_t *out) {
    const Expression *e=&a->compiler->expressions[id];
    a->budget.offset=e->source_offset;
    if (!charge(a,1u)) return false;
    uint32_t left=cf,right=cf;
    switch (e->kind) {
        case EXPRESSION_NAME:
            if (!name_bit(a,e->value.name,cf,&left)) return false;
            break;
        case EXPRESSION_MEMBER:
        case EXPRESSION_LOAD:
            if (e->left==NO_ID) {
                if (!name_bit(a,e->kind==EXPRESSION_MEMBER?e->value.member.object:e->value.reference.storage,cf,&left)) return false;
            } else if (!expression_bit(a,e->left,cf,&left)) return false;
            if (a->pointers) {
                bool pointer = false; PointerValue origin = {.category=ROOT_VALUE};
                if (e->left == NO_ID) {
                    if (!pointer_name(a,e->kind == EXPRESSION_MEMBER ? e->value.member.object : e->value.reference.storage,
                        false,&origin,&pointer)) return false;
                } else if (a->compiler->expressions[e->left].kind == EXPRESSION_NAME) {
                    if (!pointer_name(a,a->compiler->expressions[e->left].value.name,false,&origin,&pointer)) return false;
                }
                if (pointer) left |= pointer_content(a,origin);
            }
            if (e->kind==EXPRESSION_LOAD) {
                if (!expression_bit(a,e->value.reference.index,cf,&right)) return false;
                left|=right;
            }
            break;
        case EXPRESSION_CALL:
            if (!call_bit(a,id,cf,&left)) return false;
            break;
        case EXPRESSION_NOT: case EXPRESSION_NEGATE: case EXPRESSION_COMPLEMENT:
        case EXPRESSION_CONSTRUCT: case EXPRESSION_BITCAST:
            if (!expression_bit(a,e->left,cf,&left)) return false;
            break;
        case EXPRESSION_DEREFERENCE:
            if (!expression_bit(a,e->left,cf,&left)) return false;
            if (a->pointers) {
                PointerValue origin;
                if (!pointer_origin(a,e->left,false,&origin)) return false;
                left |= pointer_content(a,origin);
            }
            break;
        case EXPRESSION_AND: case EXPRESSION_OR:
            if (!expression_bit(a,e->left,cf,&left) || !expression_bit(a,e->right,left,&right)) return false;
            left=right;
            break;
        case EXPRESSION_ADD: case EXPRESSION_SUBTRACT: case EXPRESSION_MULTIPLY:
        case EXPRESSION_MAX: case EXPRESSION_COMPARE: case EXPRESSION_DIVIDE:
        case EXPRESSION_REMAINDER: case EXPRESSION_BIT_AND: case EXPRESSION_BIT_OR:
        case EXPRESSION_BIT_XOR: case EXPRESSION_SHIFT_LEFT: case EXPRESSION_SHIFT_RIGHT:
            if (!expression_bit(a,e->left,cf,&left) || !expression_bit(a,e->right,cf,&right)) return false;
            left|=right;
            break;
        case EXPRESSION_ADDRESS:
            if (!address_bits(a,e->left,cf,&left)) return false;
            break;
        case EXPRESSION_INTEGER: case EXPRESSION_DECIMAL: case EXPRESSION_ABSTRACT_INTEGER:
        case EXPRESSION_BOOLEAN: case EXPRESSION_LITERAL: break;
    }
    if (!charge(a,1u)) return false;
    a->expression_bits[id]=(unsigned char)left;
    *out=left; return true;
}
/* LHS syntax is a validated reference path, not a load of its old root. */
static bool assignment_bit(Uniformity *a,const Assignment *assignment) {
    uint32_t bits=a->cf,part;
    if (assignment->kind == TARGET_ROOT && source_span_is(a->source, assignment->root, "_"))
        return expression_bit(a, assignment->expression, a->cf, &part);
    if (assignment->kind==TARGET_INDEX) {
        if (!expression_bit(a,assignment->index,a->cf,&part)) return false;
        bits|=part;
    }
    for (uint32_t path=a->pointers && assignment->kind == TARGET_REFERENCE ? NO_ID : assignment->path;path!=NO_ID;path=a->compiler->expressions[path].left) {
        if (!charge(a,1u)) return false;
        const Expression *e=&a->compiler->expressions[path];
        if (e->kind==EXPRESSION_LOAD) {
            if (!expression_bit(a,e->value.reference.index,a->cf,&part)) return false;
            bits|=part;
        }
    }
    ValueIdentity identity = {.kind=IDENTITY_NONE};
    PointerValue target = {.category=ROOT_VALUE,.path=NO_ID};
    if (a->pointers && assignment->kind == TARGET_REFERENCE) {
        if (!pointer_origin(a,assignment->path,true,&target) ||
            !address_bits(a,assignment->path,a->cf,&part)) return false;
        bits |= part;
    } else {
        if (!sema_value_identity(a->source,&a->scope,assignment->root,&a->budget,&identity)) return false;
        if (a->pointers) {
            bool pointer;
            if (!pointer_name(a,assignment->root,true,&target,&pointer)) return false;
            if (pointer) {
                if (!name_bit(a,assignment->root,a->cf,&part)) return false;
                bits |= part;
            }
            if (assignment->kind != TARGET_ROOT) target.path = 0u;
        }
    }
    if (assignment->expression!=NO_ID) {
        if (!expression_bit(a,assignment->expression,a->cf,&part)) return false;
        bits|=part;
    }
    if (a->pointers && target.category != ROOT_VALUE) {
        pointer_write(a,target,bits,assignment->operation != UPDATE_SET);
    } else if (identity.kind==IDENTITY_LOCAL) {
        const uint32_t local=a->scope.values[identity.index].local_id;
        if (!charge(a,1u)) return false;
        if (assignment->kind!=TARGET_ROOT || assignment->operation!=UPDATE_SET) bits|=a->current_local[local];
        a->current_local[local]=(unsigned char)bits;
    }
    return true;
}
static bool local_bit(Uniformity *a,uint32_t statement) {
    const LocalSyntax *local=&a->compiler->statements[statement].value.local;
    uint32_t bit=a->cf;
    if ((local->flags&LOCAL_INITIALIZED)!=0u && !expression_bit(a,local->expression,a->cf,&bit)) return false;
    if (!charge(a,2u)) return false;
    a->current_local[statement]=(unsigned char)bit;
    LocalBinding binding = {.name=local->name,.local_id=statement,.flags=local->flags};
    if (a->pointers && (local->flags & LOCAL_INITIALIZED) != 0u) {
        const Expression *e = &a->compiler->expressions[local->expression];
        bool pointer = e->kind == EXPRESSION_ADDRESS;
        PointerValue origin = {.category=ROOT_VALUE};
        if (e->kind == EXPRESSION_NAME && !pointer_name(a,e->value.name,false,&origin,&pointer)) return false;
        if (pointer) {
            if (!pointer_origin(a,local->expression,false,&binding.pointer_value)) return false;
            binding.value = 1u;
        }
    }
    a->scope.values[a->scope.count++]=binding;
    return true;
}
static bool block_bits(Uniformity *,uint32_t,uint32_t,bool,bool *);
static bool if_bits(Uniformity *a,uint32_t statement) {
    const Statement *st=&a->compiler->statements[statement];
    uint32_t frame,test;
    if (!enter_frame(a,statement,&frame) || !copy_map(a,frame_map(a,frame,MAP_INCOMING),a->current_local) ||
        !expression_bit(a,st->value.condition,a->cf,&test)) return false;
    bool yes,no;
    a->cf=test;
    if (!block_bits(a,st->body_start,st->else_start,false,&yes)) return false;
    a->frames[frame].first_cf=a->cf;
    if (yes && !copy_map(a,frame_map(a,frame,MAP_FIRST),a->current_local)) return false;
    if (!copy_map(a,a->current_local,frame_map(a,frame,MAP_INCOMING))) return false;
    a->cf=test;
    if (!block_bits(a,st->else_start,st->end,false,&no)) return false;
    if (yes) {
        if (no) {if (!join_map(a,a->current_local,frame_map(a,frame,MAP_FIRST))) return false;}
        else if (!copy_map(a,a->current_local,frame_map(a,frame,MAP_FIRST))) return false;
    }
    a->cf=(a->compiler->statement_behaviors[statement]&15u)==BEHAVIOR_NEXT?
        a->frames[frame].incoming_cf:a->cf|a->frames[frame].first_cf;
    --a->frame_count; return true;
}
static bool loop_exit(Uniformity *a,bool continuing) {
    uint32_t target = a->loop;
    if (continuing)
        while (target != NO_ID && (a->frames[target].flags & FRAME_SWITCH) != 0u)
            target = a->frames[target].parent_loop;
    if (target==NO_ID)
        return source_error(a->source,DOE_COMPILER_INPUT_ERROR,a->budget.offset,"validated uniformity loop target is missing");
    UniformityFrame *frame=&a->frames[target];
    const uint32_t flag=continuing?FRAME_CONTINUE:FRAME_BREAK, map=continuing?MAP_CONTINUE:MAP_BREAK;
    if ((frame->flags&flag)!=0u) {
        if (!join_map(a,frame_map(a,target,map),a->current_local)) return false;
    } else if (!copy_map(a,frame_map(a,target,map),a->current_local)) return false;
    frame->flags|=flag;
    if (continuing) frame->continue_cf|=a->cf; else frame->break_cf|=a->cf;
    return true;
}
static bool switch_bits(Uniformity *a,uint32_t statement) {
    const Statement *st = &a->compiler->statements[statement];
    uint32_t frame, test;
    if (!enter_frame(a, statement, &frame) ||
        !copy_map(a, frame_map(a, frame, MAP_INCOMING), a->current_local) ||
        !expression_bit(a, st->value.condition, a->cf, &test)) return false;
    a->frames[frame].flags |= FRAME_SWITCH;
    a->loop = frame;
    uint32_t outgoing = 0u;
    for (uint32_t clause = st->body_start; clause < st->end; clause = a->compiler->statements[clause].end) {
        if (!charge(a, 1u) || !copy_map(a, a->current_local, frame_map(a, frame, MAP_INCOMING))) return false;
        const Statement *part = &a->compiler->statements[clause];
        a->cf = test;
        bool next;
        if (!block_bits(a, part->body_start, part->end, false, &next)) return false;
        outgoing |= a->cf;
        if (next && !loop_exit(a, false)) return false;
    }
    if ((a->frames[frame].flags & FRAME_BREAK) != 0u &&
        !copy_map(a, a->current_local, frame_map(a, frame, MAP_BREAK))) return false;
    const uint32_t behavior = a->compiler->statement_behaviors[statement] & 15u;
    a->cf = behavior == BEHAVIOR_NEXT ? a->frames[frame].incoming_cf : outgoing;
    a->loop = a->frames[frame].parent_loop;
    --a->frame_count;
    return true;
}
static bool loop_bits(Uniformity *a,uint32_t statement) {
    const Statement *st=&a->compiler->statements[statement];
    const size_t saved_scope=a->scope.count;
    bool ignored;
    if (st->kind==STATEMENT_FOR && st->initializer!=NO_ID &&
        !block_bits(a,st->initializer,st->initializer+1u,true,&ignored)) return false;
    const size_t body_scope=a->scope.count;
    uint32_t frame;
    if (!enter_frame(a,statement,&frame)) return false;
    a->loop=frame;
    if (!copy_map(a,frame_map(a,frame,MAP_INCOMING),a->current_local) ||
        !copy_map(a,frame_map(a,frame,MAP_HEAD),a->current_local)) return false;
    a->frames[frame].head_cf=a->cf;
    uint32_t continuing=st->end;
    for (uint32_t i=st->body_start;i<st->end;i=a->compiler->statements[i].end) {
        if (!charge(a,1u)) return false;
        if (a->compiler->statements[i].kind==STATEMENT_CONTINUING) continuing=i;
    }
    const bool has_test=st->kind==STATEMENT_WHILE || (st->kind==STATEMENT_FOR && st->value.condition!=NO_ID);
    const uint32_t body_behavior=a->compiler->statement_behaviors[statement]>>4u;
    const bool backedge=(body_behavior&(BEHAVIOR_NEXT|BEHAVIOR_CONTINUE))!=0u;
    for (;;) {
        a->scope.count=body_scope;
        a->frames[frame].flags&=~(uint32_t)FRAME_CONTINUE;
        a->frames[frame].continue_cf=0u;
        if (!copy_map(a,a->current_local,frame_map(a,frame,MAP_HEAD))) return false;
        a->cf=a->frames[frame].head_cf;
        if (has_test) {
            if (!expression_bit(a,st->value.condition,a->cf,&a->cf) || !loop_exit(a,false)) return false;
        }
        bool body_next;
        if (!block_bits(a,st->body_start,continuing,true,&body_next)) return false;
        a->frames[frame].first_cf=a->cf;
        if (backedge) {
            if ((a->frames[frame].flags&FRAME_CONTINUE)!=0u) {
                if (body_next) {if (!join_map(a,a->current_local,frame_map(a,frame,MAP_CONTINUE))) return false;}
                else if (!copy_map(a,a->current_local,frame_map(a,frame,MAP_CONTINUE))) return false;
            }
            if (st->kind==STATEMENT_FOR) {
                a->scope.count=body_scope;
                if (st->update!=NO_ID && !block_bits(a,st->update,st->update+1u,true,&ignored)) return false;
            } else if (continuing!=st->end) {
                const Statement *part=&a->compiler->statements[continuing];
                if (!block_bits(a,part->body_start,part->end,true,&ignored)) return false;
            }
            if (!copy_map(a,frame_map(a,frame,MAP_TEMP),frame_map(a,frame,MAP_HEAD)) ||
                !join_map(a,frame_map(a,frame,MAP_HEAD),a->current_local)) return false;
            bool stable;
            if (!equal_map(a,frame_map(a,frame,MAP_TEMP),frame_map(a,frame,MAP_HEAD),&stable)) return false;
            const uint32_t head=a->frames[frame].incoming_cf|a->cf;
            stable=stable && head==a->frames[frame].head_cf;
            a->frames[frame].head_cf=head;
            if (!stable) continue;
        }
        break;
    }
    if ((a->frames[frame].flags&FRAME_BREAK)!=0u && !copy_map(a,a->current_local,frame_map(a,frame,MAP_BREAK))) return false;
    const uint32_t behavior=a->compiler->statement_behaviors[statement]&15u;
    a->cf=behavior==BEHAVIOR_NEXT?a->frames[frame].incoming_cf:
        backedge?a->frames[frame].head_cf:a->frames[frame].first_cf;
    a->loop=a->frames[frame].parent_loop;
    a->scope.count=saved_scope;
    --a->frame_count; return true;
}
static bool statement_bits(Uniformity *a,uint32_t id) {
    const Statement *st=&a->compiler->statements[id];
    a->budget.offset=st->source_offset;
    if (!charge(a,1u)) return false;
    uint32_t bit;
    bool ignored;
    switch (st->kind) {
        case STATEMENT_ASSIGN: return assignment_bit(a,&st->value.assignment);
        case STATEMENT_VAR: case STATEMENT_LET: case STATEMENT_CONST: return local_bit(a,id);
        case STATEMENT_CALL: return expression_bit(a,st->value.condition,a->cf,&bit);
        case STATEMENT_RETURN:
            bit=a->cf;
            if (st->value.condition!=NO_ID && !expression_bit(a,st->value.condition,a->cf,&bit)) return false;
            a->returned|=bit;
            return !a->pointers || pointer_return(a);
        case STATEMENT_BREAK: return loop_exit(a,false);
        case STATEMENT_CONTINUE: return loop_exit(a,true);
        case STATEMENT_BREAK_IF:
            return expression_bit(a,st->value.condition,a->cf,&a->cf) && loop_exit(a,false);
        case STATEMENT_IF: return if_bits(a,id);
        case STATEMENT_SWITCH: return switch_bits(a,id);
        case STATEMENT_CASE:
            return source_error(a->source, DOE_COMPILER_INPUT_ERROR, st->source_offset, "unexpected standalone case header");
        case STATEMENT_BLOCK: return block_bits(a,st->body_start,st->end,false,&ignored);
        case STATEMENT_FOR: case STATEMENT_WHILE: case STATEMENT_LOOP: return loop_bits(a,id);
        case STATEMENT_CONTINUING:
            return source_error(a->source,DOE_COMPILER_INPUT_ERROR,st->source_offset,"unexpected validated continuing statement");
        case STATEMENT_DISCARD: /* Helper demotion is not a control-flow exit. */
        case STATEMENT_EMPTY: return true;
    }
    return false;
}
static bool block_bits(Uniformity *a,uint32_t start,uint32_t end,bool retain,bool *next) {
    const size_t saved=a->scope.count;
    *next=true;
    for (uint32_t i=start;i<end && *next;i=a->compiler->statements[i].end) {
        if (!statement_bits(a,i) || !charge(a,1u)) return false;
        *next=(a->compiler->statement_behaviors[i]&BEHAVIOR_NEXT)!=0u;
    }
    if (!retain) a->scope.count=saved;
    return true;
}
static bool function_bits(Uniformity *a,uint32_t index) {
    const FunctionRecord *f=&a->compiler->functions[index];
    a->compiler->current_function=index;
    for (uint32_t run=0u;run<(a->pointers?2u:1u)*f->formal_count+2u;++run) {
        a->budget.offset=f->name.offset;
        a->scope.count=0u; a->frame_count=0u; a->loop=NO_ID;
        a->seed=run==0u?NO_ID:run==1u?NO_ID-1u:(a->pointers?2u:1u)*f->formal_start+run-2u;
        a->cf=run==1u?1u:0u; a->required=0u; a->returned=0u; a->required_offset=f->name.offset;
        if (!reset_bytes(a,a->expression_bits,a->compiler->limits.physical_expressions) ||
            !reset_bytes(a,a->current_local,a->local_slots) || !charge(a,3u)) return false;
        if (a->pointers) {
            if (!reset_bytes(a,a->pointer_returns,a->compiler->formal_count)) return false;
            for (uint32_t i=0u;i<f->formal_count;++i) {
                if (!charge(a,1u)) return false;
                a->pointer_state[f->formal_start+i] =
                    (unsigned char)(a->seed == 2u*(f->formal_start+i)+1u ? 1u : 0u);
            }
        }
        bool next;
        if (!block_bits(a,f->statement_start,f->statement_end,true,&next)) return false;
        if (a->pointers) {
            if (next && !pointer_return(a)) return false;
            unsigned char *summary = pointer_summary(a,index,run);
            summary[0] = (unsigned char)((a->required != 0u ? SUMMARY_REQUIRED : 0u) |
                (a->returned != 0u ? SUMMARY_RETURN : 0u));
            for (uint32_t i=0u;i<f->formal_count;++i) summary[i+1u] = a->pointer_returns[f->formal_start+i];
        }
        if (run==0u) {
            if (a->required!=0u)
                return source_error(a->source,DOE_COMPILER_SEMANTIC_ERROR,a->required_offset,
                    a->subgroup ? "subgroup uniform control cannot be established" :
                    "uniform control cannot be established");
            if (a->returned!=0u) a->function_summary[index]|=SUMMARY_RETURN;
        } else if (run==1u) {
            if (a->required!=0u) a->function_summary[index]|=SUMMARY_REQUIRED;
        } else if (!a->pointers) a->formal_summary[f->formal_start+run-2u]=(unsigned char)(
            (a->required!=0u?SUMMARY_REQUIRED:0u)|(a->returned!=0u?SUMMARY_RETURN:0u));
    }
    return true;
}
static bool module_bits(Uniformity *a) {
    CompilerWorkspace *c=a->compiler;
    if (!reset_bytes(a,a->function_summary,c->limits.tokens/6u) ||
        !reset_bytes(a,a->formal_summary,c->limits.tokens/4u)) return false;
    for (uint32_t i=0u;i<c->function_count;++i) {
        if (!charge(a,1u)) return false;
        c->functions[i].state.graph.color=0u;
    }
    for (uint32_t root=0u;root<c->function_count;++root) {
        if (!charge(a,1u)) return false;
        if (c->functions[root].state.graph.color==2u) continue;
        uint32_t current=root;
        c->functions[current].state.graph.color=1u;
        c->functions[current].state.graph.scan=c->functions[current].expression_start;
        c->functions[current].state.graph.parent=NO_ID;
        while (current!=NO_ID) {
            FunctionRecord *f=&c->functions[current]; bool descend=false;
            a->budget.offset=f->name.offset;
            while (f->state.graph.scan<f->expression_end) {
                if (!charge(a,1u)) return false;
                const uint32_t expression=f->state.graph.scan++;
                const SourceReference edge=c->expression_references[expression];
                if (edge.kind!=SOURCE_FUNCTION) continue;
                if (!charge(a,1u)) return false;
                FunctionRecord *child=&c->functions[edge.index];
                if (child->state.graph.color==2u) continue;
                child->state.graph.color=1u; child->state.graph.scan=child->expression_start;
                child->state.graph.parent=current; current=edge.index; descend=true; break;
            }
            if (!descend) {
                if (!function_bits(a,current)) return false;
                f->state.graph.color=2u; current=f->state.graph.parent;
            }
        }
    }
    return true;
}
bool uniformity_analyze(Source *source,uint64_t limit,uint64_t *used) {
    CompilerWorkspace *c=source->compiler;
    if (used!=NULL) *used=0u;
    bool barrier=false, subgroup=false;
    for (uint32_t i=0u;i<c->expression_count;++i) {
        const SourceReference reference=c->expression_references[i];
        barrier=barrier || reference.kind==SOURCE_BARRIER || reference.kind==SOURCE_DERIVATIVE ||
            (reference.kind == SOURCE_SUBGROUP && reference.index >= (uint32_t)BUILTIN_QUAD_BROADCAST) ||
            (reference.kind==SOURCE_IMAGE && (reference.index & SOURCE_IMAGE_DERIVATIVE)!=0u);
        subgroup=subgroup || c->expression_references[i].kind==SOURCE_SUBGROUP;
    }
    if (!barrier && !subgroup) return true;
    Uniformity a={.source=source,.compiler=c,.budget={.limit=limit},
        .scope={.values=c->scope,.capacity=(size_t)c->limits.statements+1u},
        .local_slots=(size_t)c->limits.statements+1u,
        .frame_capacity=2u*(size_t)c->limits.control_depth+c->limits.expression_depth+4u};
    unsigned char *cursor=c->uniformity;
    a.expression_bits=cursor; cursor+=c->limits.physical_expressions;
    a.current_local=cursor; cursor+=a.local_slots;
    a.formal_summary=cursor; cursor+=c->limits.tokens/4u;
    a.function_summary=cursor; cursor+=c->limits.tokens/6u;
    const size_t remainder=(size_t)(cursor-c->uniformity)%_Alignof(UniformityFrame);
    if (remainder!=0u) cursor+=_Alignof(UniformityFrame)-remainder;
    a.frames=(UniformityFrame *)cursor; cursor+=a.frame_capacity*sizeof(UniformityFrame);
    a.frame_maps=cursor;
    for (uint32_t i=0u;i<c->formal_count;++i) a.pointers |= type_pointer(c,c->formals[i].type.resolved.type);
    for (uint32_t i=0u;i<c->expression_count;++i) a.pointers |= c->expressions[i].kind == EXPRESSION_ADDRESS;
    if (a.pointers) {
        uint64_t matrix = 0u;
        for (uint32_t f=0u;f<c->function_count;++f)
            matrix += (2u*(uint64_t)c->functions[f].formal_count+2u)*((uint64_t)c->functions[f].formal_count+1u);
        const uint64_t fixed = (uint64_t)c->function_count*sizeof(uint32_t) + c->formal_count +
            (uint64_t)c->formal_count*(a.frame_capacity*MAP_COUNT+1u);
        const uint64_t available = ((uint64_t)c->limits.instructions+c->limits.tokens/4u+1u)*sizeof(uint32_t);
        if (matrix > UINT32_MAX || fixed > available || matrix > available-fixed) {
            const uint64_t words = (fixed+matrix+3u)/4u;
            const uint64_t minimum = words > c->limits.tokens/4u+1u ? words-c->limits.tokens/4u-1u : c->limits.instructions+1u;
            return source_pool_error(source,DOE_COMPILER_POOL_IR,(size_t)minimum,source->length,
                "pointer uniformity dependency scratch capacity exceeded");
        }
        a.pointer_offsets = c->values;
        unsigned char *scratch = (unsigned char *)(c->values+c->function_count);
        a.pointer_state = scratch; scratch += c->formal_count;
        a.pointer_frame_maps = scratch; scratch += c->formal_count*a.frame_capacity*MAP_COUNT;
        a.pointer_returns = scratch; scratch += c->formal_count;
        a.pointer_summaries = scratch; a.pointer_bytes = (size_t)matrix;
        uint32_t offset = 0u;
        for (uint32_t f=0u;f<c->function_count;++f) {
            a.pointer_offsets[f] = offset;
            offset += (2u*c->functions[f].formal_count+2u)*(c->functions[f].formal_count+1u);
        }
    }
    const uint32_t saved_function=c->current_function;
    /* Both scope analyses reuse dead scratch, but share the one work budget.
     * A subgroup-uniform result never becomes a workgroup barrier proof. */
    bool ok=!barrier || module_bits(&a);
    if (ok && subgroup) {
        a.subgroup=true;
        ok=module_bits(&a);
    }
    c->current_function=saved_function;
    if (used!=NULL) *used=a.budget.used;
    return ok;
}
_Static_assert(sizeof(UniformityFrame)==32u,"M11 uniformity metadata ledger");
