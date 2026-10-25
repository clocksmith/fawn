/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "parse_internal.h"

static uint32_t builtin_name(const Parser *p) {
    static const char *const names[] = {"position", "vertex_index", "instance_index",
        "front_facing", "frag_depth", "sample_index", "sample_mask", "subgroup_size", "subgroup_invocation_id", "clip_distances", "primitive_index"};
    for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i)
        if (parser_spelled(p, parser_peek(p), names[i])) return i + 1u;
    return 0u;
}
static bool interpolation(Parser *p, InterfaceAttribute *attribute) {
    if (parser_spelled(p, parser_peek(p), "perspective")) attribute->interpolation = DOE_COMPILER_INTERPOLATION_PERSPECTIVE;
    else if (parser_spelled(p, parser_peek(p), "linear")) attribute->interpolation = DOE_COMPILER_INTERPOLATION_LINEAR;
    else if (parser_spelled(p, parser_peek(p), "flat")) attribute->interpolation = DOE_COMPILER_INTERPOLATION_FLAT;
    else return parser_reject(p, "perspective, linear or flat interpolation");
    ++p->cursor;
    if (parser_peek(p)->kind != ',') return true;
    ++p->cursor;
    if (parser_spelled(p, parser_peek(p), "center")) attribute->sampling = DOE_COMPILER_SAMPLING_CENTER;
    else if (parser_spelled(p, parser_peek(p), "centroid")) attribute->sampling = DOE_COMPILER_SAMPLING_CENTROID;
    else if (parser_spelled(p, parser_peek(p), "sample")) attribute->sampling = DOE_COMPILER_SAMPLING_SAMPLE;
    else if (parser_spelled(p, parser_peek(p), "first")) attribute->sampling = DOE_COMPILER_SAMPLING_FIRST;
    else if (parser_spelled(p, parser_peek(p), "either")) attribute->sampling = DOE_COMPILER_SAMPLING_EITHER;
    else return parser_reject(p, "interpolation sampling enumerant");
    ++p->cursor;
    return true;
}
bool parse_interface_attribute(Parser *p, Syntax *syntax, uint32_t owner, uint32_t index) {
    const uint32_t offset = parser_peek(p)->offset;
    if (!parser_punctuation(p, '@', "@")) return false;
    uint32_t flag;
    if (parser_spelled(p, parser_peek(p), "builtin")) flag = STAGE_HAS_BUILTIN;
    else if (parser_spelled(p, parser_peek(p), "location")) flag = STAGE_HAS_LOCATION;
    else if (parser_spelled(p, parser_peek(p), "interpolate")) flag = STAGE_HAS_INTERPOLATION;
    else if (parser_spelled(p, parser_peek(p), "blend_src")) flag = STAGE_HAS_BLEND_SRC;
    else if (parser_spelled(p, parser_peek(p), "invariant")) flag = STAGE_HAS_INVARIANT;
    else return source_error(p->source, DOE_COMPILER_UNSUPPORTED, offset, "unsupported stage interface attribute");
    InterfaceAttribute *attribute = stage_attribute(p->source, owner, index, offset, true);
    if (attribute == NULL) return false;
    if ((attribute->flags & flag) != 0u)
        return source_error(p->source, DOE_COMPILER_SEMANTIC_ERROR, offset, "duplicate stage interface attribute");
    attribute->flags |= flag;
    ++p->cursor;
    if (flag == STAGE_HAS_INVARIANT) return true;
    if (!parser_punctuation(p, '(', "(")) return false;
    if (flag == STAGE_HAS_BLEND_SRC) {
        if (!expression(p, syntax, 0u, 0u, &attribute->blend_expression)) return false;
    } else if (flag == STAGE_HAS_LOCATION) {
        if (!expression(p, syntax, 0u, 0u, &attribute->location_expression)) return false;
    } else if (flag == STAGE_HAS_BUILTIN) {
        attribute->builtin = builtin_name(p);
        if (attribute->builtin == 0u)
            return source_error(p->source, DOE_COMPILER_UNSUPPORTED, parser_peek(p)->offset, "unsupported graphics builtin");
        ++p->cursor;
    } else if (!interpolation(p, attribute)) return false;
    if (parser_peek(p)->kind == ',') ++p->cursor;
    return parser_punctuation(p, ')', ")");
}
bool parse_graphics_parameters(Parser *p, Syntax *syntax) {
    StageEntry *entry = &p->source->stages->entries[syntax->entry_count];
    entry->formal_first = syntax->formal_count;
    if (!parser_punctuation(p, '(', "(")) return false;
    while (parser_peek(p)->kind != ')') {
        if (syntax->formal_count == syntax->limits.tokens / 4u)
            return source_pool_error(p->source, DOE_COMPILER_POOL_TOKENS,
                4u * ((size_t)syntax->formal_count + 1u), parser_peek(p)->offset, "formal parameter capacity exceeded");
        while (parser_peek(p)->kind == '@')
            if (!parse_interface_attribute(p, syntax, STAGE_ATTRIBUTE_PARAMETER, syntax->formal_count)) return false;
        FormalRecord *formal = &syntax->formals[syntax->formal_count];
        if (!parser_name_span(p, &formal->name) || !parser_punctuation(p, ':', ":") ||
            !type_reference(p, syntax, &formal->type.annotation)) return false;
        ++syntax->formal_count;
        ++entry->formal_count;
        if (parser_peek(p)->kind != ',') break;
        ++p->cursor;
    }
    return parser_punctuation(p, ')', ")");
}
