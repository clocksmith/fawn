/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include "override.h"
#include <string.h>

static const char *const forbidden_names[] = {
    "alias", "break", "case", "const", "const_assert", "continue", "continuing", "default", "diagnostic", "discard", "else", "enable", "false", "fn", "for", "if", "let", "loop", "override", "requires", "return", "struct", "switch", "true", "var", "while", "NULL", "Self", "abstract", "active", "alignas", "alignof", "as", "asm", "asm_fragment", "async", "attribute", "auto", "await", "become", "cast", "catch", "class", "co_await", "co_return", "co_yield", "coherent", "column_major", "common", "compile", "compile_fragment", "concept", "const_cast", "consteval", "constexpr", "constinit", "crate", "debugger", "decltype", "delete", "demote", "demote_to_helper", "do", "dynamic_cast", "enum", "explicit", "export", "extends", "extern", "external", "fallthrough", "filter", "final", "finally", "friend", "from", "fxgroup", "get", "goto", "groupshared", "highp", "impl", "implements", "import", "inline", "instanceof", "interface", "layout", "lowp", "macro", "macro_rules", "match", "mediump", "meta", "mod", "module", "move", "mut", "mutable", "namespace", "new", "nil", "noexcept", "noinline", "nointerpolation", "non_coherent", "noncoherent", "noperspective", "null", "nullptr", "of", "operator", "package", "packoffset", "partition", "pass", "patch", "pixelfragment", "precise", "precision", "premerge", "priv", "protected", "pub", "public", "readonly", "ref", "regardless", "register", "reinterpret_cast", "require", "resource", "restrict", "self", "set", "shared", "sizeof", "smooth", "snorm", "static", "static_assert", "static_cast", "std", "subroutine", "super", "target", "template", "this", "thread_local", "throw", "trait", "try", "type", "typedef", "typeid", "typename", "typeof", "union", "unless", "unorm", "unsafe", "unsized", "use", "using", "varying", "virtual", "volatile", "wgsl", "where", "with", "writeonly", "yield"
};
bool sema_valid_name(Source *s, NameSpan name) {
    if (source_span_is(s, name, "_") || (name.length >= 2u && s->source[name.offset] == '_' && s->source[name.offset+1u] == '_'))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "reserved WGSL identifier spelling");
    for (size_t i = 0u;i<sizeof(forbidden_names)/sizeof(forbidden_names[0]);++i)
        if (source_span_is(s, name, forbidden_names[i])) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "reserved WGSL keyword or word cannot name a declaration");
    return true;
}
uint32_t sema_find_constant(const Source *s, NameSpan name) {
    const CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u;i<c->constant_decl_count;++i)
        if (c->constant_decls[i].expression != NO_ID && source_spans_equal(s, name, c->constant_decls[i].name)) return i;
    return NO_ID;
}
uint32_t sema_find_type_alias(const Source *s, NameSpan name) {
    const CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u; i < c->constant_decl_count; ++i)
        if (c->constant_decls[i].expression == NO_ID && source_spans_equal(s, name, c->constant_decls[i].name)) return i;
    return NO_ID;
}
uint32_t sema_find_function(const Source *s, NameSpan name) {
    const CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u;i<c->function_count;++i) if (source_spans_equal(s, name, c->functions[i].name)) return i;
    return NO_ID;
}
uint32_t sema_find_struct(const Source *s, NameSpan name) {
    const CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u;i<c->type_count;++i)
        if (c->types[i].kind == TYPE_STRUCT && source_spans_equal(s, name, c->types[i].name)) return i+IR_PRIMITIVE_COUNT;
    return NO_ID;
}
bool sema_module_declared(const Source *s, NameSpan name) {
    if (sema_find_constant(s, name) != NO_ID || sema_find_type_alias(s, name) != NO_ID || sema_find_function(s, name) != NO_ID || sema_find_struct(s, name) != NO_ID) return true;
    for (uint32_t i = 0u;i<s->compiler->resource_count;++i) if (source_spans_equal(s, name, s->compiler->resources[i].name)) return true;
    for (uint32_t i = 0u;i<s->compiler->workgroup_count;++i) if (source_spans_equal(s, name, s->compiler->workgroups[i].name)) return true;
    return false;
}
bool sema_alias_visible(const Source *s) {
    return s->compiler->entry_count!=0u && s->compiler->current_function == s->compiler->entries[0].function && s->compiler->alias.length != 0u;
}
uint32_t sema_find_parameter(const Source *s, NameSpan name) {
    const CompilerWorkspace *c = s->compiler;
    if (c->current_function>=c->function_count || (c->functions[c->current_function].flags&FUNCTION_ENTRY)==0u) return NO_ID;
    const EntryRecord *entry=&c->entries[c->functions[c->current_function].entry_index];
    for (uint32_t i=entry->input_first;i<entry->input_first+entry->input_count;++i)
        if (source_spans_equal(s,name,c->entry_inputs[i].name)) return i;
    return NO_ID;
}
uint32_t sema_find_formal(const Source *s, NameSpan name) {
    const CompilerWorkspace *c = s->compiler;
    if (c->current_function >= c->function_count) return NO_ID;
    const FunctionRecord *f = &c->functions[c->current_function];
    if (stage_graphics(f)) return stage_parameter(s, name);
    for (uint32_t i = 0u;i<f->formal_count;++i) if (source_spans_equal(s, name, c->formals[f->formal_start+i].name)) return f->formal_start+i;
    return NO_ID;
}
const LocalBinding *sema_find_local(const Source *s, const LocalScope *scope, NameSpan name) {
    for (size_t i = scope->count;i != 0u;--i) if (source_spans_equal(s, name, scope->values[i-1u].name)) return &scope->values[i-1u];
    return NULL;
}
/* Shared read-only value identity. The analysis supplies a budget; normal
 * lowering uses the same precedence with no analysis accounting. */
