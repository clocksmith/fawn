/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
static bool lower_boolean(Source *source,const Syntax *s,uint32_t id,const LocalScope *scope,IrProgram *program,IrValue *value) {
    (void)s;(void)program;ExpressionValue result;
    if (!sema_expression(source,id,scope,true,true,&result)) return false;
    if (result.constant.type!=IR_BOOL) return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,result.value.source_offset,"control condition requires scalar bool");
    if (!sema_ensure_value(source,&result,true)) return false;
    *value=result.value;return true;
}
static ExpressionKind update_kind(UpdateKind operation) {
    switch (operation) {
        case UPDATE_ADD: case UPDATE_INCREMENT:return EXPRESSION_ADD;
        case UPDATE_SUBTRACT: case UPDATE_DECREMENT:return EXPRESSION_SUBTRACT;
        case UPDATE_MULTIPLY:return EXPRESSION_MULTIPLY;case UPDATE_DIVIDE:return EXPRESSION_DIVIDE;
        case UPDATE_REMAINDER:return EXPRESSION_REMAINDER;case UPDATE_BIT_AND:return EXPRESSION_BIT_AND;
        case UPDATE_BIT_OR:return EXPRESSION_BIT_OR;case UPDATE_BIT_XOR:return EXPRESSION_BIT_XOR;
        case UPDATE_SHIFT_LEFT:return EXPRESSION_SHIFT_LEFT;case UPDATE_SHIFT_RIGHT:return EXPRESSION_SHIFT_RIGHT;
        case UPDATE_SET:break;
    }
    return EXPRESSION_ADD;
}
static bool lower_assignment(Source *source,const Syntax *s,const Assignment *assignment,const LocalScope *scope,IrProgram *program) {
    (void)s;(void)program;ValueReference reference;
    if (!sema_assignment_reference(source,assignment,scope,&reference)) return false;
    const uint32_t offset=assignment->root.offset;
    const bool increment=assignment->operation==UPDATE_INCREMENT || assignment->operation==UPDATE_DECREMENT;
    ExpressionValue previous={0},result={0};
    if (assignment->operation!=UPDATE_SET) {
        if (increment && reference.type!=IR_U32 && reference.type!=IR_I32)
            return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,offset,"increment requires a writable scalar integer");
        if (!sema_reference_load(source,reference,offset,true,&previous)) return false;
    }
    if (increment) result=(ExpressionValue){.known=true,.constant={.type=reference.type,.bits=1u},.value={.type=reference.type,.source_offset=offset}};
    else if (!sema_expression(source,assignment->expression,scope,true,true,&result)) return false;
    if (assignment->operation!=UPDATE_SET) {
        ExpressionValue combined;
        if (!sema_binary(source,update_kind(assignment->operation),COMPARE_EQUAL,&previous,&result,offset,true,true,&combined)) return false;
        result=combined;
    }
    if (!sema_materialize(source,&result,reference.type) || !sema_ensure_value(source,&result,true)) return false;
    if (reference.category==ROOT_LOCAL && reference.path==NO_ID)
        return sema_append(source,(IrInstruction){.opcode=IR_LOCAL_STORE,.type=reference.type,.source_offset=offset,
            .operands.scalar={.right=result.value.id,.binding=reference.root}},NULL);
    return sema_append(source,(IrInstruction){.opcode=IR_ACCESS_STORE,.type=reference.type,.source_offset=offset,
        .operands.access={reference.root,reference.path,result.value.id,reference.category}},NULL);
}
static bool lower_local(Source *source,const Syntax *s,const LocalSyntax *local,LocalScope *scope,IrProgram *program) {
    (void)s;const NameSpan name=local->name;
    if (!sema_valid_name(source,name)) return false;
    bool duplicate=false;
    for (size_t i=scope->scope_start;i<scope->count;++i) duplicate=duplicate||source_spans_equal(source,name,scope->values[i].name);
    if (scope->level==0u && (sema_find_formal(source,name)!=NO_ID || sema_find_parameter(source,name)!=NO_ID ||
        (sema_alias_visible(source) && source_spans_equal(source,name,program->alias)))) duplicate=true;
    if (duplicate) return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,name.offset,"local declaration duplicates a binding in its scope");
    TypeId annotation=IR_VOID;const bool constant=(local->flags&LOCAL_CONSTANT)!=0u;
    if ((local->flags&LOCAL_ANNOTATED)!=0u && !sema_type_reference(source,local->annotation,scope,&annotation)) return false;
    ExpressionValue result={.known=true,.constant.type=annotation,.value={.type=annotation,.source_offset=name.offset}};
    if ((local->flags&LOCAL_INITIALIZED)!=0u) {
        if (!sema_expression(source,local->expression,scope,true,true,&result)) return false;
        if (constant && result.runtime) return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,name.offset,"const initializer requires a const expression");
        if ((local->flags&LOCAL_ANNOTATED)!=0u) {if (!sema_materialize(source,&result,annotation)) return false;}
        else if (!constant && type_abstract(program,result.constant.type)) {
            TypeId type;
            if (!sema_default_type(source,result.constant.type,name.offset,&type) || !sema_materialize(source,&result,type)) return false;
        }
    } else if (!constant_zero(source,annotation,&result.constant)) return false;
    if (!constant && !type_constructible(program,result.constant.type)) return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,name.offset,"local variable type must be concrete and constructible");
    if (!constant && !sema_ensure_value(source,&result,true)) return false;
    if (scope->count==scope->capacity) return source_pool_error(source,DOE_COMPILER_POOL_STATEMENTS,scope->count,name.offset,"local scope capacity exceeded");
    LocalBinding binding={.name=name,.value=result.value.id,.type=result.constant.type,.constant_value=result.constant,
        .declaration_offset=name.offset,.flags=local->flags,.level=(uint32_t)scope->level};
    if ((local->flags&LOCAL_MUTABLE)!=0u) {
        const size_t capacity=(size_t)program->limits.statements+1u+program->limits.accesses;
        if (program->local_count==capacity) return source_pool_error(source,DOE_COMPILER_POOL_ACCESS_STEPS,(size_t)program->limits.accesses+1u,name.offset,"mutable and access local capacity exceeded");
        binding.local_id=program->local_count;program->locals[program->local_count++]=(IrLocal){.type=result.constant.type,.flags=LOCAL_MUTABLE};
        if (!sema_append(source,(IrInstruction){.opcode=IR_LOCAL_STORE,.type=result.constant.type,.source_offset=name.offset,
            .operands.scalar={.right=result.value.id,.binding=binding.local_id}},NULL)) return false;
    }
    scope->values[scope->count++]=binding;return true;
}
static bool lower_block(Source *, const Syntax *, size_t, size_t, LocalScope *, IrProgram *,
                        LoopScope *, bool, bool, bool, unsigned *);
