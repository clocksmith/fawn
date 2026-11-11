/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_DIAGNOSTICS_INTERNAL_H
#define DOE_COMPILER_DIAGNOSTICS_INTERNAL_H
#include "internal.h"
#include "../include/doe_compiler_diagnostics.h"

static inline unsigned char diagnostic_policy_pack(uint32_t derivative, uint32_t subgroup)
{
    return (unsigned char)((derivative & 3u) | ((subgroup & 3u) << 2u));
}
static inline unsigned char diagnostic_policy_with(unsigned char policy, uint32_t rule, uint32_t severity)
{
    const unsigned shift = 2u * (rule - 1u);
    return (unsigned char)((policy & (unsigned char)~(3u << shift)) | ((severity & 3u) << shift));
}
static inline uint32_t expression_diagnostic_severity(const Expression *expression, uint32_t rule)
{
    return ((uint32_t)expression->diagnostic_policy >> (2u * (rule - 1u))) & 3u;
}
size_t compiler_notice_count(const Source *);
bool compiler_uniformity_notice(Source *, uint32_t expression, uint32_t rule);
void compiler_notices_publish(const Source *, DoeCompilerNoticeV18 *);
#endif
