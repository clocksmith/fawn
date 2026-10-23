/* SPDX-License-Identifier: MIT
 * Planning-only record/layout probe. No compiler implementation. */
#include "m11_accepted_internal.h"
#include <stdio.h>
#include <stdlib.h>
typedef struct { NameSpan name; TypeRef annotation; TypeId type; uint32_t target; } WorkgroupRoot;
typedef struct { CompilerWorkspace accepted; WorkgroupRoot *workgroups; unsigned char *uniformity;
    uint32_t workgroup_count, uniformity_flags; uint64_t workgroup_storage_bytes;
    unsigned char *statement_behaviors; } ProposedCore;
typedef struct { size_t t,e,s,i,q,m,r,x,k,l,o,d,v,w,physical,ts,ce,cc; } Profile;
static size_t add(size_t n,size_t count,size_t width,size_t alignment) {
    size_t rem=n%alignment,pad=rem?alignment-rem:0;
    if (n>SIZE_MAX-pad || count>(SIZE_MAX-n-pad)/width) abort();
    return n+pad+count*width;
}
#define ADD(type,count) n=add(n,count,sizeof(type),_Alignof(type))
static void profile(const char *name,Profile p) {
    size_t n=sizeof(ProposedCore), f=p.t/6, fp=p.t/4, g=p.t/8;
    ADD(Expression,p.physical); ADD(Statement,p.s); ADD(ConstantSyntax,p.e+1);
    ADD(FunctionRecord,f); ADD(FormalRecord,fp); ADD(ArgumentSyntax,p.e); ADD(uint32_t,p.physical);
    ADD(GenericSyntax,p.ts); ADD(TypeRecord,p.q); ADD(MemberRecord,p.m); ADD(ResourceRecord,p.r);
    ADD(WorkgroupRoot,g); ADD(ConstantRecord,p.k); ADD(ConstantValue,p.l); ADD(AccessStep,p.x);
    n=add(n,0,1,8); size_t persistent=n, token=add(0,p.t+1,sizeof(Token),_Alignof(Token));
    n=0; ADD(IrInstruction,p.i); ADD(IrLocal,p.s+1+p.x); ADD(LocalBinding,p.s+1);
    ADD(uint32_t,p.i+fp+1); ADD(uint32_t,p.o); ADD(unsigned char,p.s); n=add(n,0,1,8);size_t common=n;
    n=0; ADD(uint32_t,p.q+7); ADD(TargetDeclaration,p.d); ADD(WalkItem,p.v); ADD(uint32_t,p.w);n=add(n,0,1,8);size_t target=n;
    size_t h=2*p.cc+p.ce+4, local=p.s+1;
    size_t uniform=p.physical+local+fp+f;
    uniform=add(uniform,h,32,4); /* aligned eight-u32 metadata per frame */
    uniform=add(uniform,h,6*local,1); /* separately packed byte maps */
    uniform=add(uniform,0,1,8);
    size_t lower=common+(target>uniform?target:uniform), total=persistent+(token>lower?token:lower);
    printf("%s core=%zu root=%zu/%zu G=%zu H=%zu persistent=%zu tokens=%zu common=%zu target=%zu uniformity=%zu total=%zu alignment=8\n",name,sizeof(ProposedCore),sizeof(WorkgroupRoot),_Alignof(WorkgroupRoot),g,h,persistent,token,common,target,uniform,total);
}
int main(void) {
    _Static_assert(sizeof(CompilerWorkspace)==512,"accepted core changed");
    _Static_assert(sizeof(ProposedCore)==552,"proposal core size");
    _Static_assert(sizeof(WorkgroupRoot)==24,"proposal root size");
    profile("fixed",(Profile){2048,320,16,385,32,64,8,337,64,128,320,464,97,4096,337,32,18,4});
    profile("cli",(Profile){8192,4096,1024,24577,256,1024,32,5121,1024,4096,4096,7744,1281,65536,4097,4096,32,16});
    profile("job_cli",(Profile){8192,4096,1024,24577,32,64,8,5121,64,128,4096,464,97,65536,4097,32,32,16});
    return 0;
}
