/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

typedef struct {
    size_t expressions, statements, declarations, functions, formals, arguments, entries, inputs, references;
    size_t generic_syntax, types, members, resources, workgroups, constants, edges, accesses;
    size_t phase, locals, scope, values, operands, behaviors, expression_references, statement_references, uniformity, reflection, targets, walk, words, total;
} WorkspaceLayout;
static bool reserve_array(size_t *cursor, size_t count, size_t element, size_t alignment, size_t *offset) {
    const size_t remainder = *cursor % alignment;
    const size_t padding = remainder == 0u ? 0u : alignment - remainder;
    if (padding > SIZE_MAX - *cursor) return false;
    *cursor += padding; *offset = *cursor;
    if (count > (SIZE_MAX - *cursor) / element) return false;
    *cursor += count * element;
    return true;
}
static bool workspace_layout(const CompilerLimits *l, WorkspaceLayout *p) {
    size_t cursor = sizeof(CompilerWorkspace), ignored;
    const size_t locals = (size_t)l->statements + 1u + l->accesses;
    const size_t values = (size_t)l->instructions + l->tokens / 4u + 1u;
    if (!reserve_array(&cursor,l->physical_expressions,sizeof(Expression),_Alignof(Expression),&p->expressions) ||
        !reserve_array(&cursor,l->statements,sizeof(Statement),_Alignof(Statement),&p->statements) ||
        !reserve_array(&cursor,(size_t)l->expressions+1u,sizeof(ConstantSyntax),_Alignof(ConstantSyntax),&p->declarations) ||
        !reserve_array(&cursor,l->tokens/6u,sizeof(FunctionRecord),_Alignof(FunctionRecord),&p->functions) ||
        !reserve_array(&cursor,l->tokens/4u,sizeof(FormalRecord),_Alignof(FormalRecord),&p->formals) ||
        !reserve_array(&cursor,l->expressions,sizeof(ArgumentSyntax),_Alignof(ArgumentSyntax),&p->arguments) ||
        !reserve_array(&cursor,l->generic_syntax,sizeof(GenericSyntax),_Alignof(GenericSyntax),&p->generic_syntax) ||
        !reserve_array(&cursor,l->types,sizeof(TypeRecord),_Alignof(TypeRecord),&p->types) ||
        !reserve_array(&cursor,l->members,sizeof(MemberRecord),_Alignof(MemberRecord),&p->members) ||
        !reserve_array(&cursor,l->resources,sizeof(ResourceRecord),_Alignof(ResourceRecord),&p->resources) ||
        !reserve_array(&cursor,l->tokens/8u,sizeof(WorkgroupRoot),_Alignof(WorkgroupRoot),&p->workgroups) ||
        !reserve_array(&cursor,l->constants,sizeof(ConstantRecord),_Alignof(ConstantRecord),&p->constants) ||
        !reserve_array(&cursor,l->edges,sizeof(ConstantValue),_Alignof(ConstantValue),&p->edges) ||
        !reserve_array(&cursor,l->accesses,sizeof(AccessStep),_Alignof(AccessStep),&p->accesses) ||
        !reserve_array(&cursor,l->entries,sizeof(EntryRecord),_Alignof(EntryRecord),&p->entries) ||
        !reserve_array(&cursor,l->entry_inputs,sizeof(EntryInput),_Alignof(EntryInput),&p->inputs) ||
        !reserve_array(&cursor,l->entry_references,sizeof(uint32_t),_Alignof(uint32_t),&p->references) ||
        !reserve_array(&cursor,0u,1u,_Alignof(ConstantValue),&p->phase)) return false;
    size_t token_end = cursor;
    if (!reserve_array(&token_end,(size_t)l->tokens+1u,sizeof(Token),_Alignof(Token),&ignored) ||
        !reserve_array(&cursor,l->instructions,sizeof(IrInstruction),_Alignof(IrInstruction),&ignored) ||
        !reserve_array(&cursor,locals,sizeof(IrLocal),_Alignof(IrLocal),&p->locals) ||
        !reserve_array(&cursor,(size_t)l->statements+1u,sizeof(LocalBinding),_Alignof(LocalBinding),&p->scope) ||
        !reserve_array(&cursor,values,sizeof(uint32_t),_Alignof(uint32_t),&p->values) ||
        !reserve_array(&cursor,l->operands,sizeof(uint32_t),_Alignof(uint32_t),&p->operands) ||
        !reserve_array(&cursor,l->statements,1u,1u,&p->behaviors) ||
        !reserve_array(&cursor,0u,1u,_Alignof(ConstantValue),&ignored)) return false;
    size_t uniform_end=cursor;
    if (!reserve_array(&uniform_end,l->physical_expressions,sizeof(SourceReference),_Alignof(SourceReference),&p->expression_references) ||
        !reserve_array(&uniform_end,l->statements,sizeof(SourceReference),_Alignof(SourceReference),&p->statement_references) ||
        !reserve_array(&uniform_end,0u,1u,_Alignof(ConstantValue),&p->uniformity)) return false;
    size_t semantic_end=uniform_end;
    if (!reserve_array(&semantic_end,l->traversal,sizeof(WalkItem),_Alignof(WalkItem),&ignored)) return false;
    size_t closure_end=uniform_end;
    if (!reserve_array(&closure_end,(size_t)l->resources+l->tokens/8u,1u,1u,&ignored) ||
        !reserve_array(&closure_end,l->resources,sizeof(uint32_t),_Alignof(uint32_t),&ignored)) return false;
    const size_t frames=2u*(size_t)l->control_depth+l->expression_depth+4u;
    if (!reserve_array(&uniform_end,l->physical_expressions,1u,1u,&ignored) ||
        !reserve_array(&uniform_end,(size_t)l->statements+1u,1u,1u,&ignored) ||
        !reserve_array(&uniform_end,l->tokens/4u,1u,1u,&ignored) ||
        !reserve_array(&uniform_end,l->tokens/6u,1u,1u,&ignored) ||
        !reserve_array(&uniform_end,frames,32u,4u,&ignored) ||
        frames>SIZE_MAX/6u ||
        !reserve_array(&uniform_end,frames*6u,(size_t)l->statements+1u,1u,&ignored) ||
        !reserve_array(&uniform_end,0u,1u,_Alignof(ConstantValue),&ignored) ||
        !reserve_array(&cursor,(size_t)l->types+7u,sizeof(uint32_t),_Alignof(uint32_t),&p->reflection) ||
        !reserve_array(&cursor,l->targets,sizeof(TargetDeclaration),_Alignof(TargetDeclaration),&p->targets) ||
        !reserve_array(&cursor,l->traversal,sizeof(WalkItem),_Alignof(WalkItem),&p->walk) ||
        !reserve_array(&cursor,l->words,sizeof(uint32_t),_Alignof(uint32_t),&p->words)) return false;
    if (cursor < semantic_end) cursor = semantic_end;
    if (cursor < closure_end) cursor = closure_end;
    if (cursor < uniform_end) cursor = uniform_end;
    if (cursor < token_end) cursor = token_end;
    if (!reserve_array(&cursor,0u,1u,_Alignof(ConstantValue),&ignored)) return false;
    p->total = cursor;
    return true;
}
static bool limits_fit(const CompilerLimits *l) {
    return l->types <= UINT32_MAX-7u && l->expressions < UINT32_MAX && l->tokens < UINT32_MAX &&
        (uint64_t)l->statements+1u+l->accesses < UINT32_MAX &&
        (uint64_t)l->instructions+l->tokens/4u+1u < UINT32_MAX;
}
static bool finish_layout(CompilerLimits candidate, CompilerLimits *limits, size_t *bytes) {
    WorkspaceLayout layout;
    if (!limits_fit(&candidate) || !workspace_layout(&candidate,&layout)) return false;
    *limits = candidate; *bytes = layout.total; return true;
}
bool compiler_job_layout(const DoeCompilerJobCapacities *c, CompilerLimits *l, size_t *bytes) {
    if (c == NULL || c->source_bytes > UINT32_MAX-1u || c->tokens > UINT32_MAX-1u ||
        c->statements > UINT32_MAX-1u || c->expressions > UINT32_MAX-1u ||
        c->expression_depth == 0u || c->expression_depth > 64u ||
        c->control_depth == 0u || c->control_depth > 32u) return false;
    const uint64_t instructions = 1u+4u*(uint64_t)c->expressions+8u*(uint64_t)c->statements;
    const uint64_t old_bound = 256u+32u*instructions+3u*(c->tokens/6u)+c->tokens/4u;
    if (instructions >= UINT32_MAX || old_bound >= UINT32_MAX) return false;
    CompilerLimits candidate = { .source_bytes=(uint32_t)c->source_bytes,.tokens=(uint32_t)c->tokens,
        .statements=(uint32_t)c->statements,.expressions=(uint32_t)c->expressions,
        .physical_expressions=(uint32_t)c->expressions+1u,.instructions=(uint32_t)instructions,
        .types=32u,.members=64u,.resources=8u,.accesses=(uint32_t)(c->expressions+c->statements+1u),
        .constants=64u,.edges=128u,.targets=464u,.generic_syntax=32u,.traversal=97u,
        .words=c->words,.operands=(uint32_t)c->expressions,
        .expression_depth=c->expression_depth,.control_depth=c->control_depth,.type_depth=64u,
        .entries=1u,.entry_inputs=5u,.entry_references=8u+(uint32_t)(c->tokens/8u),.flags=LIMIT_SINGULAR };
    return finish_layout(candidate,l,bytes);
}
static bool typed_limits(const DoeCompilerTypedCapacities *c, CompilerLimits *l) {
    if (c == NULL || c->reserved != 0u || c->expression_depth == 0u || c->expression_depth > 64u ||
        c->control_depth == 0u || c->control_depth > 32u || c->type_syntax_depth == 0u || c->type_syntax_depth > 64u ||
        c->source_bytes >= UINT32_MAX || c->tokens >= UINT32_MAX || c->statements >= UINT32_MAX ||
        c->expressions >= UINT32_MAX || c->types > UINT32_MAX-7u || c->members >= UINT32_MAX ||
        c->resources >= UINT32_MAX || c->access_steps >= UINT32_MAX || c->constants >= UINT32_MAX ||
        c->constant_edges >= UINT32_MAX || c->ir_instructions >= UINT32_MAX || c->ir_operands >= UINT32_MAX ||
        c->target_declarations >= UINT32_MAX || c->traversal_items >= UINT32_MAX) return false;
    CompilerLimits candidate = { .source_bytes=(uint32_t)c->source_bytes,.tokens=(uint32_t)c->tokens,
        .statements=(uint32_t)c->statements,.expressions=(uint32_t)c->expressions,
        .physical_expressions=(uint32_t)c->expressions+1u,.instructions=(uint32_t)c->ir_instructions,
        .types=(uint32_t)c->types,.members=(uint32_t)c->members,.resources=(uint32_t)c->resources,
        .accesses=(uint32_t)c->access_steps,.constants=(uint32_t)c->constants,.edges=(uint32_t)c->constant_edges,
        .targets=(uint32_t)c->target_declarations,.generic_syntax=(uint32_t)(c->tokens/2u),
        .traversal=(uint32_t)c->traversal_items,.words=c->words,.operands=(uint32_t)c->ir_operands,
        .expression_depth=c->expression_depth,.control_depth=c->control_depth,.type_depth=c->type_syntax_depth };
    *l=candidate;
    return limits_fit(l);
}
bool compiler_typed_layout(const DoeCompilerTypedCapacities *c, CompilerLimits *l, size_t *bytes) {
    CompilerLimits candidate;
    if (!typed_limits(c,&candidate)) return false;
    candidate.entries=candidate.tokens/13u;
    candidate.entry_inputs=candidate.tokens/8u;
    const uint64_t references=(uint64_t)candidate.entries*((uint64_t)candidate.resources+candidate.tokens/8u);
    candidate.entry_references=references<UINT32_MAX?(uint32_t)references:UINT32_MAX-1u;
    return finish_layout(candidate,l,bytes);
}
bool compiler_module_layout(const DoeCompilerModuleCapacities *c, CompilerLimits *l, size_t *bytes) {
    CompilerLimits candidate;
    if (c==NULL || c->entries>=UINT32_MAX || c->entry_inputs>=UINT32_MAX || c->entry_references>=UINT32_MAX ||
        !typed_limits(&c->typed,&candidate)) return false;
    candidate.entries=(uint32_t)c->entries;
    candidate.entry_inputs=(uint32_t)c->entry_inputs;
    candidate.entry_references=(uint32_t)c->entry_references;
    candidate.flags=LIMIT_MODULE;
    return finish_layout(candidate,l,bytes);
}
void compiler_job_bind(CompilerWorkspace *c, void *memory, const CompilerLimits *limits) {
    WorkspaceLayout p; (void)workspace_layout(limits,&p);
    unsigned char *b=memory;
    memset(c,0,sizeof(*c)); c->limits=*limits;
    c->expressions=(Expression *)(b+p.expressions); c->statements=(Statement *)(b+p.statements);
    c->constant_decls=(ConstantSyntax *)(b+p.declarations); c->functions=(FunctionRecord *)(b+p.functions);
    c->formals=(FormalRecord *)(b+p.formals); c->arguments=(ArgumentSyntax *)(b+p.arguments);
    c->entries=(EntryRecord *)(b+p.entries); c->entry_inputs=(EntryInput *)(b+p.inputs);
    c->entry_references=(uint32_t *)(b+p.references);
    c->expression_references=(SourceReference *)(b+p.expression_references);
    c->statement_references=(SourceReference *)(b+p.statement_references);
    c->generic_syntax=(GenericSyntax *)(b+p.generic_syntax); c->types=(TypeRecord *)(b+p.types);
    c->members=(MemberRecord *)(b+p.members); c->resources=(ResourceRecord *)(b+p.resources);
    c->workgroups=(WorkgroupRoot *)(b+p.workgroups);
    c->statement_behaviors=b+p.behaviors; c->uniformity=b+p.uniformity;
    c->constants=(ConstantRecord *)(b+p.constants); c->constant_edges=(ConstantValue *)(b+p.edges);
    c->accesses=(AccessStep *)(b+p.accesses); c->tokens=(Token *)(b+p.phase);
    c->locals=(IrLocal *)(b+p.locals); c->scope=(LocalBinding *)(b+p.scope); c->values=(uint32_t *)(b+p.values);
    c->typed_operands=(uint32_t *)(b+p.operands); c->reflection_map=(uint32_t *)(b+p.reflection);
    c->targets=(TargetDeclaration *)(b+p.targets); c->walk=(WalkItem *)(b+p.uniformity); c->words=(uint32_t *)(b+p.words);
}
void compiler_legacy_bind(LegacyWorkspace *storage, bool uniforms) {
    (void)uniforms;
    const CompilerLimits l={ .source_bytes=65536u,.tokens=2048u,.statements=16u,.expressions=320u,
        .physical_expressions=337u,.instructions=385u,.types=32u,.members=64u,.resources=8u,.accesses=337u,
        .constants=64u,.edges=128u,.targets=464u,.generic_syntax=32u,.traversal=97u,.words=4096u,.operands=320u,
        .expression_depth=18u,.control_depth=4u,.type_depth=18u,.flags=LIMIT_LEGACY|LIMIT_SINGULAR,
        .entries=1u,.entry_inputs=5u,.entry_references=264u };
    compiler_job_bind(&storage->job,storage,&l);
}
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(CompilerWorkspace)==472u,"LP64 core ledger");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(CompilerWorkspace)==340u,"32-bit core ledger");
#else
#error Unsupported pointer width
#endif
_Static_assert(sizeof(Expression)==40u && sizeof(Statement)==56u && sizeof(ConstantSyntax)==40u,"M10 syntax ledger");
_Static_assert(sizeof(TypeRecord)==56u && sizeof(MemberRecord)==56u && sizeof(ResourceRecord)==104u,"M10 type ledger");
_Static_assert(sizeof(ConstantRecord)==32u && sizeof(ConstantValue)==16u && sizeof(AccessStep)==24u,"M10 value ledger");
_Static_assert(sizeof(IrInstruction)==32u && sizeof(LocalBinding)==48u && sizeof(TargetDeclaration)==32u,"M10 lowering ledger");
_Static_assert(sizeof(LegacyWorkspace)<=147456u,"M10 fixed workspace ceiling");

_Static_assert(sizeof(WorkgroupRoot)==24u,"M11 workgroup root ledger");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(LegacyWorkspace)==146048u,"LP64 exact fixed workspace ledger");
#else
_Static_assert(sizeof(LegacyWorkspace)==145920u,"32-bit exact fixed workspace ledger");
#endif

_Static_assert(sizeof(EntryRecord)==64u && sizeof(EntryInput)==20u && sizeof(SourceReference)==8u,"M13 entry ledger");

bool compiler_target_phase(Source *source) {
    CompilerWorkspace *c=source->compiler;
    if (c->walk_count!=0u)
        return source_error(source,DOE_COMPILER_INPUT_ERROR,source->length,"semantic traversal remains active at target phase boundary");
    c->walk=(WalkItem *)(c->targets+c->limits.targets);
    return true;
}
