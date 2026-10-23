/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "stage.h"
#include <string.h>
static uint32_t declared_structs(const CompilerWorkspace *c) {
    uint32_t n = 0u;
    for (uint32_t i = 0u;i<c->type_count;++i) if (c->types[i].kind == TYPE_STRUCT) ++n;
    return n;
}
static bool discover(Source *s, TypeId root, size_t *count) {
    CompilerWorkspace *c = s->compiler;
    TypeId current = root;
    for (;;) {
        bool seen = c->reflection_map[current] != NO_ID;
        if (!seen) c->reflection_map[current] = (uint32_t)(*count)++;
        if (current >= 7u) {
            TypeRecord *t = &c->types[current-7u];
            if (t->kind == TYPE_STRUCT) {
                seen = t->aux != 0u;
                t->aux = 1u;
            }
            const uint32_t children = t->kind == TYPE_STRUCT?t->count:1u;
            if (!seen && children != 0u) {
                if (c->walk_count == c->limits.traversal) return source_pool_error(s, DOE_COMPILER_POOL_TRAVERSAL, (size_t)c->walk_count+1u, t->name.offset, "reflection traversal capacity exceeded");
                c->walk[c->walk_count++] = (WalkItem){.object = current, .cursor = 1u, .parent = children};
                current = type_child(c, current, 0u);
                continue;
            }
        }
        while (c->walk_count != 0u && c->walk[c->walk_count-1u].cursor == c->walk[c->walk_count-1u].parent) --c->walk_count;
        if (c->walk_count == 0u) return true;
        WalkItem *w = &c->walk[c->walk_count-1u];
        current = type_child(c, w->object, w->cursor++);
    }
}
bool reflect_prepare(Source *s, CompilerWorkspace *c, DoeCompilerTypedOutputCounts *counts) {
    *counts = (DoeCompilerTypedOutputCounts){.words = c->word_count, .resources = c->resource_count};
    for (uint32_t i = 0u;i<c->type_count+7u;++i) c->reflection_map[i] = NO_ID;
    uint64_t names=0u;
    const bool module=(c->limits.flags&LIMIT_MODULE)!=0u;
    const uint32_t entries=module?c->entry_count:1u;
    for (uint32_t i=0u;i<entries;++i) names+=(uint64_t)c->functions[c->entries[i].function].name.length+1u;
    for (uint32_t i = 0u;i<c->type_count;++i) if (c->types[i].kind == TYPE_STRUCT) {
            TypeRecord *t = &c->types[i];
            t->aux = 0u;
            c->reflection_map[i+7u] = (uint32_t)counts->types++;
            counts->members+=t->count;
            names+=(uint64_t)t->name.length+1u;
            for (uint32_t j = 0u;j<t->count;++j) names+=(uint64_t)c->members[t->element+j].name.length+1u;
        }
    for (uint32_t i = 0u;i<c->type_count;++i) if (c->types[i].kind == TYPE_STRUCT && !discover(s, i+7u, &counts->types)) return false;
    for (uint32_t i = 0u;i<c->resource_count;++i) {
        names+=(uint64_t)c->resources[i].name.length+1u;
        if (!type_handle(c, c->resources[i].type) && !discover(s, c->resources[i].type, &counts->types)) return false;
    }
    if (module) for (uint32_t i=0u;i<c->workgroup_count;++i) {
        names+=(uint64_t)c->workgroups[i].name.length+1u;
        if (!discover(s,c->workgroups[i].type,&counts->types)) return false;
    }
    if (names>UINT32_MAX) return source_error(s, DOE_COMPILER_UNSUPPORTED, s->length, "reflection names exceed uint32 index format");
    counts->name_bytes = (size_t)names;
    return true;
}
bool reflect_matrix_admit(Source *s) {
    const CompilerWorkspace *c = s->compiler;
    for (uint32_t i = 0u; i < c->type_count; ++i)
        if (c->types[i].kind == TYPE_MATRIX && c->reflection_map[i + 7u] != NO_ID)
            return source_error(s, DOE_COMPILER_UNSUPPORTED, c->types[i].name.offset,
                "matrix reflection requires explicit ABI8");
    return true;
}
static DoeCompilerNameV4 publish_name(const Source *s, NameSpan name, char *names, uint32_t *cursor) {
    if (name.length == 0u) return (DoeCompilerNameV4){0u, 0u};
    const DoeCompilerNameV4 result = {*cursor, name.length};
    memcpy(names+*cursor, s->source+name.offset, name.length);
    names[*cursor+name.length] = '\0';
    *cursor+=name.length+1u;
    return result;
}
static void publish_tables(const Source *s, const CompilerWorkspace *c, const DoeCompilerTypedOutputs *o, uint32_t *name_cursor) {
    uint32_t cursor=*name_cursor, member=0u;
    for (uint32_t id = 1u;id<c->type_count+7u;++id) {
        const uint32_t index = c->reflection_map[id];
        if (index == NO_ID) continue;
        DoeCompilerTypeV4 *out = &o->types[index];
        memset(out, 0, sizeof(*out));
        out->element_type = DOE_COMPILER_TYPED_NONE;
        out->flags = type_flags(c, id)&(TYPE_CONSTRUCTIBLE|TYPE_HOST|TYPE_FIXED|TYPE_TAIL);
        out->size_bytes = (out->flags&TYPE_FIXED) != 0u?type_size(c, id):0u;
        out->alignment_bytes = type_alignment(c, id);
        if (id<7u) {
            out->kind = DOE_COMPILER_TYPE_SCALAR;
            out->scalar_kind = id;
            continue;
        }
        const TypeRecord *t = &c->types[id-7u];
        if (t->kind == TYPE_STRUCT) {
            out->kind = DOE_COMPILER_TYPE_STRUCT;
            out->name = publish_name(s, t->name, o->names, &cursor);
            out->first_member = member;
            out->member_count = t->count;
            member+=t->count;
        }
        else {
            out->element_type = c->reflection_map[t->element];
            if (t->kind == TYPE_VECTOR) {
                out->kind = DOE_COMPILER_TYPE_VECTOR;
                out->vector_width = t->count;
            }
            else if (t->kind == TYPE_MATRIX) {
                out->kind = DOE_COMPILER_TYPE_MATRIX;
                out->array_count = t->count;
                out->vector_width = type_vector_width(c, t->element);
                out->stride_bytes = t->state.layout.stride;
            }
            else {
                out->kind = t->kind == TYPE_ARRAY?DOE_COMPILER_TYPE_ARRAY:DOE_COMPILER_TYPE_RUNTIME_ARRAY;
                out->array_count = t->kind == TYPE_ARRAY?t->count:0u;
                out->stride_bytes = t->state.layout.stride;
            }
        }
    }
    member = 0u;
    for (uint32_t i = 0u;i<c->type_count;++i) if (c->types[i].kind == TYPE_STRUCT) {
            const TypeRecord *t = &c->types[i];
            for (uint32_t j = 0u;j<t->count;++j) {
                const MemberRecord *m = &c->members[t->element+j];
                DoeCompilerMemberV4 *out = &o->members[member++];
                memset(out, 0, sizeof(*out));
                out->name = publish_name(s, m->name, o->names, &cursor);
                out->type_index = c->reflection_map[m->type];
                out->flags = m->flags;
                out->offset_bytes = m->offset;
                out->size_bytes = m->size;
                out->alignment_bytes = m->alignment;
                out->explicit_align_bytes = (m->flags&DOE_COMPILER_MEMBER_EXPLICIT_ALIGN) != 0u?m->align_expression:0u;
                out->explicit_size_bytes = (m->flags&DOE_COMPILER_MEMBER_EXPLICIT_SIZE) != 0u?m->size_expression:0u;
            }
        }
    for (uint32_t i = 0u;i<c->resource_count;++i) {
        const ResourceRecord *r = &c->resources[i];
        DoeCompilerResourceV4 *out = &o->resources[i];
        memset(out, 0, sizeof(*out));
        out->name = publish_name(s, r->name, o->names, &cursor);
        out->group = r->group;
        out->binding = r->binding;
        out->kind = r->kind;
        out->access = r->access;
        out->type_index = type_handle(c, r->type) ? DOE_COMPILER_TYPED_NONE : c->reflection_map[r->type];
        out->flags = r->flags;
        out->minimum_binding_size_bytes = r->minimum;
        out->type_alignment_bytes = r->alignment;
        out->address_space_alignment_bytes = r->required_alignment;
        out->runtime_array_type = r->tail_type == NO_ID?DOE_COMPILER_TYPED_NONE:c->reflection_map[r->tail_type];
        out->runtime_array_offset_bytes = r->tail_offset;
        out->runtime_array_stride_bytes = r->tail_stride;
    }
    *name_cursor=cursor;
}

