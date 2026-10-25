/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "sema.h"
#include "stage.h"

static bool leaf_shape(const CompilerWorkspace *c, TypeId type, TypeId *scalar, uint32_t *lanes) {
    *scalar = type;
    *lanes = 1u;
    if (type >= IR_PRIMITIVE_COUNT) {
        const TypeRecord *record = &c->types[type - IR_PRIMITIVE_COUNT];
        if (record->kind != TYPE_VECTOR) return false;
        *scalar = record->element;
        *lanes = record->count;
    }
    return (*scalar >= IR_BOOL && *scalar <= IR_F32) || *scalar == IR_F16;
}
static TypeId attribute_type(const CompilerWorkspace *c, const InterfaceAttribute *attribute) {
    if (attribute->owner == STAGE_ATTRIBUTE_MEMBER) return c->members[attribute->index].type;
    if (attribute->owner == STAGE_ATTRIBUTE_PARAMETER) return c->formals[attribute->index].type.resolved.type;
    return c->functions[attribute->index].result_type;
}
static bool attribute_valid(Source *source, const LocalScope *scope, InterfaceAttribute *attribute) {
    TypeId scalar;
    uint32_t lanes;
    const uint32_t binding = attribute->flags & (STAGE_HAS_BUILTIN | STAGE_HAS_LOCATION);
    if (binding != STAGE_HAS_BUILTIN && binding != STAGE_HAS_LOCATION)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset,
            "interface leaf requires exactly one builtin or location");
    TypeId type=attribute_type(source->compiler,attribute);
    bool clip=attribute->builtin==DOE_COMPILER_BUILTIN_CLIP_DISTANCES;
    bool clip_shape=false;
    if (clip && type>=IR_PRIMITIVE_COUNT) {
        const TypeRecord *record=&source->compiler->types[type-IR_PRIMITIVE_COUNT];
        clip_shape=record->kind==TYPE_ARRAY && record->element==IR_F32 && record->count>=1 && record->count<=8;
        scalar=IR_F32; lanes=record->count;
    }
    if (clip ? !clip_shape : !leaf_shape(source->compiler, type, &scalar, &lanes))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset,
            "interface attribute requires a concrete scalar or vector leaf");
    if (binding == STAGE_HAS_LOCATION) {
        if (scalar == IR_BOOL)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "location type must be numeric");
        if (!integer_attribute(source, attribute->location_expression, scope, false, &attribute->location_expression)) return false;
        if (attribute->location_expression == NO_ID)
            return source_error(source, DOE_COMPILER_UNSUPPORTED, attribute->offset, "location exceeds interface metadata format");
    } else {
        if ((attribute->builtin == DOE_COMPILER_BUILTIN_SUBGROUP_SIZE || attribute->builtin == DOE_COMPILER_BUILTIN_SUBGROUP_INVOCATION_ID) && (source->compiler->module_flags & MODULE_SUBGROUPS) == 0u)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "subgroup stage inputs require enable subgroups");
        if ((clip && !(source->compiler->module_flags & MODULE_CLIP_DISTANCES)) ||
            (attribute->builtin==DOE_COMPILER_BUILTIN_PRIMITIVE_INDEX && !(source->compiler->module_flags & MODULE_PRIMITIVE_INDEX)))
            return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,attribute->offset,"builtin requires its enable directive");
        const bool valid = clip ? clip_shape : attribute->builtin == DOE_COMPILER_BUILTIN_POSITION ? scalar == IR_F32 && lanes == 4u :
            attribute->builtin == DOE_COMPILER_BUILTIN_FRONT_FACING ? scalar == IR_BOOL && lanes == 1u :
            attribute->builtin == DOE_COMPILER_BUILTIN_FRAG_DEPTH ? scalar == IR_F32 && lanes == 1u :
            scalar == IR_U32 && lanes == 1u;
        if (!valid) return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "builtin interface type is incorrect");
        if ((attribute->flags & STAGE_HAS_INTERPOLATION) != 0u)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "builtin cannot have interpolation");
    }
    if (attribute->flags & STAGE_HAS_BLEND_SRC) {
        if (!(source->compiler->module_flags & MODULE_DUAL_SOURCE_BLENDING) || binding!=STAGE_HAS_LOCATION ||
            attribute->owner!=STAGE_ATTRIBUTE_MEMBER || attribute->location_expression!=0)
            return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,attribute->offset,"blend_src requires enabled dual-source location-zero structure member");
        if (!integer_attribute(source,attribute->blend_expression,scope,false,&attribute->blend_expression)) return false;
        if (attribute->blend_expression>1) return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,attribute->offset,"blend_src must be zero or one");
    }
    if ((attribute->flags & STAGE_HAS_INVARIANT) != 0u && attribute->builtin != DOE_COMPILER_BUILTIN_POSITION)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "invariant requires the position builtin");
    if ((attribute->flags & STAGE_HAS_INTERPOLATION) != 0u) {
        if (attribute->sampling == 0u)
            attribute->sampling = attribute->interpolation == DOE_COMPILER_INTERPOLATION_FLAT ?
                DOE_COMPILER_SAMPLING_FIRST : DOE_COMPILER_SAMPLING_CENTER;
        const bool flat = attribute->interpolation == DOE_COMPILER_INTERPOLATION_FLAT;
        if ((flat && attribute->sampling < DOE_COMPILER_SAMPLING_FIRST) ||
            (!flat && attribute->sampling > DOE_COMPILER_SAMPLING_SAMPLE))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "invalid interpolation sampling for interpolation type");
    }
    return true;
}
static bool builtin_stage(uint32_t stage, uint32_t direction, uint32_t builtin) {
    if (stage == DOE_COMPILER_STAGE_VERTEX)
        return direction == DOE_COMPILER_INTERFACE_INPUT ?
            builtin == DOE_COMPILER_BUILTIN_VERTEX_INDEX || builtin == DOE_COMPILER_BUILTIN_INSTANCE_INDEX :
            builtin == DOE_COMPILER_BUILTIN_POSITION || builtin == DOE_COMPILER_BUILTIN_CLIP_DISTANCES;
    return direction == DOE_COMPILER_INTERFACE_INPUT ?
        builtin == DOE_COMPILER_BUILTIN_POSITION || builtin == DOE_COMPILER_BUILTIN_FRONT_FACING || builtin == DOE_COMPILER_BUILTIN_PRIMITIVE_INDEX ||
        builtin == DOE_COMPILER_BUILTIN_SAMPLE_INDEX || builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK ||
        builtin == DOE_COMPILER_BUILTIN_SUBGROUP_SIZE || builtin == DOE_COMPILER_BUILTIN_SUBGROUP_INVOCATION_ID :
        builtin == DOE_COMPILER_BUILTIN_FRAG_DEPTH || builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK;
}
static bool append_leaf(Source *source, StageInterface leaf, const InterfaceAttribute *attribute) {
    StageWorkspace *stages = source->stages;
    const CompilerWorkspace *c = source->compiler;
    if (attribute == NULL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, leaf.name.offset, "entry interface leaf is missing builtin or location");
    const uint32_t stage = stage_kind(&c->functions[c->entries[leaf.entry].function]);
    leaf.location = (attribute->flags & STAGE_HAS_LOCATION) != 0u ? attribute->location_expression : NO_ID;
    leaf.builtin = attribute->builtin;
    leaf.flags = (attribute->flags & STAGE_HAS_INVARIANT) != 0u ? DOE_COMPILER_INTERFACE_INVARIANT : 0u;
    if (attribute->flags & STAGE_HAS_BLEND_SRC) {
        if (stage!=DOE_COMPILER_STAGE_FRAGMENT || leaf.direction!=DOE_COMPILER_INTERFACE_OUTPUT)
            return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,attribute->offset,"blend_src requires fragment output");
        leaf.flags|=DOE_COMPILER_INTERFACE_BLEND_SRC;
        if (attribute->blend_expression) leaf.flags|=DOE_COMPILER_INTERFACE_BLEND_SRC_ONE;
    }
    if (leaf.builtin != 0u && !builtin_stage(stage, leaf.direction, leaf.builtin))
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "builtin is unavailable for this entry stage and direction");
    const bool interpolated = leaf.builtin == 0u &&
        ((stage == DOE_COMPILER_STAGE_VERTEX && leaf.direction == DOE_COMPILER_INTERFACE_OUTPUT) ||
         (stage == DOE_COMPILER_STAGE_FRAGMENT && leaf.direction == DOE_COMPILER_INTERFACE_INPUT));
    if (interpolated) {
        leaf.interpolation = attribute->interpolation;
        leaf.sampling = attribute->sampling;
        if (type_leaf(c, leaf.type) != IR_F32 && type_leaf(c,leaf.type) != IR_F16 && leaf.interpolation != DOE_COMPILER_INTERPOLATION_FLAT)
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "integer interstage IO requires explicit flat interpolation");
        if (leaf.interpolation == 0u) {
            leaf.interpolation = DOE_COMPILER_INTERPOLATION_PERSPECTIVE;
            leaf.sampling = DOE_COMPILER_SAMPLING_CENTER;
        }
    } else if ((attribute->flags & STAGE_HAS_INTERPOLATION) != 0u)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "interpolation requires vertex output or fragment input");
    leaf.target_type = leaf.type;
    /* Native stage carriers are f32; WGSL values and reflection stay f16.
     * No optional 16-bit Input/Output storage capability is required. */
    if (type_leaf(c,leaf.type) == IR_F16) {
        leaf.target_type = IR_F32;
        const uint32_t lanes = type_vector_width(c,leaf.type);
        if (lanes > 1u && !type_intern(source,TYPE_VECTOR,IR_F32,lanes,&leaf.target_type)) return false;
    }
    if (leaf.builtin == DOE_COMPILER_BUILTIN_SAMPLE_MASK &&
        !type_intern(source, TYPE_ARRAY, IR_U32, 1u, &leaf.target_type)) return false;
    StageEntry *entry = &stages->entries[leaf.entry];
    for (uint32_t i = entry->interface_first; i < stages->interface_count; ++i) {
        const StageInterface *prior = &stages->interfaces[i];
        if (prior->direction == leaf.direction &&
            ((leaf.builtin != 0u && prior->builtin == leaf.builtin) ||
             (leaf.builtin == 0u && prior->builtin == 0u && prior->location == leaf.location &&
              ((prior->flags ^ leaf.flags) & DOE_COMPILER_INTERFACE_BLEND_SRC_ONE)==0)))
            return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "duplicate builtin or location in entry interface");
    }
    if (stages->interface_count == stages->interface_capacity)
        return source_pool_error(source, DOE_COMPILER_POOL_STAGE_INTERFACES, (size_t)stages->interface_count + 1u,
            attribute->offset, "stage interface capacity exceeded");
    stages->interfaces[stages->interface_count++] = leaf;
    ++entry->interface_count;
    return true;
}
static bool flatten_interface(Source *source, StageInterface leaf, uint32_t owner, uint32_t index) {
    const CompilerWorkspace *c = source->compiler;
    const InterfaceAttribute *attribute = stage_attribute(source, owner, index, leaf.name.offset, false);
    if (leaf.type < IR_PRIMITIVE_COUNT || c->types[leaf.type - IR_PRIMITIVE_COUNT].kind != TYPE_STRUCT)
        return append_leaf(source, leaf, attribute);
    if (attribute != NULL)
        return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, attribute->offset, "structure interface cannot carry a leaf attribute");
    const TypeRecord *type = &c->types[leaf.type - IR_PRIMITIVE_COUNT];
    for (uint32_t i = 0u; i < type->count; ++i) {
        const uint32_t member = type->element + i;
        StageInterface child = leaf;
        child.member = i;
        child.type = c->members[member].type;
        child.name = c->members[member].name;
        if (!append_leaf(source, child, stage_attribute(source, STAGE_ATTRIBUTE_MEMBER, member, child.name.offset, false))) return false;
    }
    return true;
}
/* WGSL validates dual-source structure declarations even when unreachable. */
static bool dual_source_structures(Source *source) {
    const CompilerWorkspace *c=source->compiler;
    for (uint32_t i=0;i<c->type_count;++i) {
        const TypeRecord *type=&c->types[i];
        if (type->kind!=TYPE_STRUCT) continue;
        const InterfaceAttribute *pair[2]={NULL,NULL};
        TypeId values[2]={IR_VOID,IR_VOID};
        uint32_t locations=0;
        for (uint32_t j=0;j<type->count;++j) {
            uint32_t member=type->element+j;
            const InterfaceAttribute *attribute=stage_attribute(source,STAGE_ATTRIBUTE_MEMBER,member,c->members[member].name.offset,false);
            if (!attribute || !(attribute->flags & STAGE_HAS_LOCATION)) continue;
            ++locations;
            if (!(attribute->flags & STAGE_HAS_BLEND_SRC)) continue;
            uint32_t index=attribute->blend_expression;
            if (pair[index]) return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,attribute->offset,"duplicate blend source");
            pair[index]=attribute;
            values[index]=c->members[member].type;
        }
        if ((pair[0] || pair[1]) && (!pair[0] || !pair[1] || locations!=2 || values[0]!=values[1]))
            return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,type->name.offset,"dual-source structure requires exactly two matching indexed outputs");
    }
    return true;
}
bool sema_stage_interfaces(Source *source, const LocalScope *scope) {
    if (source->stages == NULL) return true;
    CompilerWorkspace *c = source->compiler;
    StageWorkspace *stages = source->stages;
    for (uint32_t i = 0u; i < stages->attribute_count; ++i)
        if (!attribute_valid(source, scope, &stages->attributes[i])) return false;
    if (!dual_source_structures(source)) return false;
    for (uint32_t i = 0u; i < c->entry_count; ++i) {
        const FunctionRecord *function = &c->functions[c->entries[i].function];
        if (!stage_graphics(function)) continue;
        StageEntry *entry = &stages->entries[i];
        entry->interface_first = stages->interface_count;
        for (uint32_t p = 0u; p < entry->formal_count; ++p) {
            const uint32_t formal = entry->formal_first + p;
            const StageInterface leaf = {.entry = i, .direction = DOE_COMPILER_INTERFACE_INPUT,
                .formal = formal, .member = NO_ID, .type = c->formals[formal].type.resolved.type,
                .name = c->formals[formal].name};
            if (!flatten_interface(source, leaf, STAGE_ATTRIBUTE_PARAMETER, formal)) return false;
        }
        if (function->result_type != IR_VOID) {
            const StageInterface leaf = {.entry = i, .direction = DOE_COMPILER_INTERFACE_OUTPUT,
                .formal = NO_ID, .member = NO_ID, .type = function->result_type, .name = function->name};
            if (!flatten_interface(source, leaf, STAGE_ATTRIBUTE_RESULT, c->entries[i].function)) return false;
        }
        const StageInterface *blend0=NULL,*blend1=NULL;
        bool dual=false;
        for (uint32_t j=entry->interface_first;j<stages->interface_count;++j) {
            const StageInterface *leaf=&stages->interfaces[j];
            if (leaf->flags & DOE_COMPILER_INTERFACE_BLEND_SRC) {
                dual=true;
                if (leaf->flags & DOE_COMPILER_INTERFACE_BLEND_SRC_ONE) blend1=leaf; else blend0=leaf;
            }
        }
        if (dual) {
            if (!blend0 || !blend1 || blend0->type!=blend1->type)
                return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,function->name.offset,"dual-source outputs require matching zero and one sources");
            for (uint32_t j=entry->interface_first;j<stages->interface_count;++j) {
                const StageInterface *leaf=&stages->interfaces[j];
                if (leaf->direction==DOE_COMPILER_INTERFACE_OUTPUT && !leaf->builtin && !(leaf->flags & DOE_COMPILER_INTERFACE_BLEND_SRC))
                    return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,function->name.offset,"dual-source output cannot mix unindexed locations");
            }
        }
        if ((function->flags & FUNCTION_VERTEX) != 0u) {
            bool position = false;
            for (uint32_t j = entry->interface_first; j < stages->interface_count; ++j)
                if (stages->interfaces[j].direction == DOE_COMPILER_INTERFACE_OUTPUT &&
                    stages->interfaces[j].builtin == DOE_COMPILER_BUILTIN_POSITION) position = true;
            if (!position) return source_error(source, DOE_COMPILER_SEMANTIC_ERROR, function->name.offset,
                "vertex entry must return the position builtin");
        }
    }
    return true;
}
