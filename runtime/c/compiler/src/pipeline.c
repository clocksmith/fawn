/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "compile_internal.h"
#include "override.h"
#include <string.h>

/* One analysis owner. The caller always ends the initialized environment, even
 * on failure, and publishes only after restoration and target work succeed. */
bool compiler_analyze(Source *source, CompilerWorkspace *c, NumericEnvironment *environment) {
    source->error_cause = SOURCE_CAUSE_NONE;
    environment->active = false;
    source->compiler = c;
    source->limits = &c->limits;
    if (source->source == NULL && source->length != 0u) {
        source->length = 0u;
        return source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "NULL source requires zero length");
    }
    if (source->length>c->limits.source_bytes)
        return source_pool_error(source, DOE_COMPILER_POOL_SOURCE, source->length, (c->limits.flags&LIMIT_LEGACY) != 0u?0u:c->limits.source_bytes, "source byte capacity exceeded");
    if (source->length == 0u && (c->limits.flags & LIMIT_MODULE) == 0u)
        return source_error(source, DOE_COMPILER_INPUT_ERROR, 0u, "source is empty");
    const char *nul = source->length == 0u ? NULL : memchr(source->source, '\0', source->length);
    if (nul != NULL) return source_error(source, DOE_COMPILER_INPUT_ERROR, (size_t)(nul-source->source), "embedded NUL byte in source");
    if (!lex_source(source, c) || !parse_tokens(source, c)) return false;
    for (uint32_t i=0u;i<c->expression_count;++i) c->expression_references[i]=(SourceReference){SOURCE_NONE,NO_ID};
    for (uint32_t i=0u;i<c->statement_count;++i) c->statement_references[i]=(SourceReference){SOURCE_NONE,NO_ID};
    if (!numeric_environment_begin(source, environment) || !validate_syntax(source, c)) return false;
    if (source->specialization != NULL && source->specialization->selection != NULL &&
        (!override_plan(source) || !override_reparse(source))) return false;
    return compiler_target_phase(source);
}