void reflect_typed(const Source *s, const CompilerWorkspace *c, const DoeCompilerTypedOutputs *o, DoeCompilerModuleV4 *module) {
    uint32_t cursor = 0u;
    memset(module, 0, sizeof(*module));
    module->schema_version = DOE_COMPILER_TYPED_SCHEMA_VERSION;
    module->layout_profile = DOE_COMPILER_TYPED_PROFILE_CONSERVATIVE_UNIFORM;
    module->entry_point = publish_name(s, c->functions[c->entries[0].function].name, o->names, &cursor);
    module->declared_struct_count = declared_structs(c);
    for (uint32_t i = 0u;i<3u;++i) module->workgroup_size[i] = c->entries[0].state.resolved.workgroup_size[i];
    publish_tables(s,c,o,&cursor);
}
void reflect_module(const Source *s, const CompilerWorkspace *c, const DoeCompilerModuleOutputs *o,
                    uint32_t selected, DoeCompilerModuleV5 *module) {
    uint32_t cursor=0u;
    memset(module,0,sizeof(*module));
    module->schema_version=DOE_COMPILER_MODULE_SCHEMA_VERSION;
    module->layout_profile=DOE_COMPILER_TYPED_PROFILE_CONSERVATIVE_UNIFORM;
    module->declared_struct_count=declared_structs(c);
    module->selected_entry=selected;
    for (uint32_t i=0u;i<c->entry_count;++i) {
        const EntryRecord *entry=&c->entries[i];
        DoeCompilerEntryV5 *out=&o->entries[i];
        memset(out,0,sizeof(*out));
        out->stage=stage_kind(&c->functions[entry->function]);
        out->name=publish_name(s,c->functions[entry->function].name,o->names,&cursor);
        for (uint32_t k=0u;k<3u;++k) out->workgroup_size[k]=entry->state.resolved.workgroup_size[k];
        for (uint32_t k=entry->input_first;k<entry->input_first+entry->input_count;++k)
            out->declared_builtin_mask|=1u<<c->entry_inputs[k].kind;
        out->static_builtin_mask=entry->source_input_mask;
        out->first_resource_reference=entry->state.resolved.resource_first;
        out->resource_reference_count=entry->state.resolved.resource_count;
        out->first_workgroup_reference=entry->state.resolved.workgroup_first;
        out->workgroup_reference_count=entry->state.resolved.workgroup_count;
        out->workgroup_storage_bytes=entry->state.resolved.workgroup_storage_bytes;
        if (entry->state.resolved.workgroup_count!=0u) out->flags=DOE_COMPILER_ENTRY_WORKGROUP_INITIALIZATION;
    }
    const DoeCompilerTypedOutputs tables={.types=o->types,.members=o->members,.resources=o->resources,.names=o->names};
    publish_tables(s,c,&tables,&cursor);
    for (uint32_t i=0u;i<c->workgroup_count;++i) {
        const WorkgroupRoot *root=&c->workgroups[i];
        DoeCompilerWorkgroupV5 *out=&o->workgroups[i];
        memset(out,0,sizeof(*out));
        out->name=publish_name(s,root->name,o->names,&cursor);
        out->type_index=c->reflection_map[root->type];
        out->size_bytes=type_size(c,root->type);
        out->alignment_bytes=type_alignment(c,root->type);
    }
    if (c->entry_reference_count!=0u)
        memcpy(o->entry_references,c->entry_references,(size_t)c->entry_reference_count*sizeof(*o->entry_references));
}