static bool marker(Source *source, IrProgram *program, IrOpcode opcode, uint32_t offset, uint32_t literal) {
    (void)program;
    return sema_append(source, (IrInstruction){ .opcode = opcode, .operands.scalar.literal = literal,
                   .source_offset = offset }, NULL);
}
static bool inside_continuing(const LoopScope *loop) {
    for (; loop != NULL; loop = loop->parent) if (loop->continuing) return true;
    return false;
}
static bool lower_loop(Source *source, const Syntax *s, const Statement *st,
                       LocalScope *scope, IrProgram *program, LoopScope *outer, unsigned *behavior) {
    const size_t saved_count = scope->count, saved_start = scope->scope_start, saved_level = scope->level;
    const bool is_for = st->kind == STATEMENT_FOR;
    if (is_for) {
        scope->scope_start = scope->count; ++scope->level;
        if (st->initializer != UINT32_MAX) {
            unsigned ignored;
            if (!lower_block(source, s, st->initializer, st->initializer + 1u, scope, program,
                             outer, true, true, false, &ignored)) return false;
        }
    }
    const size_t begin = program->instruction_count;
    if (!marker(source, program, IR_LOOP_BEGIN, st->source_offset, 0u)) return false;
    IrValue test = {0};
    const bool has_test = st->kind == STATEMENT_WHILE || (is_for && st->value.condition != UINT32_MAX);
    if (has_test && !lower_boolean(source, s, st->value.condition, scope, program, &test)) return false;
    if (!sema_append(source, (IrInstruction){ .opcode = IR_LOOP_TEST, .operands.scalar.left = test.id,
        .source_offset = st->source_offset }, NULL)) return false;
    size_t continuing = st->end;
    for (size_t i = st->body_start; i < st->end; i = s->statements[i].end) {
        if (s->statements[i].kind == STATEMENT_CONTINUING) {
            if (st->kind != STATEMENT_LOOP || s->statements[i].end != st->end || continuing != st->end)
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->statements[i].source_offset,
                                    "continuing must be the final direct child of loop");
            continuing = i;
        }
    }
    LoopScope loop = { .parent = outer, .body_scope_start = scope->count,
                       .first_continue = UINT32_MAX };
    const size_t body_start = scope->scope_start, body_level = scope->level;
    scope->scope_start = scope->count; ++scope->level; scope->loop = &loop;
    unsigned body, tail = BEHAVIOR_NEXT;
    if (!lower_block(source, s, st->body_start, continuing, scope, program, &loop,
                     true, true, false, &body)) return false;
    loop.body_scope_end = scope->count;
    if (!marker(source, program, IR_LOOP_CONTINUING, st->source_offset, 0u)) return false;
    if (is_for) {
        scope->count = loop.body_scope_start;
        scope->scope_start = body_start; scope->level = body_level;
        if (st->update != UINT32_MAX) {
            unsigned ignored;
            if (!lower_block(source, s, st->update, st->update + 1u, scope, program, outer,
                             true, true, false, &ignored)) return false;
        }
    } else if (continuing != st->end) {
        const Statement *part = &s->statements[continuing];
        loop.continuing = true;
        if (!lower_block(source, s, part->body_start, part->end, scope, program, &loop,
                         false, true, true, &tail)) return false;
    }
    *behavior = body & BEHAVIOR_RETURN;
    if ((body & BEHAVIOR_BREAK) != 0u || has_test) *behavior |= BEHAVIOR_NEXT;
    if ((body & (BEHAVIOR_NEXT | BEHAVIOR_CONTINUE)) != 0u) {
        *behavior |= tail & BEHAVIOR_RETURN;
        if ((tail & BEHAVIOR_BREAK) != 0u) *behavior |= BEHAVIOR_NEXT;
    }
    if (*behavior == 0u)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                            "loop has an empty WGSL behavior set");
    program->instructions[begin].operands.scalar.literal = *behavior;
    if (!marker(source, program, IR_END_LOOP, st->source_offset, *behavior)) return false;
    scope->count = saved_count; scope->scope_start = saved_start; scope->level = saved_level; scope->loop = outer;
    return true;
}
static bool lower_block(Source *source, const Syntax *s, size_t start, size_t end,
                        LocalScope *scope, IrProgram *program, LoopScope *loop,
                        bool retain_scope, bool reachable, bool continuing_direct, unsigned *behavior) {
    const size_t saved_count = scope->count, saved_start = scope->scope_start, saved_level = scope->level;
    LoopScope *saved_loop = scope->loop;
    scope->loop = loop;
    if (!retain_scope) { scope->scope_start = scope->count; ++scope->level; }
    *behavior = BEHAVIOR_NEXT;
    for (size_t i = start; i < end; i = s->statements[i].end) {
        const Statement *st = &s->statements[i];
        const bool live = reachable && ((*behavior & BEHAVIOR_NEXT) != 0u);
        unsigned current = BEHAVIOR_NEXT;
        switch (st->kind) {
            case STATEMENT_ASSIGN:
                if (!lower_assignment(source, s, &st->value.assignment, scope, program)) return false;
                break;
            case STATEMENT_LET:
            case STATEMENT_CONST:
            case STATEMENT_VAR:
                if (!lower_local(source, s, &st->value.local, scope, program)) return false;
                break;
            case STATEMENT_CALL: {
                ExpressionValue ignored;
                if (!sema_call_value(source, st->value.condition, scope, true, true, true, &ignored)) return false;
                break;
            }
            case STATEMENT_RETURN: {
                if (inside_continuing(loop))
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                        "return is prohibited inside continuing");
                const TypeId result_type = s->functions[s->current_function].result_type;
                IrInstruction result = { .opcode = IR_RETURN, .type = result_type, .source_offset = st->source_offset };
                if (st->value.condition == UINT32_MAX) {
                    if (result_type != IR_VOID)
                        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                            "value-returning function requires a return value");
                } else {
                    if (result_type == IR_VOID)
                        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                            "no-result function cannot return a value");
                    ExpressionValue value;
                    if (!sema_expression(source, st->value.condition, scope, true, true, &value) ||
                        !sema_materialize(source, &value, result_type) || !sema_ensure_value(source, &value, true)) return false;
                    result.operands.scalar.left = value.value.id;
                }
                current = BEHAVIOR_RETURN;
                if (!sema_append(source, result, NULL)) return false;
                break;
            }
            case STATEMENT_BREAK:
            case STATEMENT_CONTINUE:
                if (loop == NULL || loop->continuing)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                        "break or continue requires a non-continuing nearest loop");
                current = st->kind == STATEMENT_BREAK ? BEHAVIOR_BREAK : BEHAVIOR_CONTINUE;
                if (current == BEHAVIOR_CONTINUE && live && st->source_offset < loop->first_continue)
                    loop->first_continue = st->source_offset;
                if (!marker(source, program, st->kind == STATEMENT_BREAK ? IR_BREAK : IR_CONTINUE,
                            st->source_offset, 0u)) return false;
                break;
            case STATEMENT_BREAK_IF: {
                if (!continuing_direct || loop == NULL || !loop->continuing || st->end != end)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                        "break if must end its direct continuing block");
                IrValue test;
                if (!lower_boolean(source, s, st->value.condition, scope, program, &test) ||
                    !sema_append(source, (IrInstruction){ .opcode = IR_BREAK_IF,
                        .operands.scalar.left = test.id, .source_offset = st->source_offset }, NULL)) return false;
                current = BEHAVIOR_BREAK | BEHAVIOR_NEXT;
                break;
            }
            case STATEMENT_IF: {
                IrValue test;
                unsigned yes, no;
                if (!lower_boolean(source, s, st->value.condition, scope, program, &test) ||
                    !sema_append(source, (IrInstruction){ .opcode = IR_IF,
                        .operands.scalar.left = test.id, .source_offset = st->source_offset }, NULL) ||
                    !lower_block(source, s, st->body_start, st->else_start, scope, program, loop,
                                 false, live, false, &yes) ||
                    !marker(source, program, IR_ELSE, st->source_offset, 0u) ||
                    !lower_block(source, s, st->else_start, st->end, scope, program, loop,
                                 false, live, false, &no) ||
                    !marker(source, program, IR_END_IF, st->source_offset, 0u)) return false;
                current = yes | no;
                break;
            }
            case STATEMENT_BLOCK:
                if (!lower_block(source, s, st->body_start, st->end, scope, program, loop,
                                 false, live, false, &current)) return false;
                break;
            case STATEMENT_FOR:
            case STATEMENT_WHILE:
            case STATEMENT_LOOP:
                if (!lower_loop(source, s, st, scope, program, loop, &current)) return false;
                break;
            case STATEMENT_CONTINUING:
                return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, st->source_offset,
                                    "continuing must be the final direct child of loop");
            case STATEMENT_EMPTY: break;
        }
        if ((*behavior & BEHAVIOR_NEXT) != 0u) *behavior = (*behavior & ~(unsigned)BEHAVIOR_NEXT) | current;
    }
    if (!retain_scope) scope->count = saved_count;
    scope->scope_start = saved_start; scope->level = saved_level; scope->loop = saved_loop;
    return true;
}

