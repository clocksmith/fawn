/* SPDX-License-Identifier: MIT
 * Actual candidate-private layout measurement, not a public ABI guarantee. */
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#define TYPE(t) printf("\"" #t "\":{\"size\":%zu,\"alignment\":%zu},",sizeof(t),_Alignof(t))
#define SLICE(label, pointer, count, type) printf("\"" label "\":{\"offset\":%zu,\"count\":%zu,\"element\":%zu,\"alignment\":%zu},",(size_t)((unsigned char *)(pointer)-(unsigned char *)job),(size_t)(count),sizeof(type),_Alignof(type))
int main(void) {
    DoeCompilerJobCapacities caps={65536u,8192u,1024u,4096u,65536u,32u,16u};
    CompilerLimits limits;
    size_t bytes;
    if (!compiler_job_layout(&caps,&limits,&bytes)) return 1;
    CompilerWorkspace *job=malloc(bytes);
    if (job==NULL) return 2;
    compiler_job_bind(job,job,&limits);
    printf("{\"types\":{");
    TYPE(CompilerLimits); TYPE(Source); TYPE(Token); TYPE(Name); TYPE(NameSpan);
    TYPE(Expression); TYPE(Statement); TYPE(Syntax); TYPE(IrInstruction); TYPE(IrLocal);
    TYPE(LocalBinding); TYPE(IrProgram); TYPE(Spirv); TYPE(CompilerWorkspace);
    printf("\"LegacyWorkspace\":{\"size\":%zu,\"alignment\":%zu}},",sizeof(LegacyWorkspace),_Alignof(LegacyWorkspace));
    printf("\"legacy\":{\"statements_offset\":%zu,\"expressions_offset\":%zu,\"phase_offset\":%zu,\"phase_size\":%zu,\"tokens_size\":%zu,\"lowered_size\":%zu},",offsetof(LegacyWorkspace,statements),offsetof(LegacyWorkspace,expressions),offsetof(LegacyWorkspace,phase),sizeof(((LegacyWorkspace *)0)->phase),sizeof(((LegacyWorkspace *)0)->phase.tokens),sizeof(((LegacyWorkspace *)0)->phase.lowered));
    printf("\"slices\":{");
    SLICE("statements",job->syntax.statements,limits.statements,Statement);
    SLICE("expressions",job->syntax.expressions,limits.physical_expressions,Expression);
    SLICE("tokens",job->tokens.tokens,limits.tokens+1u,Token);
    SLICE("instructions",job->program.instructions,limits.instructions,IrInstruction);
    SLICE("locals",job->program.locals,limits.locals,IrLocal);
    SLICE("scope",job->scope,limits.locals,LocalBinding);
    SLICE("values",job->target_values,limits.values,uint32_t);
    printf("\"words\":{\"offset\":%zu,\"count\":%zu,\"element\":%zu,\"alignment\":%zu}},",(size_t)((unsigned char *)job->output.words-(unsigned char *)job),limits.words,sizeof(uint32_t),_Alignof(uint32_t));
    printf("\"total\":%zu,\"instructions\":%zu,\"target_bound\":%u}\n",bytes,limits.instructions,limits.target_bound);
    free(job);
    return 0;
}
