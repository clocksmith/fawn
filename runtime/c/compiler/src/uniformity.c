/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include <string.h>

/* One Boolean sensitivity run represents reachability from one distinguished
 * source of the dated WGSL uniformity graph. No source numerics are executed. */
enum { SUMMARY_REQUIRED=1u, SUMMARY_RETURN=2u };
enum { MAP_INCOMING, MAP_FIRST, MAP_HEAD, MAP_CONTINUE, MAP_BREAK, MAP_TEMP, MAP_COUNT };
enum { FRAME_BREAK=1u, FRAME_CONTINUE=2u };
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
static bool copy_map(Uniformity *a,unsigned char *to,const unsigned char *from) {
    if (!charge(a,a->local_slots)) return false;
    memcpy(to,from,a->local_slots); return true;
}
static bool join_map(Uniformity *a,unsigned char *to,const unsigned char *from) {
    for (size_t i=0u;i<a->local_slots;++i) {
        if (!charge(a,1u)) return false;
        to[i]|=from[i];
    }
    return true;
}
static bool equal_map(Uniformity *a,const unsigned char *x,const unsigned char *y,bool *same) {
    *same=true;
    for (size_t i=0u;i<a->local_slots;++i) {
        if (!charge(a,1u)) return false;
        if (x[i]!=y[i]) *same=false;
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
static bool name_bit(Uniformity *a,NameSpan name,uint32_t cf,uint32_t *out) {
    ValueIdentity identity;
    if (!sema_value_identity(a->source,&a->scope,name,&a->budget,&identity) || !charge(a,1u)) return false;
    *out=cf;
    switch (identity.kind) {
        case IDENTITY_LOCAL:
            *out|=a->current_local[a->scope.values[identity.index].local_id]; return true;
        case IDENTITY_FORMAL:
            *out|=a->seed==identity.index?1u:0u; return true;
        case IDENTITY_CONSTANT: return true;
        case IDENTITY_ALIAS:
            /* The fixed adapter's compact alias denotes a validated input x. */
            for (uint32_t i=0u;i<a->compiler->entry_input_count;++i) {
                if (!charge(a,1u)) return false;
                if (source_spans_equal(a->source,a->compiler->alias_source,a->compiler->entry_inputs[i].name)) {
                    const InputKind kind=a->compiler->entry_inputs[i].kind;
                    *out=kind==INPUT_WORKGROUP || kind==INPUT_NUM_WORKGROUPS?cf:varying(a); return true;
                }
            }
            break;
        case IDENTITY_INPUT: {
            const InputKind kind=a->compiler->entry_inputs[identity.index].kind;
            *out=kind==INPUT_WORKGROUP || kind==INPUT_NUM_WORKGROUPS?cf:varying(a); return true;
        }
        case IDENTITY_RESOURCE:
            if (a->compiler->resources[identity.index].access==DOE_COMPILER_READ_WRITE) *out=varying(a);
            return true;
        case IDENTITY_WORKGROUP: *out=varying(a); return true;
        case IDENTITY_NONE: break;
    }
    return source_error(a->source,DOE_COMPILER_INPUT_ERROR,name.offset,"validated uniformity value identity is missing");
}
static bool expression_bit(Uniformity *,uint32_t,uint32_t,uint32_t *);
static bool call_bit(Uniformity *a,uint32_t id,uint32_t cf,uint32_t *out) {
    const Expression *e=&a->compiler->expressions[id];
    if (!charge(a,1u)) return false;
    const SourceReference reference=a->compiler->expression_references[id];
    const uint32_t callee=reference.kind==SOURCE_FUNCTION?reference.index+1u:0u;
    *out=cf;
    if (reference.kind==SOURCE_BARRIER) return require_uniform(a,cf,e->source_offset);
    const FunctionRecord *function=callee==0u?NULL:&a->compiler->functions[callee-1u];
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
        if (function==NULL) *out|=bit;
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
    if (assignment->kind==TARGET_INDEX) {
        if (!expression_bit(a,assignment->index,a->cf,&part)) return false;
        bits|=part;
    }
    for (uint32_t path=assignment->path;path!=NO_ID;path=a->compiler->expressions[path].left) {
        if (!charge(a,1u)) return false;
        const Expression *e=&a->compiler->expressions[path];
        if (e->kind==EXPRESSION_LOAD) {
            if (!expression_bit(a,e->value.reference.index,a->cf,&part)) return false;
            bits|=part;
        }
    }
    ValueIdentity identity;
    if (!sema_value_identity(a->source,&a->scope,assignment->root,&a->budget,&identity)) return false;
    if (assignment->expression!=NO_ID) {
        if (!expression_bit(a,assignment->expression,a->cf,&part)) return false;
        bits|=part;
    }
    if (identity.kind==IDENTITY_LOCAL) {
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
    a->scope.values[a->scope.count++]=(LocalBinding){.name=local->name,.local_id=statement,.flags=local->flags};
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
    if (a->loop==NO_ID)
        return source_error(a->source,DOE_COMPILER_INPUT_ERROR,a->budget.offset,"validated uniformity loop target is missing");
    UniformityFrame *frame=&a->frames[a->loop];
    const uint32_t flag=continuing?FRAME_CONTINUE:FRAME_BREAK, map=continuing?MAP_CONTINUE:MAP_BREAK;
    if ((frame->flags&flag)!=0u) {
        if (!join_map(a,frame_map(a,a->loop,map),a->current_local)) return false;
    } else if (!copy_map(a,frame_map(a,a->loop,map),a->current_local)) return false;
    frame->flags|=flag;
    if (continuing) frame->continue_cf|=a->cf; else frame->break_cf|=a->cf;
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
            a->returned|=bit; return true;
        case STATEMENT_BREAK: return loop_exit(a,false);
        case STATEMENT_CONTINUE: return loop_exit(a,true);
        case STATEMENT_BREAK_IF:
            return expression_bit(a,st->value.condition,a->cf,&a->cf) && loop_exit(a,false);
        case STATEMENT_IF: return if_bits(a,id);
        case STATEMENT_BLOCK: return block_bits(a,st->body_start,st->end,false,&ignored);
        case STATEMENT_FOR: case STATEMENT_WHILE: case STATEMENT_LOOP: return loop_bits(a,id);
        case STATEMENT_CONTINUING:
            return source_error(a->source,DOE_COMPILER_INPUT_ERROR,st->source_offset,"unexpected validated continuing statement");
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
    for (uint32_t run=0u;run<f->formal_count+2u;++run) {
        a->budget.offset=f->name.offset;
        a->scope.count=0u; a->frame_count=0u; a->loop=NO_ID;
        a->seed=run==0u?NO_ID:run==1u?NO_ID-1u:f->formal_start+run-2u;
        a->cf=run==1u?1u:0u; a->required=0u; a->returned=0u; a->required_offset=f->name.offset;
        if (!reset_bytes(a,a->expression_bits,a->compiler->limits.physical_expressions) ||
            !reset_bytes(a,a->current_local,a->local_slots) || !charge(a,3u)) return false;
        bool next;
        if (!block_bits(a,f->statement_start,f->statement_end,true,&next)) return false;
        if (run==0u) {
            if (a->required!=0u)
                return source_error(a->source,DOE_COMPILER_SEMANTIC_ERROR,a->required_offset,"uniform control cannot be established");
            if (a->returned!=0u) a->function_summary[index]|=SUMMARY_RETURN;
        } else if (run==1u) {
            if (a->required!=0u) a->function_summary[index]|=SUMMARY_REQUIRED;
        } else a->formal_summary[f->formal_start+run-2u]=(unsigned char)(
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
    bool barrier=false;
    for (uint32_t i=0u;i<c->expression_count;++i) barrier=barrier || c->expression_references[i].kind==SOURCE_BARRIER;
    if (!barrier) return true;
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
    const uint32_t saved_function=c->current_function;
    const bool ok=module_bits(&a);
    c->current_function=saved_function;
    if (used!=NULL) *used=a.budget.used;
    return ok;
}
_Static_assert(sizeof(UniformityFrame)==32u,"M11 uniformity metadata ledger");