static bool identity_name(Source *s,NameSpan a,NameSpan b,UniformityBudget *budget,bool *same) {
    if (budget!=NULL && !uniformity_charge(s,budget,1u)) return false;
    *same=source_spans_equal(s,a,b); return true;
}
bool sema_value_identity(Source *s,const LocalScope *scope,NameSpan name,UniformityBudget *budget,ValueIdentity *out) {
    const CompilerWorkspace *c=s->compiler; bool same=false;
    *out=(ValueIdentity){IDENTITY_NONE,NO_ID};
    for (size_t i=scope->count;i!=0u;--i) {
        if (!identity_name(s,name,scope->values[i-1u].name,budget,&same)) return false;
        if (same) {*out=(ValueIdentity){IDENTITY_LOCAL,(uint32_t)i-1u};return true;}
    }
    if (c->current_function<c->function_count) {
        const FunctionRecord *f=&c->functions[c->current_function];
        for (uint32_t i=0u;i<f->formal_count;++i) {
            if (!identity_name(s,name,c->formals[f->formal_start+i].name,budget,&same)) return false;
            if (same) {*out=(ValueIdentity){IDENTITY_FORMAL,f->formal_start+i};return true;}
        }
    }
    if (budget != NULL && c->current_function < c->function_count && stage_graphics(&c->functions[c->current_function]) &&
        !uniformity_charge(s, budget, s->stages->entries[c->functions[c->current_function].entry_index].formal_count)) return false;
    const uint32_t stage_formal = stage_parameter(s, name);
    if (stage_formal != NO_ID) {*out = (ValueIdentity){IDENTITY_STAGE_INPUT, stage_formal}; return true;}
    if (sema_alias_visible(s)) {
        if (!identity_name(s,name,c->alias,budget,&same)) return false;
        if (same) {*out=(ValueIdentity){IDENTITY_ALIAS,0u};return true;}
    }
    if (c->current_function<c->function_count && (c->functions[c->current_function].flags&FUNCTION_ENTRY)!=0u) {
        const EntryRecord *entry=&c->entries[c->functions[c->current_function].entry_index];
        for (uint32_t i=entry->input_first;i<entry->input_first+entry->input_count;++i) {
            if (!identity_name(s,name,c->entry_inputs[i].name,budget,&same)) return false;
            if (same) {*out=(ValueIdentity){IDENTITY_INPUT,i};return true;}
        }
    }
    for (uint32_t i=0u;i<c->constant_decl_count;++i) {
        if (c->constant_decls[i].expression == NO_ID) continue;
        if (!identity_name(s,name,c->constant_decls[i].name,budget,&same)) return false;
        if (same) {*out=(ValueIdentity){IDENTITY_CONSTANT,i};return true;}
    }
    for (uint32_t i=0u;i<c->resource_count;++i) {
        if (!identity_name(s,name,c->resources[i].name,budget,&same)) return false;
        if (same) {*out=(ValueIdentity){IDENTITY_RESOURCE,i};return true;}
    }
    for (uint32_t i=0u;i<c->workgroup_count;++i) {
        if (!identity_name(s,name,c->workgroups[i].name,budget,&same)) return false;
        if (same) {*out=(ValueIdentity){IDENTITY_WORKGROUP,i};return true;}
    }
    return true;
}
bool sema_local_use(Source *s, const LocalScope *scope, const LocalBinding *b, uint32_t offset) {
    const size_t index = (size_t)(b-scope->values);
    for (const LoopScope *l = scope->loop;l != NULL;l = l->parent)
        if (l->continuing && index >= l->body_scope_start && index<l->body_scope_end && b->declaration_offset>l->first_continue)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "continuing references a declaration bypassed by continue");
    return true;
}
static bool lexical_value(const Source *s, const LocalScope *scope, NameSpan name) {
    return sema_find_local(s, scope, name) != NULL || sema_find_formal(s, name) != NO_ID || sema_find_parameter(s, name) != NO_ID || (sema_alias_visible(s) && source_spans_equal(s, name, s->compiler->alias));
}
static bool base_type(Source *s, NameSpan name, const LocalScope *scope, TypeId *out, uint32_t *width) {
    *width = 0u;
    if (lexical_value(s, scope, name)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "type name resolves to a lexical value");
    const uint32_t alias = sema_find_type_alias(s, name);
    if (alias != NO_ID) {
        const ConstantSyntax *declaration = &s->compiler->constant_decls[alias];
        if ((declaration->state & 3u) != 2u)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "unresolved or cyclic type alias");
        *out = declaration->type;
        return true;
    }
    const TypeId nominal = sema_find_struct(s, name);
    if (nominal != NO_ID) {
        if ((type_flags(s->compiler, nominal)&TYPE_RESOLVED) == 0u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "unresolved or cyclic struct type");
        *out = nominal;
        return true;
    }
    if (sema_module_declared(s, name)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "type name resolves to a module value");
    if (type_image_named(s, name)) return sema_image_type(s, (TypeRef){name.offset, NO_ID}, IR_VOID, out);
    if (source_span_is(s, name, "bool")) *out = IR_BOOL;
    else if (source_span_is(s, name, "i32")) *out = IR_I32;
    else if (source_span_is(s, name, "u32")) *out = IR_U32;
    else if (source_span_is(s, name, "f32")) *out = IR_F32;
    else if (source_span_is(s, name, "f16")) *out = IR_F16;
    else if (name.length == 5u && memcmp(s->source+name.offset, "vec", 3u) == 0 && s->source[name.offset+3u] >= '2' && s->source[name.offset+3u] <= '4') {
        const char suffix = s->source[name.offset+4u];
        if (suffix != 'u' && suffix != 'i' && suffix != 'f' && suffix != 'h') return source_error(s, DOE_COMPILER_UNSUPPORTED, name.offset, "unsupported vector type alias");
        *width = (uint32_t)(s->source[name.offset+3u]-'0');
        *out = suffix == 'u'?IR_U32:suffix == 'i'?IR_I32:suffix == 'h'?IR_F16:IR_F32;
    }
    else {
        uint32_t columns, rows;
        if (!type_matrix_name(s, name, &columns, &rows) || name.length != 7u)
            return source_error_cause(s, DOE_COMPILER_UNSUPPORTED, SOURCE_CAUSE_UNKNOWN_TYPE, name.offset, "unsupported type name");
        if (s->source[name.offset + 6u] != 'f' && s->source[name.offset + 6u] != 'h')
            return source_error(s, DOE_COMPILER_UNSUPPORTED, name.offset, "unsupported matrix element type");
        if (s->source[name.offset + 6u] == 'h' && (s->compiler->module_flags & MODULE_F16) == 0u)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "f16 requires enable f16");
        return type_matrix_intern(s, s->source[name.offset + 6u] == 'h' ? IR_F16 : IR_F32, columns, rows, out);
    }
    if (*out == IR_F16 && (s->compiler->module_flags & MODULE_F16) == 0u)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, name.offset, "f16 requires enable f16");
    return true;
}
bool integer_attribute(Source *s, uint32_t expression, const LocalScope *scope, bool count, uint32_t *out) {
    ExpressionValue v;
    if (!sema_expression(s, expression, scope, true, false, &v)) return false;
    if (count && v.constant.type == IR_ABSTRACT_INT && !sema_materialize(s, &v, IR_I32)) return false;
    if (v.runtime || v.overridable || !v.known || (v.constant.type != IR_U32 && v.constant.type != IR_I32 && v.constant.type != IR_ABSTRACT_INT) || (v.constant.type == IR_I32 && (v.constant.bits&UINT64_C(0x80000000)) != 0u) || v.constant.bits>UINT32_MAX)
        return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_INVALID_ATTRIBUTE, v.value.source_offset, "attribute or array count requires a nonnegative 32-bit integer constant");
    *out = (uint32_t)v.constant.bits;
    return true;
}
bool sema_type_reference(Source *s, TypeRef ref, const LocalScope *scope, TypeId *out) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t base = c->walk_count;
    TypeId type = IR_VOID;
    while (ref.syntax != NO_ID) {
        if (ref.syntax >= c->generic_count) return source_error(s, DOE_COMPILER_INPUT_ERROR, ref.offset, "invalid type syntax identity");
        const GenericSyntax *g = &c->generic_syntax[ref.syntax];
        if (lexical_value(s, scope, g->name) || sema_module_declared(s, g->name)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, g->name.offset, "type generator is shadowed");
        if (g->name.length >= 16u && memcmp(s->source + g->name.offset, "texture_storage_", 16u) == 0) {
            if (!sema_image_type(s, ref, IR_VOID, &type)) return false;
            break;
        }
        if (c->walk_count == c->limits.traversal) return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count+1u, ref.offset, "type resolution traversal capacity exceeded");
        c->walk[c->walk_count++] = (WalkItem){.object = ref.syntax};
        ref = g->element;
    }
    uint32_t width = 0u;
    if (type == IR_VOID && !base_type(s, source_name_at(s, ref.offset), scope, &type, &width)) return false;
    if (width != 0u && !type_intern(s, TYPE_VECTOR, type, width, &type)) return false;
    while (c->walk_count>base) {
        const uint32_t index = c->walk[c->walk_count-1u].object;
        const GenericSyntax *g = &c->generic_syntax[index];
        if (type_image_named(s, g->name)) {
            if (!sema_image_type(s, (TypeRef){g->name.offset, index}, type, &type)) return false;
        } else if (source_span_is(s, g->name, "ptr")) {
            if (!type_intern(s, TYPE_POINTER, type, g->flags, &type)) return false;
        } else if (source_span_is(s, g->name, "atomic")) {
            if (g->count_expression != NO_ID || (type != IR_I32 && type != IR_U32))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, g->name.offset, "atomic requires one i32 or u32 element type");
            if (!type_intern(s, TYPE_ATOMIC, type, 1u, &type)) return false;
        } else if (source_span_is(s, g->name, "array")) {
            uint32_t count = 0u;
            bool deferred = false;
            if (g->count_expression != NO_ID && s->specialization != NULL &&
                !override_integer(s, g->count_expression, scope, &count, &deferred)) return false;
            if (deferred) {
                if (!override_array_type(s, type, index, &type)) return false;
                --c->walk_count;
                continue;
            }
            if (g->count_expression != NO_ID && ((s->specialization == NULL &&
                !integer_attribute(s, g->count_expression, scope, true, &count)) || count == 0u)) {
                if (s->diagnostic->status == DOE_COMPILER_OK) (void)source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_INVALID_TYPE, g->name.offset, "array count must be positive");
                return false;
            }
            if (!type_intern(s, g->count_expression == NO_ID?TYPE_RUNTIME_ARRAY:TYPE_ARRAY, type, count, &type)) return false;
        }
        else {
            const char *name = s->source+g->name.offset;
            if (g->name.length == 6u && memcmp(name, "mat", 3u) == 0 &&
                name[3] >= '2' && name[3] <= '4' && name[4] == 'x' &&
                name[5] >= '2' && name[5] <= '4') {
                if (g->count_expression != NO_ID || (type != IR_F32 && type != IR_F16))
                    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, g->name.offset,
                                        "matrix annotation requires one floating-point scalar type");
                if (!type_matrix_intern(s, type, (uint32_t)(name[3] - '0'),
                                         (uint32_t)(name[5] - '0'), &type)) return false;
                --c->walk_count;
                continue;
            }
            if (g->name.length != 4u || memcmp(name, "vec", 3u) != 0 || name[3]<'2' || name[3]>'4')
                return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_UNKNOWN_TYPE, g->name.offset,
                                    "unknown type generator");
            if (g->count_expression != NO_ID || type<IR_BOOL || (type>IR_F32 && type != IR_F16))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, g->name.offset, "vector annotation requires one concrete scalar type");
            if (!type_intern(s, TYPE_VECTOR, type, (uint32_t)(name[3]-'0'), &type)) return false;
        }
        --c->walk_count;
    }
    *out = type;
    return true;
}
static bool scalar_common(Source *s, TypeId a, TypeId b, uint32_t offset, TypeId *out) {
    if (a == b) {
        *out = a;
        return true;
    }
    if (a == IR_ABSTRACT_INT && b != IR_BOOL && b != IR_VOID) {
        *out = b;
        return true;
    }
    if (b == IR_ABSTRACT_INT && a != IR_BOOL && a != IR_VOID) {
        *out = a;
        return true;
    }
    if ((a == IR_ABSTRACT_FLOAT && (b == IR_F32 || b == IR_F16)) || (b == IR_ABSTRACT_FLOAT && (a == IR_F32 || a == IR_F16))) {
        *out = a == IR_ABSTRACT_FLOAT ? b : a;
        return true;
    }
    return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_TYPE_MISMATCH, offset, "types have no common implicit materialization");
}
bool sema_common_type(Source *s, TypeId a, TypeId b, uint32_t offset, TypeId *out) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t base = c->walk_count;
    while (a != b && (a >= IR_PRIMITIVE_COUNT || b >= IR_PRIMITIVE_COUNT)) {
        if (a<IR_PRIMITIVE_COUNT || b<IR_PRIMITIVE_COUNT) return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_TYPE_MISMATCH, offset, "aggregate shapes differ");
        const TypeRecord *left = &c->types[a-IR_PRIMITIVE_COUNT], *right = &c->types[b-IR_PRIMITIVE_COUNT];
        if (left->kind == TYPE_STRUCT || left->kind != right->kind || left->count != right->count)
            return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_TYPE_MISMATCH, offset, "aggregate shapes or nominal identities differ");
        if (c->walk_count == c->limits.traversal) return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count+1u, offset, "common type traversal capacity exceeded");
        c->walk[c->walk_count++] = (WalkItem){.kind = left->kind, .cursor = left->count};
        a = left->element;
        b = right->element;
    }
    TypeId type;
    if (a == b) type = a;
    else if (!scalar_common(s, a, b, offset, &type)) return false;
    while (c->walk_count>base) {
        const WalkItem w = c->walk[--c->walk_count];
        if (!type_intern(s, w.kind, type, w.cursor, &type)) return false;
    }
    *out = type;
    return true;
}
bool sema_default_type(Source *s, TypeId input, uint32_t offset, TypeId *out) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t base = c->walk_count;
    TypeId type = input;
    if (!type_abstract(c, type)) {
        *out = type;
        return true;
    }
    while (type >= IR_PRIMITIVE_COUNT) {
        const TypeRecord *t = &c->types[type-IR_PRIMITIVE_COUNT];
        if (c->walk_count == c->limits.traversal) return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count+1u, offset, "default type traversal capacity exceeded");
        c->walk[c->walk_count++] = (WalkItem){.kind = t->kind, .cursor = t->count};
        type = t->element;
    }
    type = type == IR_ABSTRACT_FLOAT?IR_F32:IR_I32;
    while (c->walk_count>base) {
        const WalkItem w = c->walk[--c->walk_count];
        if (!type_intern(s, w.kind, type, w.cursor, &type)) return false;
    }
    *out = type;
    return true;
}
/* Dependency edges come only from parsed type references and expression nodes.
 * Parent/cursor state is embedded in each declaration, so a long mixed chain
 * never consumes C stack or an unlisted worklist. */
