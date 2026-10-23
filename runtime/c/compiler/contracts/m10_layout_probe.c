/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
/* Planning-only C11 data-layout probe. Not compiler implementation. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { uint32_t offset, length; } Span;
typedef struct { uint32_t offset, syntax; } TypeRef;
typedef struct { uint32_t type, flags; uint64_t bits_or_id; } ConstValue;
typedef struct { uint32_t kind, offset, length; } Token;
typedef struct { uint32_t offset, kind, depth, left, right; uint32_t payload[4]; uint32_t flags; } Expr;
typedef struct { uint32_t kind, offset; uint32_t payload[7]; uint32_t body, alternate, end, init, update; } Stmt;
typedef struct { Span name; TypeRef annotation; uint32_t first_expr, expr; uint64_t payload; uint32_t type, state; } ConstantDecl;
typedef struct { Span name; TypeRef annotation; uint32_t formal_first,formal_count,stmt_first,stmt_end,expr_first,expr_end,ir_first,ir_end,local_first,local_count,result,flags,value_base; uint32_t state[3]; } Function;
typedef struct { Span name; union { TypeRef syntax; struct { uint32_t type, offset; } resolved; } annotation; } Formal;
typedef struct { uint32_t expr, next; } Argument;
typedef struct { Span name; TypeRef element; uint32_t count_expr, flags; } GenericSyntax;
typedef struct { uint32_t kind,flags,a,b; Span name; uint64_t size,alignment,stride; uint32_t target,aux; } Type;
typedef struct { Span name; TypeRef syntax; uint32_t align_expr,size_expr,type,flags; uint64_t offset,size,alignment; } Member;
typedef struct { Span name; TypeRef syntax; uint32_t group_expr,binding_expr,group,binding,kind,access,type,flags; uint64_t minimum,alignment,required_alignment,tail_offset,tail_stride; uint32_t tail_type,target,source_offset,reserved; } Resource;
typedef struct { uint32_t kind,type,first,count; uint64_t payload; uint32_t target,aux; } Constant;
typedef struct { uint32_t parent,source_offset,input_type,output_type,kind,index; } Access;
typedef struct { uint32_t opcode,type,result,source_offset; uint32_t payload[4]; } Ir;
typedef struct { uint32_t type,flags; } Local;
typedef struct { Span name; uint32_t value,type; ConstValue constant; uint32_t local,offset,flags,level; } Binding;
typedef struct { uint32_t kind,id,type,storage,first,count; uint64_t payload; } TargetDecl;
typedef struct { uint32_t kind,object,cursor,parent; } Walk;
typedef struct { Span name; uint32_t kind,flags; } Input;
typedef struct { uint32_t source,tokens,statements,expressions,physical_expr,instructions,types,members,resources,accesses,constants,edges,targets,generic_syntax,walk,words,expression_depth,control_depth,type_depth; } Limits;
typedef struct {
 Limits limits;
 const char *source; size_t source_length; void *diagnostic;
 Token *tokens; Expr *expressions; Stmt *statements; ConstantDecl *constant_decls;
 Function *functions; Formal *formals; Argument *arguments; GenericSyntax *generic_syntax;
 Type *types; Member *members; Resource *resources; Constant *constants;
 ConstValue *constant_edges; Access *accesses; Ir *instructions; Local *locals;
 Binding *scope; TargetDecl *targets; Walk *walk; uint32_t *values,*typed_operands,*words,*callees,*reflection_map;
 uint32_t token_count,expression_count,statement_count,constant_decl_count,function_count,formal_count,argument_count,generic_count,type_count,member_count,resource_count,constant_count,edge_count,access_count,instruction_count,local_count,scope_count,target_count,walk_count,word_count,next_target_id,entry_function,current_function;
 Span entry,alias,alias_source; Input inputs[5]; uint32_t input_count,local_size[3];
} Core;
enum { T=2048,E=320,S=16,I=385,F=T/6,P=T/4,Q=32,M=64,R=8,X=E+S+1,K=64,L=128,TS=32,V=Q+K+1,D=6*Q+2*R+K+L+M,W=4096 };
typedef struct {
 Core core;
 Expr expressions[E+S+1]; Stmt statements[S]; ConstantDecl constant_decls[E+1];
 Function functions[F]; Formal formals[P]; Argument arguments[E]; uint32_t callees[E+S+1];
 GenericSyntax generic_syntax[TS]; Type types[Q]; Member members[M]; Resource resources[R];
 Constant constants[K]; ConstValue constant_edges[L]; Access accesses[X];
 union {
   Token tokens[T+1];
   struct {
     Ir instructions[I]; Local locals[S+1+X]; Binding scope[S+1];
     uint32_t values[I+P+1],typed_operands[E],reflection_map[Q+7];
     TargetDecl targets[D]; Walk walk[V]; uint32_t words[W];
   } lowered;
 } phase;
} Fixed;
/* Taking the whole object's address into libc forces a real bounded frame.
 * This measures only the proposed wrapper storage, never an implementation path. */
__attribute__((noinline)) void planning_frame(void) { Fixed workspace; printf("%p", (void *)&workspace); }
#define ROW(TY) printf("%-16s size=%zu align=%zu\n",#TY,sizeof(TY),_Alignof(TY))
int main(void) {
 ROW(Core);ROW(Expr);ROW(Stmt);ROW(ConstantDecl);ROW(Function);ROW(Formal);ROW(GenericSyntax);
 ROW(Type);ROW(Member);ROW(Resource);ROW(Constant);ROW(ConstValue);ROW(Access);ROW(Ir);
 ROW(Local);ROW(Binding);ROW(TargetDecl);ROW(Walk);ROW(Fixed);
 printf("persistent_offset=%zu lowered=%zu tokens=%zu total=%zu target_capacity=%d traversal=%d\n",offsetof(Fixed,phase),sizeof(((Fixed*)0)->phase.lowered),sizeof(((Fixed*)0)->phase.tokens),sizeof(Fixed),D,V);
 return 0;
}
