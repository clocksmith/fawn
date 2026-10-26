// SPDX-License-Identifier: MIT
// CSL target constants, limits and structural validation, mirroring
// csl_spec.zig (Cerebras SDK 2.10.0 reference in that file).
#ifndef CSL_SPEC_H
#define CSL_SPEC_H

#include <stddef.h>
#include <stdint.h>

#define CSL_MAX_COLORS ((uint8_t)24)
#define CSL_MEMCPY_RESERVED_COLORS ((uint8_t)4)
#define CSL_APP_COLORS ((uint8_t)20)
#define CSL_PE_SRAM_BYTES ((size_t)49152)
#define CSL_MAX_RECT_DIM ((uint16_t)512)
#define CSL_HOST_PLAN_SCHEMA_VERSION ((uint32_t)3)
#define CSL_SIMULATOR_PLAN_SCHEMA_VERSION ((uint32_t)2)
#define CSL_SIMULATOR_RESULT_SCHEMA_VERSION ((uint32_t)1)
#define CSL_MEMORY_PLAN_SCHEMA_VERSION ((uint32_t)1)
#define CSL_RUNTIME_CONFIG_SCHEMA_VERSION ((uint32_t)1)
#define CSL_Q4K_BLOCK_SIZE ((uint32_t)256)
#define CSL_Q4K_BLOCK_BYTES ((uint32_t)144)
#define CSL_Q4K_SUB_BLOCKS ((uint32_t)8)
#define CSL_Q4K_SUB_BLOCK_SIZE ((uint32_t)32)
#define CSL_Q8_0_BLOCK_SIZE ((uint32_t)32)
#define CSL_Q8_0_BLOCK_BYTES ((uint32_t)34)

extern const char *const CSL_SECTION_SEPARATOR;
extern const char *const CSL_SECTION_SEPARATOR_END;
extern const char *const CSL_LAYOUT_FILENAME;
extern const char *const CSL_PE_PROGRAM_FILENAME;
extern const char *const CSL_HOST_PLAN_ARTIFACT_KIND;
extern const char *const CSL_HOST_PLAN_TARGET;
extern const char *const CSL_HOST_PLAN_CONTRACT;
extern const char *const CSL_HOST_PLAN_DISCOVERY_EXPLICIT_CONFIG;
extern const char *const CSL_HOST_PLAN_DISCOVERY_IMPLICIT_PATH_LOOKUP;
extern const char *const CSL_CSLC_SDK_MIN_VERSION;
extern const char *const CSL_CSLC_VERSION_ARG;
extern const char *const CSL_SIMULATOR_PLAN_ARTIFACT_KIND;
extern const char *const CSL_SIMULATOR_PLAN_TARGET;
extern const char *const CSL_SIMULATOR_PLAN_CONTRACT;
extern const char *const CSL_SIMULATOR_DRIVER_PROTOCOL;
extern const char *const CSL_SIMULATOR_DRIVER_ENV_VAR;
extern const char *const CSL_SIMULATOR_RESULT_ARTIFACT_KIND;
extern const char *const CSL_SIMULATOR_RESULT_TARGET;
extern const char *const CSL_SIMULATOR_RESULT_CONTRACT;
extern const char *const CSL_MEMORY_PLAN_ARTIFACT_KIND;
extern const char *const CSL_MEMORY_PLAN_TARGET;
extern const char *const CSL_MEMORY_PLAN_CONTRACT;
extern const char *const CSL_RUNTIME_CONFIG_ARTIFACT_KIND;
extern const char *const CSL_RUNTIME_CONFIG_TARGET;
extern const char *const CSL_RUNTIME_CONFIG_CONTRACT;
extern const char *const CSL_FABIN_DSD;
extern const char *const CSL_FABOUT_DSD;
extern const char *const CSL_MEM1D_DSD;
extern const char *const CSL_MEM4D_DSD;

typedef enum {
    CSL_DIR_NORTH,
    CSL_DIR_SOUTH,
    CSL_DIR_EAST,
    CSL_DIR_WEST,
    CSL_DIR_RAMP
} CslDirection;

typedef enum {
    CSL_ARCH_WSE2,
    CSL_ARCH_WSE3
} CslArch;

typedef enum {
    CSL_SCALAR_VOID,
    CSL_SCALAR_BOOL,
    CSL_SCALAR_I32,
    CSL_SCALAR_ABSTRACT_INT,
    CSL_SCALAR_U32,
    CSL_SCALAR_F32,
    CSL_SCALAR_ABSTRACT_FLOAT,
    CSL_SCALAR_F16
} CslScalar;

const char *csl_direction_name(CslDirection dir);
const char *csl_arch_name(CslArch arch);
const char *csl_scalar_type_name(CslScalar scalar);
/* NULL when valid, otherwise a static message, exactly as in Zig. */
const char *csl_validate_q4k_buffer_size(uint64_t byte_count);
const char *csl_validate_q8_0_buffer_size(uint64_t byte_count);
int csl_q4k_element_count(uint64_t byte_count, uint64_t *out);
int csl_q8_0_element_count(uint64_t byte_count, uint64_t *out);
/* Structural validation of emitted CSL text. NULL when valid. */
const char *csl_validate_output(const char *csl);

#endif