static bool type_dependency(const Source *s, TypeRef ref, uint32_t *kind, uint32_t *id) {
    const CompilerWorkspace *c = s->compiler;
    while (ref.syntax != NO_ID) ref = c->generic_syntax[ref.syntax].element;
    if (ref.offset == NO_ID) return false;
    const NameSpan name = source_name_at(s, ref.offset);
    *id = sema_find_type_alias(s, name);
    if (*id != NO_ID) { *kind = 0u; return true; }
    const TypeId nominal = sema_find_struct(s, name);
    if (nominal == NO_ID) return false;
    *kind = 1u;
    *id = nominal - IR_PRIMITIVE_COUNT;
    return true;
}
static bool expression_dependency(const Source *s, const Expression *e, uint32_t *kind, uint32_t *id) {
    if (e->kind == EXPRESSION_NAME) {
        *id = sema_find_constant(s, e->value.name);
        *kind = 0u;
        return *id != NO_ID;
    }
    if (e->kind == EXPRESSION_CALL) {
        *id = sema_find_type_alias(s, e->value.call.callee);
        if (*id != NO_ID) { *kind = 0u; return true; }
        TypeId type = sema_find_struct(s, e->value.call.callee);
        if (type == NO_ID && e->value.call.type.offset != NO_ID)
            return type_dependency(s, e->value.call.type, kind, id);
        if (type != NO_ID) {
            *kind = 1u;
            *id = type-IR_PRIMITIVE_COUNT;
            return true;
        }
    }
    if ((e->kind == EXPRESSION_MEMBER || e->kind == EXPRESSION_LOAD) && e->left == NO_ID) {
        const NameSpan root = e->kind == EXPRESSION_MEMBER?e->value.member.object:e->value.reference.storage;
        *id = sema_find_constant(s, root);
        *kind = 0u;
        return *id != NO_ID;
    }
    return false;
}
static uint32_t declaration_state(const CompilerWorkspace *c, uint32_t kind, uint32_t id) {
    if (kind == 0u) return c->constant_decls[id].state&3u;
    return (c->types[id].flags&TYPE_RESOLVED) != 0u?2u:(c->types[id].flags&TYPE_RESOLVING) != 0u?1u:0u;
}
static void enter_declaration(CompilerWorkspace *c, uint32_t kind, uint32_t id, uint32_t parent_kind, uint32_t parent) {
    if (kind == 0u) {
        ConstantSyntax *d = &c->constant_decls[id];
        d->state = (d->state & DECLARATION_OVERRIDE)|1u|(parent_kind<<2u);
        d->value.dependency.scan = NO_ID;
        d->value.dependency.parent = parent;
    }
    else {
        TypeRecord *t = &c->types[id];
        t->flags|=TYPE_RESOLVING;
        t->state.dependency.scan = t->state.dependency.first_expression;
        t->state.dependency.member = 0u;
        t->state.dependency.parent = parent;
        t->state.dependency.parent_kind = parent_kind;
    }
}
static bool next_dependency(Source *s, uint32_t kind, uint32_t id, uint32_t *child_kind, uint32_t *child) {
    CompilerWorkspace *c = s->compiler;
    uint32_t *scan, end;
    if (kind == 0u) {
        ConstantSyntax *d = &c->constant_decls[id];
        scan = &d->value.dependency.scan;
        end = d->expression == NO_ID || d->expression == OVERRIDE_NO_INITIALIZER ? d->type : d->expression + 1u;
        if (*scan == NO_ID) {
            *scan = d->first_expression;
            if (type_dependency(s, d->annotation, child_kind, child)) return true;
        }
    }
    else {
        TypeRecord *t = &c->types[id];
        scan = &t->state.dependency.scan;
        end = t->state.dependency.end_expression;
        while (t->state.dependency.member<t->count) {
            const MemberRecord *m = &c->members[t->element+t->state.dependency.member++];
            if (type_dependency(s, m->annotation, child_kind, child)) return true;
        }
    }
    while (*scan<end) if (expression_dependency(s, &c->expressions[(*scan)++], child_kind, child)) return true;
    return false;
}
static bool finish_declaration(Source *s, uint32_t kind, uint32_t id, LocalScope *scope) {
    CompilerWorkspace *c = s->compiler;
    if (kind == 0u) {
        ConstantSyntax *d = &c->constant_decls[id];
        if ((d->state & DECLARATION_OVERRIDE) != 0u) return override_finish(s, id, scope);
        if (d->expression == NO_ID) {
            TypeId type;
            if (!sema_type_reference(s, d->annotation, scope, &type)) return false;
            d->type = type;
            d->state = 2u;
            return true;
        }
        ExpressionValue value;
        if (!sema_expression(s, d->expression, scope, true, false, &value)) return false;
        if (value.runtime || value.overridable || !value.known) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, d->name.offset, "module const initializer requires a const expression");
        if (d->annotation.offset != NO_ID) {
            TypeId type;
            if (!sema_type_reference(s, d->annotation, scope, &type) || !sema_materialize(s, &value, type)) return false;
        }
        d->type = value.constant.type;
        d->value.bits = value.constant.bits;
        d->state = 2u|((value.constant.flags&CONSTANT_AGGREGATE) != 0u?16u:0u);
    }
    else {
        TypeRecord *t = &c->types[id];
        for (uint32_t i = 0u;i<t->count;++i) {
            MemberRecord *m = &c->members[t->element+i];
            if (!sema_type_reference(s, m->annotation, scope, &m->type)) return false;
            if ((m->flags&1u) != 0u && !integer_attribute(s, m->align_expression, scope, false, &m->align_expression)) return false;
            if ((m->flags&2u) != 0u && !integer_attribute(s, m->size_expression, scope, false, &m->size_expression)) return false;
        }
        if (!type_layout_struct(s, id+IR_PRIMITIVE_COUNT)) return false;
    }
    return true;
}
static bool resolve_graph(Source *s, LocalScope *scope) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t nominal_count = c->type_count;
    for (uint32_t root_kind = 0u;root_kind<2u;++root_kind) {
        const uint32_t roots = root_kind == 0u?c->constant_decl_count:nominal_count;
        for (uint32_t root = 0u;root<roots;++root) {
            if (declaration_state(c, root_kind, root) == 2u) continue;
            uint32_t kind = root_kind, id = root;
            enter_declaration(c, kind, id, 0u, NO_ID);
            while (id != NO_ID) {
                uint32_t child_kind, child;
                if (next_dependency(s, kind, id, &child_kind, &child)) {
                    const uint32_t state = declaration_state(c, child_kind, child);
                    if (state == 1u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, kind == 0u?c->constant_decls[id].name.offset:c->types[id].name.offset, "module type/constant dependency cycle");
                    if (state == 0u) {
                        enter_declaration(c, child_kind, child, kind, id);
                        kind = child_kind;
                        id = child;
                    }
                    continue;
                }
                const uint32_t parent = kind == 0u?c->constant_decls[id].value.dependency.parent:c->types[id].state.dependency.parent;
                const uint32_t parent_kind = kind == 0u?(c->constant_decls[id].state>>2u)&1u:c->types[id].state.dependency.parent_kind;
                if (!finish_declaration(s, kind, id, scope)) return false;
                id = parent;
                kind = parent_kind;
            }
        }
    }
    return true;
}
static NameSpan module_name(const CompilerWorkspace *c, uint32_t index, uint32_t nominal_count) {
    if (index<c->constant_decl_count) return c->constant_decls[index].name;
    index-=c->constant_decl_count;
    if (index<c->function_count) return c->functions[index].name;
    index-=c->function_count;
    if (index<c->resource_count) return c->resources[index].name;
    index-=c->resource_count;
    if (index<c->workgroup_count) return c->workgroups[index].name;
    index-=c->workgroup_count;
    return index<nominal_count?c->types[index].name:(NameSpan){0};
}
static bool declarations_valid(Source *s) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t nominal_count = c->type_count;
    const uint64_t count = (uint64_t)c->constant_decl_count+c->function_count+c->resource_count+c->workgroup_count+nominal_count;
    for (uint32_t i = 0u;i<count;++i) {
        const NameSpan name = module_name(c, i, nominal_count);
        if (!sema_valid_name(s, name)) return false;
        for (uint32_t j = 0u;j<i;++j) if (source_spans_equal(s, name, module_name(c, j, nominal_count)))
                return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_DUPLICATE_SYMBOL, name.offset, "duplicate module declaration");
    }
    for (uint32_t t = 0u;t<nominal_count;++t) for (uint32_t i = 0u;i<c->types[t].count;++i) {
            const NameSpan name = c->members[c->types[t].element+i].name;
            if (!sema_valid_name(s, name)) return false;
            for (uint32_t j = 0u;j<i;++j) if (source_spans_equal(s, name, c->members[c->types[t].element+j].name))
                    return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_DUPLICATE_SYMBOL, name.offset, "duplicate struct member name");
        }
    return true;
}
static int formal_order(const Source *s, uint32_t a, uint32_t b) {
    const NameSpan x = s->compiler->formals[a].name, y = s->compiler->formals[b].name;
    const size_t n = x.length<y.length?x.length:y.length;
    const int order = memcmp(s->source+x.offset, s->source+y.offset, n);
    return order != 0?order:x.length<y.length?-1:x.length>y.length?1:0;
}
static void sift_formals(const Source *s, uint32_t *indices, size_t root, size_t count) {
    while (root<count/2u) {
        size_t child = 2u*root+1u;
        if (child+1u<count && formal_order(s, indices[child], indices[child+1u])<0) ++child;
        if (formal_order(s, indices[root], indices[child]) >= 0) return;
        const uint32_t swap = indices[root];
        indices[root] = indices[child];
        indices[child] = swap;
        root = child;
    }
}
static bool resolve_signatures(Source *s, const LocalScope *scope) {
    CompilerWorkspace *c = s->compiler;
    for (uint32_t f = 0u;f<c->function_count;++f) {
        FunctionRecord *fn = &c->functions[f];
        TypeId result = IR_VOID;
        if (fn->annotation.offset != NO_ID && !sema_type_reference(s, fn->annotation, scope, &result)) return false;
        if ((result != IR_VOID && !type_constructible(c, result)) || ((fn->flags&FUNCTION_ENTRY) != 0u && !stage_graphics(fn) && result != IR_VOID) || ((fn->flags&FUNCTION_MUST_USE) != 0u && result == IR_VOID)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, fn->name.offset, "invalid function result declaration");
        fn->result_type = result;
        const StageEntry *stage = stage_graphics(fn) ? &s->stages->entries[fn->entry_index] : NULL;
        const uint32_t first = stage != NULL ? stage->formal_first : fn->formal_start;
        const uint32_t count = stage != NULL ? stage->formal_count : fn->formal_count;
        if (count>UINT16_MAX-3u) return source_error(s, DOE_COMPILER_UNSUPPORTED, fn->name.offset, "SPIR-V instruction word-count format capacity exceeded");
        for (uint32_t i = 0u;i<count;++i) {
            const uint32_t index = first+i;
            FormalRecord *p = &c->formals[index];
            TypeId type;
            if (!sema_valid_name(s, p->name) || !sema_type_reference(s, p->type.annotation, scope, &type)) return false;
            if (!type_constructible(c, type) && !type_handle(c, type) && !type_pointer(c, type)) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, p->name.offset, "formal type is not constructible or a handle");
            const uint32_t offset = p->type.annotation.offset;
            p->type.resolved.type = type;
            p->type.resolved.offset = offset;
            c->values[i] = index;
        }
        for (size_t i = count/2u;i != 0u;--i) sift_formals(s, c->values, i-1u, count);
        for (size_t i = count;i>1u;--i) {
            const uint32_t swap = c->values[0];
            c->values[0] = c->values[i-1u];
            c->values[i-1u] = swap;
            sift_formals(s, c->values, 0u, i-1u);
        }
        for (size_t i = 1u;i<count;++i) if (formal_order(s, c->values[i-1u], c->values[i]) == 0)
                return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_DUPLICATE_SYMBOL, c->formals[c->values[i]].name.offset, "duplicate formal parameter name");
    }
    return true;
}
bool sema_resolve_declarations(Source *s, LocalScope *scope) {
    CompilerWorkspace *c = s->compiler;
    c->current_function = NO_ID;
    if (!declarations_valid(s) || !resolve_graph(s, scope) || !resolve_signatures(s, scope) || !sema_stage_interfaces(s, scope)) return false;
    for (uint32_t e=0u;e<c->entry_count;++e) {
        EntryRecord *entry=&c->entries[e];
        if (stage_graphics(&c->functions[entry->function])) {
            memset(&entry->state, 0, sizeof(entry->state));
            continue;
        }
        if (s->specialization != NULL) {
            if (!override_workgroup_size(s, entry, scope)) return false;
            continue;
        }
        uint32_t dimensions[3],product=1u;
        for (uint32_t i=0u;i<3u;++i) {
            const Integer *v=&entry->state.dimensions[i];
            dimensions[i]=v->value;
            if (v->expression!=NO_ID && !integer_attribute(s,v->expression,scope,false,&dimensions[i])) return false;
            if (dimensions[i]==0u || dimensions[i]>UINT32_MAX/product)
                return source_error(s,DOE_COMPILER_UNSUPPORTED,v->offset,"workgroup dimensions must be positive and their product must fit u32");
            product*=dimensions[i];
        }
        memset(&entry->state,0,sizeof(entry->state));
        for (uint32_t i=0u;i<3u;++i) entry->state.resolved.workgroup_size[i]=dimensions[i];
    }
    for (uint32_t i = 0u;i<c->resource_count;++i) {
        ResourceRecord *r = &c->resources[i];
        if ((r->group_expression != NO_ID && !integer_attribute(s, r->group_expression, scope, false, &r->group)) || (r->binding_expression != NO_ID && !integer_attribute(s, r->binding_expression, scope, false, &r->binding)) || !sema_type_reference(s, r->annotation, scope, &r->type) || !type_resource_layout(s, r)) return false;
        const uint32_t maximum_binding = (c->module_flags & MODULE_CORE_BINDINGS) != 0u ? 999u : 31u;
        if (r->group > 3u || r->binding > maximum_binding)
            return source_error(s, DOE_COMPILER_UNSUPPORTED, r->source_offset,
                maximum_binding == 31u ? "storage requires group 0..3 and binding 0..31" :
                "resource requires group 0..3 and binding 0..999");
    }
    for (uint32_t i = 0u; i < c->workgroup_count; ++i) {
        WorkgroupRoot *root = &c->workgroups[i];
        const uint32_t address = root->type, initializer = root->target;
        const TypeRef annotation = root->state.annotation;
        root->type = IR_VOID;
        if (annotation.offset != NO_ID && !sema_type_reference(s, annotation, scope, &root->type)) return false;
        root->state.resolved.address_space = address;
        root->state.resolved.initializer = NO_ID;
        root->target = 0u;
        if (initializer != NO_ID) {
            ExpressionValue value;
            if (!sema_expression(s, initializer, scope, true, false, &value)) return false;
            if (value.runtime || value.overridable || !value.known)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, root->name.offset,
                                    "private variable initializer requires a const expression");
            if (root->type == IR_VOID && !sema_default_type(s, value.constant.type, root->name.offset, &root->type)) return false;
            if (!sema_materialize(s, &value, root->type)) return false;
            if (c->edge_count == c->limits.edges)
                return source_pool_error(s, DOE_COMPILER_POOL_CONSTANT_EDGES, (size_t)c->edge_count + 1u,
                                         root->name.offset, "module initializer edge capacity exceeded");
            root->state.resolved.initializer = c->edge_count;
            c->constant_edges[c->edge_count++] = value.constant;
        }
        const uint32_t flags = type_flags(c, root->type);
        if (address == MODULE_IMMEDIATE &&
            ((flags & (TYPE_HOST | TYPE_CONSTRUCTIBLE | TYPE_FIXED)) != (TYPE_HOST | TYPE_CONSTRUCTIBLE | TYPE_FIXED) ||
             (flags & (TYPE_ARRAY_CONTENT | TYPE_ATOMIC_CONTENT)) != 0u || type_size(c,root->type) > UINT32_MAX))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, root->name.offset,
                "immediate data requires a fixed host-shareable constructible type without arrays");
        if ((flags & TYPE_DEFERRED) != 0u && s->specialization != NULL) continue;
        if ((flags & TYPE_FIXED) == 0u || (flags & TYPE_ABSTRACT) != 0u ||
            (address == MODULE_PRIVATE ? (flags & TYPE_CONSTRUCTIBLE) == 0u :
             (flags & (TYPE_CONSTRUCTIBLE | TYPE_ATOMIC_CONTENT)) == 0u))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, root->name.offset,
                                "module variable requires a concrete fixed type valid for its address space");
    }
    for (uint32_t e=0u;e<c->entry_count;++e) {
    const EntryRecord *entry=&c->entries[e];
    c->current_function=entry->function;
    for (uint32_t i=entry->input_first;i<entry->input_first+entry->input_count;++i) {
        const EntryInput *p=&c->entry_inputs[i];
        if (!sema_valid_name(s, p->name)) return false;
        for (uint32_t j = entry->input_first;j<i;++j) if (source_spans_equal(s, p->name, c->entry_inputs[j].name) || p->kind == c->entry_inputs[j].kind)
                return source_error_cause(s, DOE_COMPILER_SEMANTIC_ERROR, SOURCE_CAUSE_DUPLICATE_SYMBOL, p->name.offset, "duplicate compute input name or builtin");
        for (uint32_t category = 0u;category<5u;++category) {
            const uint32_t count = category == 0u?c->constant_decl_count:category == 1u?c->function_count:category == 2u?c->resource_count:category == 3u?c->type_count:c->workgroup_count;
            for (uint32_t j = 0u;j<count;++j) {
                if (category == 3u && c->types[j].kind != TYPE_STRUCT) continue;
                const NameSpan name = category == 0u?c->constant_decls[j].name:category == 1u?c->functions[j].name:category == 2u?c->resources[j].name:category == 3u?c->types[j].name:c->workgroups[j].name;
                const bool scalar = input_scalar(p->kind) || p->flags == 0u;
                const bool vector = !input_scalar(p->kind);
                if ((scalar && source_span_is(s, name, "u32")) || (vector && source_span_is(s, name, p->flags != 0u?"vec3u":"vec3")))
                    return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, p->name.offset, "compute input type is shadowed");
            }
        }
        if (!input_scalar(p->kind)) {
            TypeId ignored;
            if (!type_intern(s, TYPE_VECTOR, IR_U32, 3u, &ignored)) return false;
        }
    }
    }
    c->current_function=c->entry_count != 0u ? c->entries[0].function : NO_ID;
    if (c->alias.length != 0u && (!sema_valid_name(s, c->alias) || sema_find_parameter(s, c->alias) != NO_ID || sema_find_parameter(s, c->alias_source) == NO_ID))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, c->alias.offset, "invalid leading invocation alias");
    c->current_function = NO_ID;
    return true;
}