static bool flat_struct(const CompilerWorkspace *c, const TypeRecord *t) {
    if (t->count>DOE_COMPILER_MAX_STRUCT_MEMBERS || (t->flags&TYPE_FIXED) == 0u || t->state.layout.size>UINT32_MAX || t->state.layout.alignment>UINT32_MAX) return false;
    for (uint32_t i = 0u;i<t->count;++i) {
        const MemberRecord *m = &c->members[t->element+i];
        if ((m->type != IR_U32 && m->type != IR_F32) || m->offset>UINT32_MAX || m->size>UINT32_MAX || m->alignment>UINT32_MAX) return false;
    }
    return true;
}
static bool old_resource(const CompilerWorkspace *c, const ResourceRecord *r, bool v2) {
    if (r->type<7u || r->minimum>UINT32_MAX || r->alignment>UINT32_MAX || r->required_alignment>UINT32_MAX) return false;
    const TypeRecord *t = &c->types[r->type-7u];
    if (r->kind == DOE_COMPILER_UNIFORM_BUFFER) return v2 && t->kind == TYPE_STRUCT && flat_struct(c, t);
    return t->kind == TYPE_RUNTIME_ARRAY && (t->element == IR_U32 || t->element == IR_F32);
}
bool reflect_legacy_admit(Source *s, const CompilerWorkspace *c, bool v2) {
    uint32_t offset = NO_ID, count = 0u;
    NameSpan name = {0};
    for (uint32_t i = 0u;i<c->type_count;++i) if (c->types[i].kind == TYPE_STRUCT) {
            const TypeRecord *t = &c->types[i];
            ++count;
            if ((!v2 || count>DOE_COMPILER_MAX_STRUCTS || !flat_struct(c, t)) && t->name.offset<offset) {
                offset = t->name.offset;
                name = t->name;
            }
        }
    for (uint32_t i = 0u;i<c->resource_count;++i) if ((i >= DOE_COMPILER_MAX_BINDINGS || !old_resource(c, &c->resources[i], v2)) && c->resources[i].name.offset<offset) {
            offset = c->resources[i].name.offset;
            name = c->resources[i].name;
        }
    if (offset == NO_ID) return true;
    char message[160] = "legacy reflection projection cannot represent ";
    const size_t prefix = strlen(message);
    size_t length = name.length;
    if (length>sizeof(message)-prefix-1u) length = sizeof(message)-prefix-1u;
    memcpy(message+prefix, s->source+name.offset, length);
    message[prefix+length] = '\0';
    return source_error(s, DOE_COMPILER_UNSUPPORTED, offset, message);
}
static void copy_name(const Source *s, NameSpan span, char name[64]) {
    memcpy(name, s->source+span.offset, span.length);
    name[span.length] = '\0';
}
static DoeCompilerScalar old_scalar(TypeId type) {
    return type == IR_U32?DOE_COMPILER_U32:DOE_COMPILER_F32;
}
void reflect_program(const Source *s, const CompilerWorkspace *c, DoeCompilerReflection *out) {
    memset(out, 0, sizeof(*out));
    copy_name(s, c->functions[c->entries[0].function].name, out->entry_point);
    for (uint32_t i = 0u;i<3u;++i) out->workgroup_size[i] = c->entries[0].state.resolved.workgroup_size[i];
    out->binding_count = c->resource_count;
    for (uint32_t i = 0u;i<c->resource_count;++i) {
        const ResourceRecord *r = &c->resources[i];
        DoeCompilerBinding *b = &out->bindings[i];
        copy_name(s, r->name, b->name);
        b->group = r->group;
        b->binding = r->binding;
        b->scalar = old_scalar(c->types[r->type-7u].element);
        b->access = (DoeCompilerAccess)r->access;
        b->element_stride = 4u;
        b->runtime_sized = 1u;
    }
}
void reflect_program_v2(const Source *s, const CompilerWorkspace *c, DoeCompilerReflectionV2 *out) {
    memset(out, 0, sizeof(*out));
    copy_name(s, c->functions[c->entries[0].function].name, out->entry_point);
    for (uint32_t i = 0u;i<3u;++i) out->workgroup_size[i] = c->entries[0].state.resolved.workgroup_size[i];
    out->binding_count = c->resource_count;
    out->struct_count = declared_structs(c);
    for (uint32_t i = 0u, index = 0u;i<c->type_count;++i) if (c->types[i].kind == TYPE_STRUCT) {
            const TypeRecord *t = &c->types[i];
            DoeCompilerStructV2 *d = &out->structs[index++];
            copy_name(s, t->name, d->name);
            d->member_count = t->count;
            d->size_bytes = (uint32_t)t->state.layout.size;
            d->alignment_bytes = (uint32_t)t->state.layout.alignment;
            for (uint32_t j = 0u;j<t->count;++j) {
                const MemberRecord *m = &c->members[t->element+j];
                DoeCompilerMemberV2 *v = &d->members[j];
                copy_name(s, m->name, v->name);
                v->scalar = (uint32_t)old_scalar(m->type);
                v->offset_bytes = (uint32_t)m->offset;
                v->size_bytes = (uint32_t)m->size;
                v->alignment_bytes = (uint32_t)m->alignment;
            }
        }
    for (uint32_t i = 0u;i<c->resource_count;++i) {
        const ResourceRecord *r = &c->resources[i];
        DoeCompilerBindingV2 *b = &out->bindings[i];
        copy_name(s, r->name, b->name);
        b->group = r->group;
        b->binding = r->binding;
        b->kind = r->kind;
        b->access = r->access;
        b->struct_index = DOE_COMPILER_NO_STRUCT;
        b->minimum_binding_size_bytes = (uint32_t)r->minimum;
        b->type_alignment_bytes = (uint32_t)r->alignment;
        b->address_space_alignment_bytes = (uint32_t)r->required_alignment;
        if (r->kind == DOE_COMPILER_STORAGE_BUFFER) {
            b->scalar = (uint32_t)old_scalar(c->types[r->type-7u].element);
            b->element_stride = 4u;
            b->runtime_sized = 1u;
        }
        else {
            uint32_t index = 0u;
            for (uint32_t j = 0u;j<r->type-7u;++j) if (c->types[j].kind == TYPE_STRUCT) ++index;
            b->struct_index = index;
        }
    }
}
