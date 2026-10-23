/* Independent post-handoff actual private-record/slice measurement. */
#include "storage.c"
#include "uniformity.c"
#include <stdio.h>
#include <stdlib.h>
#define ROW(T) printf("RECORD %s %zu %zu\n",#T,sizeof(T),_Alignof(T))
#define FIELD(P,F) printf("SLICE %s %s %zu\n",name,#F,(P).F)
static void profile(const char *name,const CompilerLimits *l){WorkspaceLayout p;if(!workspace_layout(l,&p))abort();
 FIELD(p,expressions);FIELD(p,statements);FIELD(p,declarations);FIELD(p,functions);FIELD(p,formals);FIELD(p,arguments);FIELD(p,callees);FIELD(p,generic_syntax);FIELD(p,types);FIELD(p,members);FIELD(p,resources);FIELD(p,workgroups);FIELD(p,constants);FIELD(p,edges);FIELD(p,accesses);FIELD(p,phase);FIELD(p,locals);FIELD(p,scope);FIELD(p,values);FIELD(p,operands);FIELD(p,behaviors);FIELD(p,uniformity);FIELD(p,reflection);FIELD(p,targets);FIELD(p,walk);FIELD(p,words);FIELD(p,total);
 printf("LIMITS %s %u %u %u %u %u %u %u %u %u %u %u %u %u %zu %u %u %u %u\n",name,l->tokens,l->expressions,l->statements,l->instructions,l->types,l->members,l->resources,l->accesses,l->constants,l->edges,l->operands,l->targets,l->traversal,l->words,l->physical_expressions,l->generic_syntax,l->expression_depth,l->control_depth);
}
int main(void){ROW(CompilerWorkspace);ROW(Expression);ROW(Statement);ROW(ConstantSyntax);ROW(FunctionRecord);ROW(FormalRecord);ROW(ArgumentSyntax);ROW(GenericSyntax);ROW(TypeRecord);ROW(MemberRecord);ROW(ResourceRecord);ROW(WorkgroupRoot);ROW(ConstantRecord);ROW(ConstantValue);ROW(AccessStep);ROW(IrInstruction);ROW(IrLocal);ROW(LocalBinding);ROW(TargetDeclaration);ROW(WalkItem);ROW(Token);ROW(LegacyWorkspace);ROW(UniformityFrame);
 LegacyWorkspace *fixed=malloc(sizeof(*fixed));if(!fixed)return 1;compiler_legacy_bind(fixed,false);profile("fixed",&fixed->job.limits);
 printf("LEGACY phase %zu uniformity %zu reflection %zu size %zu\n",offsetof(LegacyWorkspace,phase),offsetof(LegacyWorkspace,phase.lowered.scratch.uniformity),offsetof(LegacyWorkspace,phase.lowered.scratch.target.reflection_map),sizeof(*fixed));
 const DoeCompilerTypedCapacities typed={65536u,8192u,1024u,4096u,256u,1024u,32u,5121u,1024u,4096u,24577u,4096u,7744u,1281u,65536u,32u,16u,32u,0u};CompilerLimits l;size_t bytes;if(!compiler_typed_layout(&typed,&l,&bytes))return 2;profile("typed_cli",&l);
 const DoeCompilerJobCapacities job={65536u,8192u,1024u,4096u,65536u,32u,16u};if(!compiler_job_layout(&job,&l,&bytes))return 3;profile("job_cli",&l);
 DoeCompilerWorkspaceRequirements r;doe_compiler_workspace_requirements(2u,&r,sizeof(r));printf("PUBLIC fixed %zu %zu\n",r.size_bytes,r.alignment_bytes);doe_compiler_typed_requirements(4u,&typed,sizeof(typed),&r,sizeof(r));printf("PUBLIC typed_cli %zu %zu\n",r.size_bytes,r.alignment_bytes);doe_compiler_job_requirements(3u,&job,sizeof(job),&r,sizeof(r));printf("PUBLIC job_cli %zu %zu\n",r.size_bytes,r.alignment_bytes);free(fixed);return 0;}
