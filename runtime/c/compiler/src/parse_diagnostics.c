/* SPDX-License-Identifier: MIT
 * Global diagnostic controls use the ordinary token parser and source spans. */
#include "parse_internal.h"
#include "sema.h"
#include "../include/doe_compiler_diagnostics.h"

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
static bool read_control(Parser *p, DiagnosticControl *control)
{
    const size_t start=p->cursor;
    if (!parser_word(p,"diagnostic") || !parser_punctuation(p,'(',"(")) return false;
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
    return parser_punctuation(p,')',")") && parser_punctuation(p,';',";") && control_charge(p,p->cursor-start);
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
bool parser_diagnostic_directive(Parser *p)
{
    const size_t start=p->cursor;
    DiagnosticControl control={0};
    if(!read_control(p,&control)) return false;
    uint32_t rule=0;
    if(!control.name_space.length){
        if(source_span_is(p->source,control.name,"derivative_uniformity")) rule=DOE_COMPILER_RULE_DERIVATIVE_UNIFORMITY;
        else if(source_span_is(p->source,control.name,"subgroup_uniformity")) rule=DOE_COMPILER_RULE_SUBGROUP_UNIFORMITY;
    }
    if(rule){
        const uint32_t mask=1u<<rule;
        if((p->source->diagnostic_seen&mask) && p->source->diagnostic_severity[rule-1u]!=control.severity)
            return source_error(p->source,DOE_COMPILER_SEMANTIC_ERROR,control.name.offset,"conflicting global diagnostic controls");
        p->source->diagnostic_seen|=mask;
        p->source->diagnostic_severity[rule-1u]=control.severity;
    } else {
        if(!unknown_conflicts(p,start,&control)) return false;
        if(!p->source->unknown_diagnostic.length){
            const uint32_t offset=control.name_space.length?control.name_space.offset:control.name.offset;
            p->source->unknown_diagnostic=(NameSpan){offset,control.name.offset+control.name.length-offset};
        }
    }
    return true;
}
