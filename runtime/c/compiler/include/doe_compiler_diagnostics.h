/* SPDX-License-Identifier: MIT
 * Additive nonfatal module diagnostics; ABI1-17 layouts remain unchanged. */
#ifndef DOE_COMPILER_DIAGNOSTICS_H
#define DOE_COMPILER_DIAGNOSTICS_H
#include "doe_compiler_specialization.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_DIAGNOSTICS_ABI_VERSION 18u
#define DOE_COMPILER_DIAGNOSTIC_OFF 0u
#define DOE_COMPILER_DIAGNOSTIC_ERROR 1u
#define DOE_COMPILER_DIAGNOSTIC_WARNING 2u
#define DOE_COMPILER_DIAGNOSTIC_INFO 3u
#define DOE_COMPILER_RULE_UNRECOGNIZED 0u
#define DOE_COMPILER_RULE_DERIVATIVE_UNIFORMITY 1u
#define DOE_COMPILER_RULE_SUBGROUP_UNIFORMITY 2u

typedef struct {
    uint32_t severity, rule, offset, length, line, column;
    char message[192];
} DoeCompilerNoticeV18;
typedef struct {
    DoeCompilerValidationOutputs metadata;
    DoeCompilerNoticeV18 *notices;
    size_t notice_capacity, notice_record_size;
} DoeCompilerDiagnosticOutputsV18;
typedef struct {
    DoeCompilerValidationResult validation;
    size_t required_notices, published_notices;
} DoeCompilerDiagnosticResultV18;

/* Same workspace query and whole-module semantics as ABI13 validation.
 * NULL outputs measures metadata and notices. Every count is exact on successful
 * validation; any undersized output leaves ALL bulk outputs untouched. Fatal
 * diagnostics remain in validation.diagnostic; notices contain warning/info.
 * Each notice owns its text and source location; no borrowed or retained pointers.
 * Unknown rules produce one warning at the first unrecognized rule per module.
 * Old interfaces apply source policy but cannot expose nonfatal records.
 * Exact descriptor sizes, caller-owned aligned/disjoint spans, errno/FP state,
 * failure publication and source immutability follow ABI13.
 */
DoeCompilerStatus doe_compiler_validate_module_diagnostics(
    uint32_t abi_version, const DoeCompilerSpecializationCapacities *, size_t capacities_size,
    void *workspace, size_t workspace_size, const char *source, size_t source_length,
    uint32_t semantic_flags, const DoeCompilerDiagnosticOutputsV18 *, size_t outputs_size,
    DoeCompilerDiagnosticResultV18 *, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
