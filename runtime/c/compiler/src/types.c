/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>
NameSpan source_name_at(const Source *s, uint32_t offset) {
    uint32_t n = 0u;
    while ((size_t)offset+n<s->length) {
        const char c = s->source[(size_t)offset+n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) break;
        ++n;
    }
    return (NameSpan){offset, n};
}
bool source_spans_equal(const Source *s, NameSpan a, NameSpan b) {
    return a.length == b.length && memcmp(s->source+a.offset, s->source+b.offset, a.length) == 0;
}
bool source_span_is(const Source *s, NameSpan a, const char *text) {
    return a.length == strlen(text) && memcmp(s->source+a.offset, text, a.length) == 0;
}
TypeId type_leaf(const CompilerWorkspace *c, TypeId id) {
    if (id<7u) return id;
    const TypeRecord *t = &c->types[id-7u];
    return t->kind == TYPE_VECTOR ? t->element : IR_VOID;
}
uint32_t type_vector_width(const CompilerWorkspace *c, TypeId id) {
    return id >= 7u && c->types[id - 7u].kind == TYPE_VECTOR ? c->types[id - 7u].count : 1u;
}
uint32_t type_flags(const CompilerWorkspace *c, TypeId id) {
    if (id >= 7u) return c->types[id-7u].flags;
    if (id == IR_VOID) return TYPE_RESOLVED;
    if (id >= IR_ABSTRACT_INT) return TYPE_CONSTRUCTIBLE|TYPE_ABSTRACT|TYPE_RESOLVED;
    return TYPE_CONSTRUCTIBLE|TYPE_FIXED|TYPE_RESOLVED|(id == IR_BOOL?0u:TYPE_HOST|TYPE_UNIFORM);
}
bool type_abstract(const CompilerWorkspace *c, TypeId id) {
    return (type_flags(c, id)&TYPE_ABSTRACT) != 0u;
}
bool type_constructible(const CompilerWorkspace *c, TypeId id) {
    return (type_flags(c, id)&TYPE_CONSTRUCTIBLE) != 0u;
}
uint64_t type_size(const CompilerWorkspace *c, TypeId id) {
    return id<7u?4u:c->types[id-7u].state.layout.size;
}
uint64_t type_alignment(const CompilerWorkspace *c, TypeId id) {
    return id<7u?4u:c->types[id-7u].state.layout.alignment;
}
uint32_t type_children(const CompilerWorkspace *c, TypeId id) {
    return id<7u?0u:c->types[id-7u].count;
}
TypeId type_child(const CompilerWorkspace *c, TypeId id, uint32_t index) {
    const TypeRecord *t = &c->types[id-7u];
    return t->kind == TYPE_STRUCT?c->members[t->element+index].type:
        t->kind == TYPE_ATOMIC_RESULT && index == 1u ? IR_BOOL : t->element;
}
static bool round_up(Source *s, uint64_t value, uint64_t alignment, uint32_t offset, uint64_t *out) {
    const uint64_t remainder = value%alignment, padding = remainder == 0u?0u:alignment-remainder;
    if (value>UINT64_MAX-padding) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, offset, "type layout size overflows uint64");
    *out = value+padding;
    return true;
}
static uint64_t required_alignment(const CompilerWorkspace *c, TypeId id) {
    uint64_t a = type_alignment(c, id);
    if (id >= 7u && c->types[id-7u].kind != TYPE_VECTOR && c->types[id-7u].kind != TYPE_MATRIX && a<16u) a = 16u;
    return a;
}
static bool type_matrix_intern_valid(const CompilerWorkspace *c, TypeId column, uint32_t count) {
    if (column < 7u || count < 2u || count > 4u) return false;
    const TypeRecord *vector = &c->types[column - 7u];
    return vector->kind == TYPE_VECTOR && (vector->element == IR_F32 || vector->element == IR_ABSTRACT_FLOAT);
}
bool type_pointer(const CompilerWorkspace *c, TypeId type) {
    return type >= 7u && c->types[type - 7u].kind == TYPE_POINTER;
}
bool type_matrix(const CompilerWorkspace *c, TypeId type) {
    return type >= 7u && c->types[type - 7u].kind == TYPE_MATRIX;
}
bool type_matrix_name(const Source *s, NameSpan name, uint32_t *columns, uint32_t *rows) {
    if (name.length != 6u && name.length != 7u) return false;
    const char *text = s->source + name.offset;
    if (memcmp(text, "mat", 3u) != 0 || text[3] < '2' || text[3] > '4' ||
        text[4] != 'x' || text[5] < '2' || text[5] > '4') return false;
    *columns = (uint32_t)(text[3] - '0');
    *rows = (uint32_t)(text[5] - '0');
    return true;
}
bool type_matrix_intern(Source *s, TypeId scalar, uint32_t columns, uint32_t rows, TypeId *out) {
    TypeId column;
    return type_intern(s, TYPE_VECTOR, scalar, rows, &column) &&
        type_intern(s, TYPE_MATRIX, column, columns, out);
}
bool type_intern(Source *s, uint32_t kind, TypeId element, uint32_t count, TypeId *out) {
    CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u;i<c->type_count;++i) {
        const TypeRecord *t = &c->types[i];
        if (t->kind == kind && t->element == element && t->count == count && kind != TYPE_STRUCT) {
            *out = i+7u;
            return true;
        }
    }
    if (c->type_count == c->limits.types)
        return source_pool_error(s, DOE_COMPILER_POOL_TYPES, (size_t)c->type_count+1u, s->length, "type capacity exceeded");
    TypeRecord t = {.kind = kind, .element = element, .count = count, .flags = TYPE_RESOLVED, .target = NO_ID, .aux = NO_ID};
    const uint32_t flags = type_flags(c, element);
    if (!type_constructible(c, element) && (flags & TYPE_ATOMIC_CONTENT) == 0u)
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, s->length, "aggregate element must be constructible or contain atomics");
    if (kind == TYPE_POINTER) {
        if (!type_constructible(c, element) || type_abstract(c, element))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, s->length, "function pointer requires a concrete constructible store type");
        t.state.layout.alignment = 1u;
    }
    else if (kind == TYPE_ATOMIC || kind == TYPE_ATOMIC_RESULT) {
        if (element != IR_I32 && element != IR_U32)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, s->length, "atomic element requires i32 or u32");
        t.flags |= TYPE_FIXED | (kind == TYPE_ATOMIC ? TYPE_HOST | TYPE_ATOMIC_CONTENT : TYPE_CONSTRUCTIBLE);
        t.state.layout.size = kind == TYPE_ATOMIC ? 4u : 8u;
        t.state.layout.alignment = 4u;
        t.count = kind == TYPE_ATOMIC ? 1u : 2u;
    }
    else
    if (kind == TYPE_VECTOR) {
        if (element == IR_VOID || element >= 7u || count<2u || count>4u)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, s->length, "vector requires scalar element and width two through four");
        t.flags|=flags&(TYPE_CONSTRUCTIBLE|TYPE_HOST|TYPE_FIXED|TYPE_ABSTRACT|TYPE_UNIFORM);
        t.state.layout.size = 4u*count;
        t.state.layout.alignment = count == 2u?8u:16u;
    }
    else {
        t.flags|=flags&(TYPE_HOST|TYPE_ABSTRACT|TYPE_ATOMIC_CONTENT);
        t.state.layout.alignment = type_alignment(c, element);
        if (!round_up(s, type_size(c, element), t.state.layout.alignment, 0u, &t.state.layout.stride)) return false;
        if (kind == TYPE_ARRAY || kind == TYPE_MATRIX) {
            if (kind == TYPE_MATRIX && (!type_matrix_intern_valid(c, element, count)))
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, 0u, "matrix requires two through four floating-point columns and rows");
            if (count == 0u || t.state.layout.stride>UINT64_MAX/count)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, 0u, "array count or layout size is invalid");
            t.state.layout.size = t.state.layout.stride*count;
            t.flags|=flags&TYPE_CONSTRUCTIBLE;
            if ((flags&TYPE_FIXED) != 0u) t.flags|=TYPE_FIXED;
            if ((flags&TYPE_UNIFORM) != 0u && (kind == TYPE_MATRIX ||
                (t.state.layout.stride%16u == 0u && t.state.layout.stride%required_alignment(c, element) == 0u)))
                t.flags|=TYPE_UNIFORM;
        }
        else t.flags|=TYPE_TAIL;
    }
    *out = c->type_count+7u;
    c->types[c->type_count++] = t;
    return true;
}
bool type_layout_struct(Source *s, TypeId id) {
    CompilerWorkspace *c = s->compiler;
    TypeRecord *t = &c->types[id-7u];
    uint64_t end = 0u, alignment = 1u;
    uint32_t flags = TYPE_CONSTRUCTIBLE|TYPE_HOST|TYPE_FIXED|TYPE_UNIFORM|TYPE_RESOLVED;
    for (uint32_t i = 0u;i<t->count;++i) {
        MemberRecord *m = &c->members[t->element+i];
        const uint32_t child_flags = type_flags(c, m->type);
        if ((child_flags&TYPE_ABSTRACT) != 0u || m->type == IR_VOID || type_handle(c, m->type) || type_pointer(c, m->type))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, m->name.offset, "struct member type must be concrete");
        const bool tail = m->type >= 7u && c->types[m->type-7u].kind == TYPE_RUNTIME_ARRAY;
        if ((child_flags&TYPE_TAIL) != 0u && (!tail || i+1u != t->count))
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, m->name.offset, "runtime array must be the direct last struct member");
        m->alignment = type_alignment(c, m->type);
        m->size = type_size(c, m->type);
        if ((m->flags&1u) != 0u) {
            const uint64_t a = m->align_expression;
            if (a == 0u || (a&(a-1u)) != 0u || a<m->alignment)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, m->name.offset, "align must be a power of two at least natural alignment");
            m->alignment = a;
        }
        if ((m->flags&2u) != 0u) {
            if (tail || m->size_expression == 0u || m->size_expression<m->size)
                return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, m->name.offset, "size must cover the fixed member type");
            m->size = m->size_expression;
        }
        if (!round_up(s, end, m->alignment, m->name.offset, &m->offset)) return false;
        if (m->offset>UINT64_MAX-m->size) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, m->name.offset, "member end overflows uint64");
        end = m->offset+m->size;
        if (alignment<m->alignment) alignment = m->alignment;
        if ((child_flags&TYPE_HOST) == 0u) flags&=~(uint32_t)TYPE_HOST;
        if ((child_flags&TYPE_CONSTRUCTIBLE) == 0u) flags&=~(uint32_t)TYPE_CONSTRUCTIBLE;
        flags |= child_flags & TYPE_ATOMIC_CONTENT;
        if ((child_flags&TYPE_UNIFORM) == 0u || m->offset%required_alignment(c, m->type) != 0u) flags&=~(uint32_t)TYPE_UNIFORM;
        if (tail) {
            flags&=~(uint32_t)(TYPE_CONSTRUCTIBLE|TYPE_FIXED|TYPE_UNIFORM);
            flags|=TYPE_TAIL;
        }
        if (i != 0u) {
            const MemberRecord *previous = &c->members[t->element+i-1u];
            if (previous->type >= 7u && c->types[previous->type-7u].kind == TYPE_STRUCT) {
                uint64_t spacing = 0u;
                if (!round_up(s, type_size(c, previous->type), 16u, previous->name.offset, &spacing)) return false;
                if (m->offset-previous->offset<spacing) flags&=~(uint32_t)TYPE_UNIFORM;
            }
        }
    }
    uint64_t size = 0u;
    if (!round_up(s, end, alignment, t->name.offset, &size)) return false;
    t->flags = flags;
    t->state.layout.size = (flags&TYPE_TAIL) != 0u?0u:size;
    t->state.layout.alignment = alignment;
    t->state.layout.stride = 0u;
    return true;
}
bool type_resource_layout(Source *s, ResourceRecord *r) {
    CompilerWorkspace *c = s->compiler;
    const uint32_t flags = type_flags(c, r->type);
    if ((flags & TYPE_ATOMIC_CONTENT) != 0u &&
        (r->kind != DOE_COMPILER_STORAGE_BUFFER || r->access != DOE_COMPILER_READ_WRITE))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, r->name.offset,
            "atomic resource types require storage read_write access");
    if (type_handle(c, r->type)) {
        if (r->kind != 0u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, r->name.offset, "handle requires implicit handle address space");
        const TypeRecord *t = &c->types[r->type - 7u];
        r->kind = t->kind == TYPE_SAMPLER ? DOE_COMPILER_SAMPLER :
            t->state.image.format != 0u ? DOE_COMPILER_STORAGE_TEXTURE : DOE_COMPILER_SAMPLED_TEXTURE;
        r->access = t->kind == TYPE_SAMPLER ? DOE_COMPILER_READ : t->state.image.access;
        r->tail_type = NO_ID;
        return true;
    }
    if (r->kind == 0u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, r->name.offset, "resource requires an explicit buffer address space or a handle type");
    if ((flags&TYPE_HOST) == 0u) return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, r->name.offset, "resource type is not host-shareable");
    const bool standard = (c->module_flags & MODULE_STANDARD_UNIFORM) != 0u;
    if (r->kind == DOE_COMPILER_UNIFORM_BUFFER && ((!standard && (flags&TYPE_UNIFORM) == 0u) || (flags&TYPE_TAIL) != 0u))
        return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, r->name.offset, "uniform type violates conservative alignment or spacing");
    r->alignment = type_alignment(c, r->type);
    r->required_alignment = r->kind == DOE_COMPILER_UNIFORM_BUFFER && !standard ? required_alignment(c, r->type) : r->alignment;
    r->minimum = type_size(c, r->type);
    r->tail_type = NO_ID;
    if ((flags&TYPE_TAIL) != 0u) {
        const TypeRecord *t = &c->types[r->type-7u];
        r->flags = DOE_COMPILER_RESOURCE_RUNTIME_TAIL;
        if (t->kind == TYPE_RUNTIME_ARRAY) {
            r->tail_type = r->type;
            r->tail_offset = 0u;
        }
        else {
            const MemberRecord *m = &c->members[t->element+t->count-1u];
            r->tail_type = m->type;
            r->tail_offset = m->offset;
        }
        r->tail_stride = c->types[r->tail_type-7u].state.layout.stride;
        if (r->tail_offset>UINT64_MAX-r->tail_stride)
            return source_error(s, DOE_COMPILER_SEMANTIC_ERROR, r->name.offset, "runtime tail minimum binding size overflows uint64");
        if (!round_up(s, r->tail_offset+r->tail_stride, r->alignment, r->name.offset, &r->minimum)) return false;
    }
    return true;
}
