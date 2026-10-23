/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../src/sema.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DoeCompilerTypedCapacities capacities={
    .source_bytes=262144u,.tokens=8192u,.statements=1024u,.expressions=4096u,
    .types=256u,.members=1024u,.resources=32u,.access_steps=5121u,
    .constants=1024u,.constant_edges=4096u,.ir_instructions=24577u,.ir_operands=4096u,
    .target_declarations=7744u,.traversal_items=1281u,.words=65536u,
    .expression_depth=32u,.control_depth=16u,.type_syntax_depth=32u};
static char *read_source(const char *path,size_t *length) {
    FILE *file=fopen(path,"rb"); assert(file!=NULL);
    assert(fseek(file,0,SEEK_END)==0); const long size=ftell(file); assert(size>=0);
    assert(fseek(file,0,SEEK_SET)==0); *length=(size_t)size;
    char *text=malloc(*length+1u); assert(text!=NULL);
    assert(fread(text,1u,*length,file)==*length); assert(fclose(file)==0); text[*length]='\0'; return text;
}
static uint64_t measure(const char *text,size_t length) {
    CompilerLimits limits; size_t bytes;
    assert(compiler_typed_layout(&capacities,&limits,&bytes)); assert(bytes==2536128u);
    CompilerWorkspace *c=malloc(bytes); assert(c!=NULL); compiler_job_bind(c,c,&limits);
    DoeCompilerDiagnostic diagnostic={0}; uint32_t pool=0u; size_t minimum=0u;
    Source source={text,length,&diagnostic,&c->limits,c,&pool,&minimum};
    assert(lex_source(&source,c)); assert(parse_tokens(&source,c));
    /* The parse union now contains common lowering and phased scratch. */
    c->instruction_count=0u; c->local_count=0u;
    if (!validate_syntax(&source,c)) {fprintf(stderr,"%u:%s\n",diagnostic.line,diagnostic.message);abort();}
    uint64_t work=0u;
    assert(uniformity_analyze(&source,UNIFORMITY_WORK_LIMIT,&work));
    if (work!=0u) {
        uint64_t exact=0u; assert(uniformity_analyze(&source,work,&exact)); assert(exact==work);
        diagnostic=(DoeCompilerDiagnostic){0};
        uint64_t short_work=0u; assert(!uniformity_analyze(&source,work-1u,&short_work));
        assert(diagnostic.status==DOE_COMPILER_UNSUPPORTED);
        assert(strcmp(diagnostic.message,"uniformity analysis work limit exceeded")==0);
        assert(pool==DOE_COMPILER_POOL_NONE && minimum==0u && short_work<=work-1u);
        diagnostic=(DoeCompilerDiagnostic){0}; assert(uniformity_analyze(&source,UNIFORMITY_WORK_LIMIT,NULL));
    }
    assert(emit_spirv(&source,c));
    printf("work=%llu bytes=%zu core=%zu root=%zu instructions=%u expressions=%u statements=%u types=%u targets=%u ids=%u words=%zu logical_shared=%llu\n",
        (unsigned long long)work,bytes,sizeof(CompilerWorkspace),sizeof(WorkgroupRoot),c->instruction_count,c->expression_count,
        c->statement_count,c->type_count,c->target_count,c->words[3],c->word_count,(unsigned long long)c->workgroup_storage_bytes);
    free(c); return work;
}
int main(int argc,char **argv) {
    assert(sizeof(LegacyWorkspace)==146256u);
    for (int i=1;i<argc;++i) {size_t length;char *source=read_source(argv[i],&length);printf("%s ",argv[i]);(void)measure(source,length);free(source);}
    const char plain[]="@compute @workgroup_size(1) fn main(){var x=0u;x++;}";
    assert(measure(plain,sizeof(plain)-1u)==0u);
    const char sync[]="fn f(x:u32)->u32{var y=x;loop{if(y>2u){break;}y++;}return y;}@compute @workgroup_size(1) fn main(){if(f(0u)>0u){workgroupBarrier();}}";
    assert(measure(sync,sizeof(sync)-1u)>0u);
    return 0;
}
