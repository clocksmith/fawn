/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "compile_internal.h"
#include <string.h>

bool compiler_valid_span(const void *pointer, size_t count, size_t width, size_t alignment, bool selected) {
    if (!selected) return pointer == NULL && count == 0u;
    return count <= SIZE_MAX/width && (count == 0u || (pointer != NULL && (uintptr_t)pointer%alignment == 0u));
}

bool compiler_selection_shape(Source *s, const DoeCompilerEntrySelectionV5 *selection) {
    if (selection==NULL || selection->reserved!=0u || selection->mode>DOE_COMPILER_SELECT_NAME)
        return source_error(s,DOE_COMPILER_INPUT_ERROR,0u,"invalid entry selection descriptor");
    if (selection->mode!=DOE_COMPILER_SELECT_NAME)
        return (selection->name==NULL && selection->name_length==0u) ||
            source_error(s,DOE_COMPILER_INPUT_ERROR,0u,"NONE and AUTO selection require no name");
    if (selection->name==NULL || selection->name_length==0u || selection->name_length>63u)
        return source_error(s,DOE_COMPILER_INPUT_ERROR,0u,"NAME selection requires 1 through 63 identifier bytes");
    for (size_t i=0u;i<selection->name_length;++i) {
        const char ch=selection->name[i];
        if (ch!='_' && !(ch>='A' && ch<='Z') && !(ch>='a' && ch<='z') && !(i!=0u && ch>='0' && ch<='9'))
            return source_error(s,DOE_COMPILER_INPUT_ERROR,0u,"entry selection name must be an ASCII identifier");
    }
    return true;
}
bool compiler_select_entry(Source *s, const DoeCompilerEntrySelectionV5 *selection, uint32_t *selected) {
    const CompilerWorkspace *c=s->compiler;
    *selected=DOE_COMPILER_MODULE_NONE;
    if (selection->mode==DOE_COMPILER_SELECT_NONE) return true;
    if (selection->mode==DOE_COMPILER_SELECT_AUTO) {
        if (c->entry_count!=1u) return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,0u,"AUTO requires exactly one shader entry");
        *selected=0u;
        return true;
    }
    for (uint32_t i=0u;i<c->function_count;++i) {
        const FunctionRecord *f=&c->functions[i];
        if (f->name.length!=selection->name_length || memcmp(s->source+f->name.offset,selection->name,selection->name_length)!=0) continue;
        if ((f->flags&FUNCTION_ENTRY)==0u)
            return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,f->name.offset,"selected function is not a shader entry");
        *selected=f->entry_index;
        return true;
    }
    return source_error(s,DOE_COMPILER_SEMANTIC_ERROR,0u,"selected shader entry does not exist");
}
bool compiler_module_spans(Source *s, uint32_t flags, const DoeCompilerModuleOutputs *o) {
    const bool measure=(flags&DOE_COMPILER_MODULE_MEASURE)!=0u;
    const bool words=(flags&DOE_COMPILER_MODULE_WORDS)!=0u;
    const bool reflection=(flags&DOE_COMPILER_MODULE_REFLECTION)!=0u;
    if ((flags & 3u) == 0u || (flags & ~(7u | DOE_COMPILER_UNIFORM_BUFFER_STANDARD_LAYOUT | DOE_COMPILER_CORE_BINDING_NUMBERS | DOE_COMPILER_DISABLE_F16 | DOE_COMPILER_DISABLE_SUBGROUPS)) != 0u || measure!=(o==NULL))
        return source_error(s,DOE_COMPILER_INPUT_ERROR,0u,"invalid module output flags or measure descriptor");
    if (measure) return true;
    if (!compiler_valid_span(o->words,o->word_capacity,sizeof(*o->words),_Alignof(uint32_t),words) ||
        !compiler_valid_span(o->types,o->type_capacity,sizeof(*o->types),_Alignof(DoeCompilerTypeV4),reflection) ||
        !compiler_valid_span(o->members,o->member_capacity,sizeof(*o->members),_Alignof(DoeCompilerMemberV4),reflection) ||
        !compiler_valid_span(o->resources,o->resource_capacity,sizeof(*o->resources),_Alignof(DoeCompilerResourceV4),reflection) ||
        !compiler_valid_span(o->workgroups,o->workgroup_capacity,sizeof(*o->workgroups),_Alignof(DoeCompilerWorkgroupV5),reflection) ||
        !compiler_valid_span(o->entries,o->entry_capacity,sizeof(*o->entries),_Alignof(DoeCompilerEntryV5),reflection) ||
        !compiler_valid_span(o->entry_references,o->entry_reference_capacity,sizeof(*o->entry_references),_Alignof(uint32_t),reflection) ||
        !compiler_valid_span(o->names,o->name_capacity,1u,1u,reflection))
        return source_error(s,DOE_COMPILER_INPUT_ERROR,0u,"invalid selected module output span or capacity product");
    return true;
}
bool compiler_module_records(const DoeCompilerModuleOutputs *outputs) {
    return outputs == NULL || (outputs->type_record_size == sizeof(DoeCompilerTypeV4) &&
        outputs->member_record_size == sizeof(DoeCompilerMemberV4) && outputs->resource_record_size == sizeof(DoeCompilerResourceV4) &&
        outputs->workgroup_record_size == sizeof(DoeCompilerWorkgroupV5) && outputs->entry_record_size == sizeof(DoeCompilerEntryV5));
}
