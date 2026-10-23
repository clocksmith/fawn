/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

typedef struct {
    size_t statements, expressions, constants, tokens, instructions, locals, scope, values, words, total;
    size_t functions, formals, arguments, callees, typed_arguments;
} JobLayout;
static bool reserve_array(size_t *cursor, size_t count, size_t element,
                          size_t alignment, size_t *offset) {
    const size_t remainder = *cursor % alignment;
    const size_t padding = remainder == 0u ? 0u : alignment - remainder;
    if (padding > SIZE_MAX - *cursor) return false;
    *cursor += padding;
    *offset = *cursor;
    if (count > (SIZE_MAX - *cursor) / element) return false;
    *cursor += count * element;
    return true;
}
static bool layout_job(const CompilerLimits *l, JobLayout *out) {
    size_t cursor = sizeof(CompilerWorkspace);
    if (!reserve_array(&cursor, l->statements, sizeof(Statement), _Alignof(Statement), &out->statements) ||
        !reserve_array(&cursor, l->physical_expressions, sizeof(Expression), _Alignof(Expression), &out->expressions) ||
        !reserve_array(&cursor, l->constants, sizeof(ConstantSyntax), _Alignof(ConstantSyntax), &out->constants) ||
        !reserve_array(&cursor, l->tokens + 1u, sizeof(Token), _Alignof(Token), &out->tokens) ||
        !reserve_array(&cursor, l->instructions, sizeof(IrInstruction), _Alignof(IrInstruction), &out->instructions) ||
        !reserve_array(&cursor, l->locals, sizeof(IrLocal), _Alignof(IrLocal), &out->locals) ||
        !reserve_array(&cursor, l->locals, sizeof(LocalBinding), _Alignof(LocalBinding), &out->scope) ||
        !reserve_array(&cursor, l->values, sizeof(uint32_t), _Alignof(uint32_t), &out->values) ||
        !reserve_array(&cursor, l->words, sizeof(uint32_t), _Alignof(uint32_t), &out->words) ||
        !reserve_array(&cursor, l->functions, sizeof(FunctionRecord), _Alignof(FunctionRecord), &out->functions) ||
        !reserve_array(&cursor, l->formals, sizeof(FormalRecord), _Alignof(FormalRecord), &out->formals) ||
        !reserve_array(&cursor, l->arguments, sizeof(ArgumentSyntax), _Alignof(ArgumentSyntax), &out->arguments) ||
        !reserve_array(&cursor, l->physical_expressions, sizeof(uint32_t), _Alignof(uint32_t), &out->callees) ||
        !reserve_array(&cursor, l->arguments, sizeof(uint32_t), _Alignof(uint32_t), &out->typed_arguments)) return false;
    out->total = cursor;
    return true;
}
bool compiler_job_layout(const DoeCompilerJobCapacities *c, CompilerLimits *limits,
                         size_t *size_bytes) {
    if (c == NULL || c->source_bytes > UINT32_MAX - 1u || c->tokens > UINT32_MAX - 1u ||
        c->statements > UINT32_MAX - 1u || c->expressions > UINT32_MAX - 1u ||
        c->expression_depth == 0u || c->expression_depth > DOE_COMPILER_JOB_MAX_EXPRESSION_DEPTH ||
        c->control_depth == 0u || c->control_depth > DOE_COMPILER_JOB_MAX_CONTROL_DEPTH) return false;
    /* Reserve the final header bound as well as each actual target identity. */
    const uint64_t instructions = UINT64_C(1) + UINT64_C(4) * c->expressions +
                                  UINT64_C(8) * c->statements;
    const uint64_t functions = c->tokens / 6u, formals = c->tokens / 4u;
    const uint64_t target_bound = UINT64_C(256) + UINT64_C(32) * instructions + 3u * functions + formals;
    if (instructions >= UINT32_MAX || target_bound >= UINT32_MAX) return false;
    CompilerLimits candidate = { .source_bytes = c->source_bytes, .tokens = c->tokens,
        .statements = c->statements, .expressions = c->expressions,
        .physical_expressions = c->expressions + 1u, .constants = c->expressions + 1u, .instructions = (size_t)instructions,
        .values = (size_t)(instructions + formals + 1u), .locals = c->statements + 1u,
        .functions = (size_t)functions, .formals = (size_t)formals, .arguments = c->expressions,
        .words = c->words, .expression_depth = c->expression_depth,
        .control_depth = c->control_depth, .target_bound = (uint32_t)target_bound,
        .legacy_counts = false, .uniforms = true };
    JobLayout layout;
    if (!layout_job(&candidate, &layout)) return false;
    *limits = candidate;
    *size_bytes = layout.total;
    return true;
}
static void bind_counts(CompilerWorkspace *job) {
    job->program.scratch = job->target_values;
    job->syntax.statement_capacity = job->limits.statements;
    job->syntax.expression_capacity = job->limits.physical_expressions;
    job->syntax.constant_capacity = job->limits.constants;
    job->tokens.capacity = job->limits.tokens;
    job->program.instruction_capacity = job->limits.instructions;
    job->program.local_capacity = job->limits.locals;
    job->output.capacity = job->limits.words;
    job->syntax.function_capacity = job->limits.functions;
    job->syntax.formal_capacity = job->limits.formals;
    job->syntax.argument_capacity = job->limits.arguments;
    job->program.argument_capacity = job->limits.arguments;
}
void compiler_job_bind(CompilerWorkspace *job, void *memory, const CompilerLimits *limits) {
    JobLayout layout;
    (void)layout_job(limits, &layout); /* Same validated capacities, no fallible work. */
    unsigned char *bytes = memory;
    memset(job, 0, sizeof(*job));
    job->limits = *limits;
    job->syntax.statements = (Statement *)(bytes + layout.statements);
    job->syntax.expressions = (Expression *)(bytes + layout.expressions);
    job->tokens.tokens = (Token *)(bytes + layout.tokens);
    job->syntax.constants = (ConstantSyntax *)(bytes + layout.constants);
    job->program.instructions = (IrInstruction *)(bytes + layout.instructions);
    job->program.locals = (IrLocal *)(bytes + layout.locals);
    job->scope = (LocalBinding *)(bytes + layout.scope);
    job->target_values = (uint32_t *)(bytes + layout.values);
    job->output.words = (uint32_t *)(bytes + layout.words);
    job->syntax.functions = (FunctionRecord *)(bytes + layout.functions);
    job->syntax.formals = (FormalRecord *)(bytes + layout.formals);
    job->syntax.arguments = (ArgumentSyntax *)(bytes + layout.arguments);
    job->syntax.callees = (uint32_t *)(bytes + layout.callees);
    job->program.arguments = (uint32_t *)(bytes + layout.typed_arguments);
    bind_counts(job);
}
void compiler_legacy_bind(LegacyWorkspace *storage, bool uniforms) {
    CompilerWorkspace *job = &storage->job;
    memset(job, 0, sizeof(*job));
    job->limits = (CompilerLimits){ .source_bytes = DOE_COMPILER_MAX_SOURCE_BYTES,
        .tokens = DOE_COMPILER_MAX_TOKENS, .statements = MAX_STATEMENTS,
        .expressions = MAX_EXPRESSIONS, .physical_expressions = MAX_EXPRESSIONS + MAX_STATEMENTS + 1u,
        .constants = MAX_EXPRESSIONS + 1u,
        .instructions = MAX_IR_INSTRUCTIONS, .values = MAX_IR_VALUES + DOE_COMPILER_MAX_TOKENS / 4u,
        .functions = DOE_COMPILER_MAX_TOKENS / 6u, .formals = DOE_COMPILER_MAX_TOKENS / 4u, .arguments = MAX_EXPRESSIONS,
        .locals = MAX_STATEMENTS + 1u, .words = DOE_COMPILER_MAX_WORDS,
        .expression_depth = MAX_EXPRESSION_DEPTH, .control_depth = MAX_IF_DEPTH,
        .target_bound = 256u + 32u * MAX_IR_INSTRUCTIONS + 3u * (DOE_COMPILER_MAX_TOKENS / 6u) + DOE_COMPILER_MAX_TOKENS / 4u,
        .legacy_counts = true, .uniforms = uniforms };
    job->syntax.statements = storage->statements;
    job->syntax.expressions = storage->expressions;
    job->syntax.constants = storage->constants;
    job->tokens.tokens = storage->phase.tokens;
    job->program.instructions = storage->phase.lowered.instructions;
    job->program.locals = storage->phase.lowered.locals;
    job->scope = storage->phase.lowered.scope;
    job->target_values = storage->phase.lowered.target_values;
    job->output.words = storage->phase.lowered.words;
    job->syntax.functions = storage->functions;
    job->syntax.formals = storage->formals;
    job->syntax.arguments = storage->arguments;
    job->syntax.callees = storage->callees;
    job->program.arguments = storage->phase.lowered.arguments;
    bind_counts(job);
}
