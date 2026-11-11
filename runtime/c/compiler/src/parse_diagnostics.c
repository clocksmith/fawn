/* SPDX-License-Identifier: MIT
 * Global and scoped diagnostic controls over the canonical token parser. */
#include "parse_internal.h"
#include "diagnostics_internal.h"
#include "sema.h"

typedef struct {
    NameSpan name_space, name;
    uint32_t severity;
} DiagnosticControl;
static bool control_charge(Parser *p, uint64_t amount)
{
    UniformityBudget budget={.used=p->source->diagnostic_work,.limit=UNIFORMITY_WORK_LIMIT,
        .offset=parser_peek(p)->offset};
    const bool ok=uniformity_charge(p->source,&budget,amount);
    p->source->diagnostic_work=budget.used;
    return ok;
}
static bool read_control_args(Parser *p, DiagnosticControl *control)
{
    if (!parser_punctuation(p,'(',"(")) return false;
    if (parser_spelled(p,parser_peek(p),"off")) control->severity=DOE_COMPILER_DIAGNOSTIC_OFF;
    else if (parser_spelled(p,parser_peek(p),"error")) control->severity=DOE_COMPILER_DIAGNOSTIC_ERROR;
    else if (parser_spelled(p,parser_peek(p),"warning")) control->severity=DOE_COMPILER_DIAGNOSTIC_WARNING;
    else if (parser_spelled(p,parser_peek(p),"info")) control->severity=DOE_COMPILER_DIAGNOSTIC_INFO;
    else return parser_reject(p,"diagnostic severity off, error, warning or info");
    ++p->cursor;
    if (!parser_punctuation(p,',',",") || !parser_name_span(p,&control->name)) return false;
    if (parser_peek(p)->kind=='.') {
        ++p->cursor;
        control->name_space=control->name;
        if (!parser_name_span(p,&control->name)) return false;
    }
    if (parser_peek(p)->kind==',') ++p->cursor;
    return parser_punctuation(p,')',")");
}
static bool read_control(Parser *p, DiagnosticControl *control)
{
    const size_t start=p->cursor;
    return parser_word(p,"diagnostic") && read_control_args(p,control) &&
        parser_punctuation(p,';',";") && control_charge(p,p->cursor-start);
}
static uint32_t classify_rule(const Source *source, const DiagnosticControl *control)
{
    if (control->name_space.length) return 0u;
    if (source_span_is(source,control->name,"derivative_uniformity")) return DOE_COMPILER_RULE_DERIVATIVE_UNIFORMITY;
    if (source_span_is(source,control->name,"subgroup_uniformity")) return DOE_COMPILER_RULE_SUBGROUP_UNIFORMITY;
    return 0u;
}
static void record_unknown_rule(Source *source, const DiagnosticControl *control)
{
    if (!source->unknown_diagnostic.length) {
        const uint32_t offset=control->name_space.length?control->name_space.offset:control->name.offset;
        source->unknown_diagnostic=(NameSpan){offset,control->name.offset+control->name.length-offset};
    }
}
static bool unknown_conflicts(Parser *p, size_t end, const DiagnosticControl *control)
{
    Parser previous=*p;
    previous.cursor=0;
    while(previous.cursor<end){
        if(parser_spelled(&previous,parser_peek(&previous),"diagnostic")){
            DiagnosticControl other={0};
            if(!read_control(&previous,&other) || !control_charge(p,(uint64_t)other.name.length+other.name_space.length)) return false;
            if(source_spans_equal(p->source,other.name,control->name) &&
                source_spans_equal(p->source,other.name_space,control->name_space) && other.severity!=control->severity)
                return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,control->name.offset,"conflicting global diagnostic controls");
        } else {
            if(!control_charge(p,1)) return false;
            ++previous.cursor;
        }
    }
    return true;
}
static bool unknown_attribute_duplicates(Parser *p, size_t list_start, size_t end, const DiagnosticControl *control)
{
    Parser previous=*p;
    previous.cursor=list_start;
    while(previous.cursor<end){
        if(parser_peek(&previous)->kind=='@' && previous.cursor+1u<previous.tokens->token_count &&
            parser_spelled(&previous,&previous.tokens->tokens[previous.cursor+1u],"diagnostic")){
            previous.cursor+=2u;
            DiagnosticControl other={0};
            if(!read_control_args(&previous,&other) || !control_charge(p,(uint64_t)other.name.length+other.name_space.length)) return false;
            if(source_spans_equal(p->source,other.name,control->name) &&
                source_spans_equal(p->source,other.name_space,control->name_space))
                return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,control->name.offset,"duplicate diagnostic attribute rule");
        } else {
            if(!control_charge(p,1)) return false;
            ++previous.cursor;
        }
    }
    return true;
}
bool parser_diagnostic_directive(Parser *p)
{
    const size_t start=p->cursor;
    DiagnosticControl control={0};
    if(!read_control(p,&control)) return false;
    const uint32_t rule=classify_rule(p->source,&control);
    if(rule){
        const uint32_t mask=1u<<rule;
        if((p->source->diagnostic_seen&mask) && p->source->diagnostic_severity[rule-1u]!=control.severity)
            return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,control.name.offset,"conflicting global diagnostic controls");
        p->source->diagnostic_seen|=mask;
        p->source->diagnostic_severity[rule-1u]=control.severity;
        p->diagnostic_policy=diagnostic_policy_with(p->diagnostic_policy,rule,control.severity);
    } else {
        if(!unknown_conflicts(p,start,&control)) return false;
        record_unknown_rule(p->source,&control);
    }
    return true;
}
bool parser_diagnostic_attribute(Parser *p, size_t list_start, uint32_t *seen_mask)
{
    const size_t start=p->cursor;
    DiagnosticControl control={0};
    if(!parser_punctuation(p,'@',"@diagnostic") || !parser_word(p,"diagnostic") ||
        !read_control_args(p,&control) || !control_charge(p,p->cursor-start)) return false;
    const uint32_t rule=classify_rule(p->source,&control);
    if(rule){
        const uint32_t mask=1u<<rule;
        if((*seen_mask&mask)!=0u)
            return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,control.name.offset,"duplicate diagnostic attribute rule");
        *seen_mask|=mask;
        p->diagnostic_policy=diagnostic_policy_with(p->diagnostic_policy,rule,control.severity);
        p->source->diagnostic_seen|=DIAGNOSTIC_SCOPED;
    } else {
        if(!unknown_attribute_duplicates(p,list_start,start,&control)) return false;
        record_unknown_rule(p->source,&control);
    }
    return true;
}
bool parser_diagnostic_attributes(Parser *p)
{
    const size_t list_start=p->cursor;
    uint32_t seen=0u;
    while(parser_peek(p)->kind=='@'){
        const Token *attr=p->cursor+1u<p->tokens->token_count ? &p->tokens->tokens[p->cursor+1u] : parser_peek(p);
        if(attr->kind!=TOKEN_WORD || !parser_spelled(p,attr,"diagnostic"))
            return source_error(p->source,DOE_COMPILER_PARSE_ERROR,attr->offset,"unsupported statement attribute");
        if(!parser_diagnostic_attribute(p,list_start,&seen)) return false;
    }
    return true;
}
