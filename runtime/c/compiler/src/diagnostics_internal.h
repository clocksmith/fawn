/* SPDX-License-Identifier: MIT */
#ifndef DOE_COMPILER_DIAGNOSTICS_INTERNAL_H
#define DOE_COMPILER_DIAGNOSTICS_INTERNAL_H
#include "internal.h"
#include "../include/doe_compiler_diagnostics.h"
size_t compiler_notice_count(const Source *);
bool compiler_uniformity_notice(Source *, uint32_t expression, uint32_t rule);
void compiler_notices_publish(const Source *, DoeCompilerNoticeV18 *);
#endif
