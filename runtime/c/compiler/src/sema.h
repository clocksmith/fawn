/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_SEMA_H
#define DOE_COMPILER_SEMA_H
#include "numeric.h"
#include "stage.h"
typedef struct LoopScope LoopScope;
struct LoopScope { LoopScope *parent; size_t body_scope_start, body_scope_end; uint32_t first_continue; bool continuing; };
typedef struct { LocalBinding *values; size_t count, capacity, scope_start, level; LoopScope *loop; } LocalScope;
/* Stage flags occupy IrValue's alignment gap before the eight-byte constant
 * payload. This private semantic temporary stays 32 bytes on 32/64-bit hosts. */
typedef struct { IrValue value; bool runtime, known, overridable; union { ConstantValue constant; PointerValue pointer; }; } ExpressionValue;
_Static_assert(sizeof(ExpressionValue) == 32u, "semantic temporary layout");
typedef struct { uint32_t root, path, category; TypeId type; bool writable, addressable; } ValueReference;
typedef struct { uint64_t used, limit; uint32_t offset; } UniformityBudget;
#define UNIFORMITY_WORK_LIMIT UINT64_C(67108864)
typedef enum { IDENTITY_NONE, IDENTITY_LOCAL, IDENTITY_FORMAL, IDENTITY_INPUT,
    IDENTITY_STAGE_INPUT, IDENTITY_ALIAS, IDENTITY_CONSTANT, IDENTITY_RESOURCE, IDENTITY_WORKGROUP } ValueIdentityKind;
typedef struct { ValueIdentityKind kind; uint32_t index; } ValueIdentity;
bool uniformity_charge(Source *,UniformityBudget *,uint64_t);
bool uniformity_analyze(Source *,uint64_t,uint64_t *);
bool sema_value_identity(Source *,const LocalScope *,NameSpan,UniformityBudget *,ValueIdentity *);
enum { BEHAVIOR_NEXT=1u,BEHAVIOR_BREAK=2u,BEHAVIOR_CONTINUE=4u,BEHAVIOR_RETURN=8u };
bool constant_zero(Source *,TypeId,ConstantValue *);
bool constant_composite(Source *,TypeId,const ConstantValue *,uint32_t,bool,ConstantValue *);
bool constant_reserve(Source *,TypeId,uint32_t,uint32_t,ConstantValue *,uint32_t *);
bool constant_project(Source *,ConstantValue,uint32_t,ConstantValue *);
bool constant_materialize(Source *,uint32_t,ConstantValue,TypeId,ConstantValue *);
bool sema_expression(Source *,uint32_t,const LocalScope *,bool,bool,ExpressionValue *);
bool sema_select(Source *,const Expression *,ExpressionValue [3],bool,bool,ExpressionValue *);
bool sema_ensure_value(Source *,ExpressionValue *,bool);
bool sema_materialize(Source *,ExpressionValue *,TypeId);
bool sema_type_reference(Source *,TypeRef,const LocalScope *,TypeId *);
bool sema_common_type(Source *,TypeId,TypeId,uint32_t,TypeId *);
bool sema_default_type(Source *,TypeId,uint32_t,TypeId *);
bool sema_resolve_declarations(Source *,LocalScope *);
bool integer_attribute(Source *, uint32_t, const LocalScope *, bool, uint32_t *);
bool sema_stage_interfaces(Source *, const LocalScope *);
bool sema_valid_name(Source *,NameSpan);
bool sema_module_declared(const Source *,NameSpan);
uint32_t sema_find_function(const Source *,NameSpan);
uint32_t sema_find_constant(const Source *,NameSpan);
uint32_t sema_find_type_alias(const Source *,NameSpan);
uint32_t sema_find_struct(const Source *,NameSpan);
uint32_t sema_find_formal(const Source *,NameSpan);
uint32_t sema_find_parameter(const Source *,NameSpan);
const LocalBinding *sema_find_local(const Source *,const LocalScope *,NameSpan);
bool sema_alias_visible(const Source *);
bool sema_local_use(Source *,const LocalScope *,const LocalBinding *,uint32_t);
bool sema_append(Source *,IrInstruction,IrValue *);
bool sema_emit(Source *,bool,IrInstruction,IrValue *);
bool sema_reference_load(Source *,ValueReference,uint32_t,bool,ExpressionValue *);
bool sema_assignment_reference(Source *,const Assignment *,const LocalScope *,ValueReference *,SourceReference *);
bool sema_matrix_result(Source *, NumericBuiltinKind, TypeId, uint32_t, TypeId *);
bool sema_matrix_binary(Source *, ExpressionKind, ExpressionValue *, ExpressionValue *, uint32_t, bool, bool, ExpressionValue *);
bool sema_binary(Source *,ExpressionKind,Comparison,ExpressionValue *,ExpressionValue *,uint32_t,bool,bool,ExpressionValue *);
bool sema_call_statement(Source *,uint32_t,const LocalScope *);
/* The call dispatcher validates builtin arity, type arguments and result use. */
bool sema_constant_lane(Source *, ConstantValue, uint32_t, NumericValue *);
bool sema_finish_lanes(Source *, TypeId, ConstantValue[4], uint32_t, ConstantValue *);
bool sema_swizzle_value(Source *, const Expression *, ExpressionValue *, TypeId, uint32_t, bool, bool, ExpressionValue *);
bool sema_unary_value(Source *, const Expression *, ExpressionValue *, bool, bool, ExpressionValue *);
bool sema_packed_integer(Source *, uint32_t, const NumericBuiltin *, ExpressionValue *, bool, ExpressionValue *);
bool sema_builtin_finish(Source *, const Expression *, const NumericBuiltin *, ExpressionValue *, bool, ExpressionValue *);
bool sema_image_component(Source *, ExpressionValue *);
bool sema_image_finish(Source *, const Expression *, TypeId, TypeId, ImageOperation, uint32_t, bool, ExpressionValue *);
TypeId sema_image_result(Source *, const Expression *, const TypeRecord *, ImageOperation);
bool sema_image_evaluate(const CompilerWorkspace *,TypeId,ImageOperation,uint32_t,bool);
bool sema_image_argument(Source *, const TypeRecord *, ImageOperation, uint32_t, ExpressionValue *);
bool sema_image_auxiliaries(Source *, TypeId, ImageOperation);
bool sema_image_type(Source *, TypeRef, TypeId, TypeId *);
uint32_t image_builtin_kind(const Source *, NameSpan);
uint32_t atomic_builtin_kind(const Source *, NameSpan);
bool atomic_opcode(IrOpcode);
TypeId atomic_element(const CompilerWorkspace *, const IrInstruction *);
bool sema_address_reference(Source *, uint32_t, const LocalScope *, bool, bool, ValueReference *);
bool sema_pointer_argument(Source *, uint32_t, const ExpressionValue *);
bool sema_pointer_aliases(Source *);
bool sema_array_length(Source *, uint32_t, const LocalScope *, bool, ExpressionValue *);
bool sema_call_value(Source *,uint32_t,const LocalScope *,bool,bool,bool,ExpressionValue *);
#endif
