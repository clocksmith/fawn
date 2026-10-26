/* SPDX-License-Identifier: MIT
 * Canonical nonfatal diagnostic records and source attribution. */
#include "diagnostics_internal.h"
#include <string.h>

static const char *rule_message(uint32_t rule)
{
    return rule==DOE_COMPILER_RULE_DERIVATIVE_UNIFORMITY ?
        "derivative uniform control cannot be established" : "subgroup uniform control cannot be established";
}
bool compiler_uniformity_notice(Source *source, uint32_t expression, uint32_t rule)
{
    Expression *trigger=&source->compiler->expressions[expression];
    const uint32_t severity=source->diagnostic_severity[rule-1u];
    if(severity==DOE_COMPILER_DIAGNOSTIC_ERROR)
        return source_error(source,DOE_COMPILER_SEMANTIC_ERROR,trigger->source_offset,rule_message(rule));
    if(severity!=DOE_COMPILER_DIAGNOSTIC_OFF) trigger->diagnostic_rule=(unsigned char)rule;
    return true;
}
size_t compiler_notice_count(const Source *source)
{
    size_t count=source->unknown_diagnostic.length ? 1u : 0u;
    for(uint32_t i=0;i<source->compiler->expression_count;++i)
        if(source->compiler->expressions[i].diagnostic_rule) ++count;
    return count;
}
static void notice(const Source *source, DoeCompilerNoticeV18 *output, uint32_t severity,
    uint32_t rule, NameSpan location, const char *message)
{
    DoeCompilerDiagnostic diagnostic={0};
    Source temporary=*source;
    temporary.diagnostic=&diagnostic;
    (void)source_error(&temporary,DOE_COMPILER_OK,location.offset,message);
    *output=(DoeCompilerNoticeV18){.severity=severity,.rule=rule,.offset=location.offset,
        .length=location.length,.line=diagnostic.line,.column=diagnostic.column};
    memcpy(output->message,diagnostic.message,sizeof(output->message));
}
void compiler_notices_publish(const Source *source, DoeCompilerNoticeV18 *outputs)
{
    size_t next=0;
    if(source->unknown_diagnostic.length)
        notice(source,&outputs[next++],DOE_COMPILER_DIAGNOSTIC_WARNING,DOE_COMPILER_RULE_UNRECOGNIZED,
            source->unknown_diagnostic,"unrecognized diagnostic rule");
    for(uint32_t i=0;i<source->compiler->expression_count;++i){
        const Expression *expression=&source->compiler->expressions[i];
        const uint32_t rule=expression->diagnostic_rule;
        if(rule) notice(source,&outputs[next++],source->diagnostic_severity[rule-1u],rule,
            source_name_at(source,expression->source_offset),rule_message(rule));
    }
}