static bool reject_call_cycles(Source *source, Syntax *s) {
    for (size_t f = 0u; f < s->function_count; ++f) s->functions[f].state.graph.color = 0u;
    for (size_t root = 0u; root < s->function_count; ++root) {
        if (s->functions[root].state.graph.color == 2u) continue;
        uint32_t current = (uint32_t)root;
        s->functions[current].state.graph.color = 1u;
        s->functions[current].state.graph.scan = s->functions[current].expression_start;
        s->functions[current].state.graph.parent = UINT32_MAX;
        while (current != UINT32_MAX) {
            FunctionRecord *function = &s->functions[current];
            bool descend = false;
            while (function->state.graph.scan < function->expression_end) {
                const uint32_t expression = function->state.graph.scan++;
                const uint32_t edge = s->callees[expression];
                if (edge == 0u) continue;
                FunctionRecord *child = &s->functions[edge - 1u];
                if (child->state.graph.color == 1u)
                    return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, s->expressions[expression].source_offset,
                                        "function call dependency cycle");
                if (child->state.graph.color == 2u) continue;
                child->state.graph.color = 1u; child->state.graph.scan = child->expression_start;
                child->state.graph.parent = current; current = edge - 1u; descend = true; break;
            }
            if (!descend) {
                function->state.graph.color = 2u;
                current = function->state.graph.parent;
            }
        }
    }
    return true;
}
bool validate_syntax(Source *source,CompilerWorkspace *c) {
    LocalScope scope={.values=c->scope,.capacity=(size_t)c->limits.statements+1u};
    if (!sema_resolve_declarations(source,&scope)) return false;
    for (uint32_t f=0u;f<c->function_count;++f) {
        FunctionRecord *function=&c->functions[f];c->current_function=f;
        scope.count=0u;scope.scope_start=0u;scope.level=0u;scope.loop=NULL;
        function->instruction_start=c->instruction_count;function->local_start=c->local_count;
        if (sema_alias_visible(source)) {
            const uint32_t parameter=sema_find_parameter(source,c->alias_source);IrValue alias;
            if (parameter==NO_ID || c->parameters[parameter].kind==INPUT_LOCAL_INDEX)
                return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,c->alias_source.offset,"leading alias requires a vector compute input");
            if (!sema_append(source,(IrInstruction){.opcode=IR_INPUT,.type=IR_U32,.source_offset=c->alias_source.offset,
                .operands.scalar={.binding=parameter,.literal=0u}},&alias)) return false;
            function->value_base=alias.id;
        }
        unsigned behavior;
        if (!lower_block(source,c,function->statement_start,function->statement_end,&scope,c,NULL,true,true,false,&behavior)) return false;
        if (function->result_type!=IR_VOID && behavior!=BEHAVIOR_RETURN)
            return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,function->name.offset,"value-returning function must return a value on every path");
        function->instruction_end=c->instruction_count;function->local_count=c->local_count-function->local_start;
    }
    return reject_call_cycles(source,c);
}
