/* Source-only planning probe. Not compiler implementation or acceptance. */
#include "../src/internal.h"
#include "../include/doe_compiler_module.h"
#include <stdio.h>
#include <stdint.h>
typedef struct { uint32_t kind, index; } ModuleSourceReference;
typedef struct { NameSpan name; InputKind kind; uint32_t flags, target; } ModuleEntryInput;
typedef struct {
    uint32_t function, input_first, input_count, source_input_mask;
    union {
        Integer dimensions[3];
        struct {
            uint32_t workgroup_size[3], resource_first, resource_count;
            uint32_t workgroup_first, workgroup_count, local_index_target, target_input_mask;
            uint64_t workgroup_storage_bytes;
        } resolved;
    } state;
} ModuleEntry;
typedef struct {
    CompilerLimits base;
    uint32_t entries, entry_inputs, entry_references;
} ModuleLimits;
typedef struct {
    ModuleLimits limits;
    union { Token *tokens; IrInstruction *instructions; };
    Expression *expressions; Statement *statements; ConstantSyntax *constant_decls;
    FunctionRecord *functions; FormalRecord *formals; ArgumentSyntax *arguments; GenericSyntax *generic_syntax;
    TypeRecord *types; MemberRecord *members; ResourceRecord *resources; ConstantRecord *constants;
    ConstantValue *constant_edges; AccessStep *accesses; IrLocal *locals;
    LocalBinding *scope; TargetDeclaration *targets; WalkItem *walk;
    uint32_t *values, *typed_operands, *words, *reflection_map;
    ModuleSourceReference *expression_references, *statement_references;
    ModuleEntry *entries; ModuleEntryInput *entry_inputs; uint32_t *entry_references;
    uint32_t token_count, expression_count, expression_budget_count, statement_count, constant_decl_count;
    uint32_t function_count, formal_count, argument_count, generic_count, type_count, member_count;
    uint32_t resource_count, constant_count, edge_count, access_count, instruction_count, local_count;
    uint32_t operand_count, target_count, walk_count, current_function;
    uint32_t entry_count, entry_input_count, entry_reference_count;
    size_t word_count;
    NameSpan alias, alias_source;
    WorkgroupRoot *workgroups; unsigned char *uniformity;
    uint32_t workgroup_count, uniformity_flags;
    unsigned char *statement_behaviors;
} ModuleCore;
static size_t add(size_t c,size_t n,size_t w,size_t a) { return (c+a-1u)/a*a+n*w; }
#define ADD(c,n,type) c=add(c,n,sizeof(type),_Alignof(type))
static void layout(const char *name,size_t T,size_t E,size_t EP,size_t S,size_t Q,size_t TS,size_t M,size_t R,size_t X,size_t K,size_t L,size_t I,size_t O,size_t D,size_t V,size_t W,size_t DE,size_t DC,size_t J,size_t B,size_t C) {
    size_t p=sizeof(ModuleCore),F=T/6u,P=T/4u,G=T/8u;
    ADD(p,EP,Expression); ADD(p,S,Statement); ADD(p,E+1u,ConstantSyntax); ADD(p,F,FunctionRecord); ADD(p,P,FormalRecord); ADD(p,E,ArgumentSyntax); ADD(p,TS,GenericSyntax); ADD(p,Q,TypeRecord); ADD(p,M,MemberRecord); ADD(p,R,ResourceRecord); ADD(p,G,WorkgroupRoot); ADD(p,K,ConstantRecord); ADD(p,L,ConstantValue); ADD(p,X,AccessStep); ADD(p,J,ModuleEntry); ADD(p,B,ModuleEntryInput); ADD(p,C,uint32_t); p=add(p,0,1,8);
    size_t tok=p; ADD(tok,T+1u,Token);
    size_t common=p; ADD(common,I,IrInstruction); ADD(common,S+1u+X,IrLocal); ADD(common,S+1u,LocalBinding); ADD(common,I+P+1u,uint32_t); ADD(common,O,uint32_t); common+=S; common=add(common,0,1,8);
    size_t target=common; ADD(target,Q+7u,uint32_t); ADD(target,D,TargetDeclaration); ADD(target,V,WalkItem); ADD(target,W,uint32_t);
    size_t refs=common; ADD(refs,EP,ModuleSourceReference); ADD(refs,S,ModuleSourceReference);
    size_t uniform=refs,frames=2u*DC+DE+4u;
    uniform+=EP+S+1u+P+F; uniform=add(uniform,frames,32,4); uniform+=frames*6u*(S+1u); uniform=add(uniform,0,1,8);
    size_t closure=refs; closure+=R+G; ADD(closure,R,uint32_t); closure=add(closure,0,1,8);
    size_t total=target>uniform?target:uniform; if (total<closure) total=closure; if(total<tok)total=tok; total=add(total,0,1,8);
    printf("%s: persistent=%zu common=%zu token=%zu target=%zu refs=%zu uniform_after_refs=%zu closure_after_refs=%zu total=%zu J=%zu B=%zu C=%zu\n",name,p,common-p,tok-p,target-common,refs-common,uniform-refs,closure-refs,total,J,B,C);
}
int main(void) {
    printf("proposed ABI5: capacities=%zu selection=%zu workgroup=%zu entry=%zu counts=%zu outputs=%zu module=%zu result=%zu\n", sizeof(DoeCompilerModuleCapacities), sizeof(DoeCompilerEntrySelectionV5), sizeof(DoeCompilerWorkgroupV5), sizeof(DoeCompilerEntryV5), sizeof(DoeCompilerModuleOutputCounts), sizeof(DoeCompilerModuleOutputs), sizeof(DoeCompilerModuleV5), sizeof(DoeCompilerModuleResult));
    printf("planning only: source_reference=%zu/%zu entry_input=%zu/%zu entry=%zu/%zu limits=%zu core=%zu/%zu; old_core=%zu old_fixed=%zu\n",sizeof(ModuleSourceReference),_Alignof(ModuleSourceReference),sizeof(ModuleEntryInput),_Alignof(ModuleEntryInput),sizeof(ModuleEntry),_Alignof(ModuleEntry),sizeof(ModuleLimits),sizeof(ModuleCore),_Alignof(ModuleCore),sizeof(CompilerWorkspace),sizeof(LegacyWorkspace));
    layout("fixed-singular",2048,320,337,16,32,32,64,8,337,64,128,385,320,464,97,4096,18,4,1,5,264);
    layout("ABI4-binary-profile",8192,4096,4097,1024,256,4096,1024,32,5121,1024,4096,24577,4096,7744,1281,65536,32,16,8192/13,8192/8,(8192/13)*(32+8192/8));
    layout("ABI5-example-not-CLI-policy",8192,4096,4097,1024,256,4096,1024,32,5121,1024,4096,24577,4096,7744,1281,65536,32,16,32,160,2048);
    return 0;
}
