/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "internal.h"
#include <string.h>

bool input_subgroup(InputKind kind) {
    return kind == INPUT_SUBGROUP_SIZE || kind == INPUT_SUBGROUP_INVOCATION ||
        kind == INPUT_NUM_SUBGROUPS || kind == INPUT_SUBGROUP_ID;
}
bool input_scalar(InputKind kind) {
    return kind == INPUT_LOCAL_INDEX || input_subgroup(kind);
}
bool source_reference_record(Source *s, SourceReference *slot, SourceReferenceKind kind,
                             uint32_t index, uint32_t offset) {
    if (slot->index!=NO_ID && (slot->kind!=kind || slot->index!=index))
        return source_error(s,DOE_COMPILER_INPUT_ERROR,offset,"resolved source reference changed between semantic visits");
    *slot=(SourceReference){kind,index};
    return true;
}
typedef struct { uint64_t used, limit; } ClosureWork;
static bool closure_charge(Source *s, ClosureWork *work, uint64_t count) {
    if (count>work->limit-work->used)
        return source_error(s,DOE_COMPILER_UNSUPPORTED,s->length,"source interface work format exhausted");
    work->used+=count;
    return true;
}
static bool binding_less(const CompilerWorkspace *c, uint32_t a, uint32_t b) {
    const ResourceRecord *x=&c->resources[a], *y=&c->resources[b];
    if (x->group!=y->group) return x->group<y->group;
    if (x->binding!=y->binding) return x->binding<y->binding;
    return a<b;
}
static bool sift_bindings(Source *s, ClosureWork *work, uint32_t *indices,
                          size_t root, size_t count) {
    while (root<count/2u) {
        if (!closure_charge(s,work,8u)) return false;
        size_t child=2u*root+1u;
        if (child+1u<count && binding_less(s->compiler,indices[child],indices[child+1u])) ++child;
        if (!binding_less(s->compiler,indices[root],indices[child])) return true;
        const uint32_t swap=indices[root];
        indices[root]=indices[child];
        indices[child]=swap;
        root=child;
    }
    return true;
}
static bool check_bindings(Source *s, EntryRecord *entry, ClosureWork *work,
                           uint32_t *sorted) {
    CompilerWorkspace *c=s->compiler;
    const size_t count=entry->state.resolved.resource_count;
    for (size_t i=0u;i<count;++i) sorted[i]=c->entry_references[entry->state.resolved.resource_first+i];
    if (!closure_charge(s,work,count)) return false;
    for (size_t i=count/2u;i!=0u;--i) if (!sift_bindings(s,work,sorted,i-1u,count)) return false;
    for (size_t i=count;i>1u;--i) {
        if (!closure_charge(s,work,3u)) return false;
        const uint32_t swap=sorted[0];
        sorted[0]=sorted[i-1u];
        sorted[i-1u]=swap;
        if (!sift_bindings(s,work,sorted,0u,i-1u)) return false;
    }
    for (size_t i=1u;i<count;++i) {
        if (!closure_charge(s,work,2u)) return false;
        const ResourceRecord *a=&c->resources[sorted[i-1u]], *b=&c->resources[sorted[i]];
        if (a->group==b->group && a->binding==b->binding)
            return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,b->name.offset,
                "distinct resources share a group and binding in one entry interface");
    }
    return true;
}
static bool closure_reference(Source *s, EntryRecord *entry, SourceReference ref,
                               unsigned char *marks) {
    CompilerWorkspace *c=s->compiler;
    switch (ref.kind) {
        case SOURCE_RESOURCE: marks[ref.index]=1u; break;
        case SOURCE_WORKGROUP: marks[(size_t)c->resource_count+ref.index]=1u; break;
        case SOURCE_INPUT:
            if (ref.index<entry->input_first || ref.index-entry->input_first>=entry->input_count)
                return source_error(s,DOE_COMPILER_INPUT_ERROR,c->functions[entry->function].name.offset,
                    "entry interface contains a sibling input identity");
            entry->source_input_mask|=1u<<c->entry_inputs[ref.index].kind;
            break;
        case SOURCE_NONE: case SOURCE_FUNCTION: case SOURCE_BARRIER: case SOURCE_SUBGROUP: case SOURCE_IMAGE: break;
    }
    return true;
}
static bool collect_entry(Source *s, EntryRecord *entry, ClosureWork *work,
                           unsigned char *marks) {
    CompilerWorkspace *c=s->compiler;
    for (uint32_t i=0u;i<c->function_count;++i) c->functions[i].state.graph.color=0u;
    const size_t roots=(size_t)c->resource_count+c->workgroup_count;
    memset(marks,0,roots);
    if (!closure_charge(s,work,(uint64_t)c->function_count+roots)) return false;
    uint32_t current=entry->function;
    c->functions[current].state.graph.color=1u;
    c->functions[current].state.graph.scan=c->functions[current].expression_start;
    c->functions[current].state.graph.parent=NO_ID;
    while (current!=NO_ID) {
        FunctionRecord *f=&c->functions[current];
        if (!closure_charge(s,work,5u)) return false;
        if (f->state.graph.color==1u && f->state.graph.scan<f->expression_end) {
            const SourceReference ref=c->expression_references[f->state.graph.scan++];
            if (!closure_reference(s,entry,ref,marks)) return false;
            if (ref.kind==SOURCE_FUNCTION && c->functions[ref.index].state.graph.color==0u) {
                FunctionRecord *child=&c->functions[ref.index];
                child->state.graph.color=1u;
                child->state.graph.scan=child->expression_start;
                child->state.graph.parent=current;
                current=ref.index;
            }
            continue;
        }
        if (f->state.graph.color==1u) {
            f->state.graph.color=2u;
            f->state.graph.scan=f->statement_start;
        }
        if (f->state.graph.scan<f->statement_end) {
            if (!closure_reference(s,entry,c->statement_references[f->state.graph.scan++],marks)) return false;
            continue;
        }
        f->state.graph.color=3u;
        current=f->state.graph.parent;
    }
    return true;
}
static bool append_reference(Source *s, const EntryRecord *entry, uint32_t index) {
    CompilerWorkspace *c=s->compiler;
    if (c->entry_reference_count==c->limits.entry_references)
        return source_pool_error(s,DOE_COMPILER_POOL_ENTRY_REFERENCES,
            (size_t)c->entry_reference_count+1u,c->functions[entry->function].name.offset,
            "entry reference capacity exceeded");
    c->entry_references[c->entry_reference_count++]=index;
    return true;
}
static bool publish_closure(Source *s, EntryRecord *entry, ClosureWork *work,
                            const unsigned char *marks) {
    CompilerWorkspace *c=s->compiler;
    entry->state.resolved.resource_first=c->entry_reference_count;
    for (uint32_t i=0u;i<c->resource_count;++i) {
        if (!closure_charge(s,work,3u)) return false;
        if (marks[i]==0u) continue;
        if (!append_reference(s,entry,i)) return false;
        ++entry->state.resolved.resource_count;
    }
    entry->state.resolved.workgroup_first=c->entry_reference_count;
    for (uint32_t i=0u;i<c->workgroup_count;++i) {
        if (!closure_charge(s,work,3u)) return false;
        if (marks[(size_t)c->resource_count+i]==0u) continue;
        if (!append_reference(s,entry,i)) return false;
        ++entry->state.resolved.workgroup_count;
        const TypeId type=c->workgroups[i].type;
        const uint64_t size=type_size(c,type), alignment=type_alignment(c,type);
        const uint64_t remainder=size%alignment, padding=remainder==0u?0u:alignment-remainder;
        if (size>UINT64_MAX-padding || entry->state.resolved.workgroup_storage_bytes>UINT64_MAX-(size+padding))
            return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,c->workgroups[i].name.offset,
                "logical entry workgroup storage total overflows uint64");
        entry->state.resolved.workgroup_storage_bytes+=size+padding;
    }
    return true;
}
bool module_closures(Source *s) {
    CompilerWorkspace *c=s->compiler;
    uint64_t logarithm=1u;
    for (uint64_t n=c->resource_count;n>2u;n=(n+1u)/2u) ++logarithm;
    const uint64_t per_entry=20u*((uint64_t)c->function_count+c->expression_count+c->statement_count+
        c->resource_count+c->workgroup_count+1u)+8u*(uint64_t)c->resource_count*(logarithm+1u);
    if (c->entry_count!=0u && per_entry>UINT64_MAX/c->entry_count)
        return source_error(s,DOE_COMPILER_UNSUPPORTED,s->length,"source interface work bound exceeds uint64 format");
    ClosureWork work={.limit=per_entry*c->entry_count};
    unsigned char *marks=c->uniformity;
    size_t offset=(size_t)c->limits.resources+c->limits.tokens/8u;
    offset+=(4u-offset%4u)%4u;
    uint32_t *sorted=(uint32_t *)(marks+offset);
    for (uint32_t i=0u;i<c->entry_count;++i) {
        EntryRecord *entry=&c->entries[i];
        if (!collect_entry(s,entry,&work,marks) || !publish_closure(s,entry,&work,marks) ||
            !check_bindings(s,entry,&work,sorted)) return false;
        if (i==0u && c->alias.length!=0u) {
            for (uint32_t j=entry->input_first;j<entry->input_first+entry->input_count;++j)
                if (source_spans_equal(s,c->alias_source,c->entry_inputs[j].name))
                    entry->source_input_mask|=1u<<c->entry_inputs[j].kind;
        }
    }
    return true;
}
