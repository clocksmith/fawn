// SPDX-License-Identifier: MIT
// CSL target constants and structural validation, mirroring csl_spec.zig.
#include "csl_spec.h"

#include <string.h>

const char *const CSL_SECTION_SEPARATOR = "\n//--- ";
const char *const CSL_SECTION_SEPARATOR_END = " ---\n";
const char *const CSL_LAYOUT_FILENAME = "layout.csl";
const char *const CSL_PE_PROGRAM_FILENAME = "pe_program.csl";
const char *const CSL_HOST_PLAN_ARTIFACT_KIND = "csl_host_plan";
const char *const CSL_HOST_PLAN_TARGET = "wse3";
const char *const CSL_HOST_PLAN_CONTRACT = "explicit_host_plan";
const char *const CSL_HOST_PLAN_DISCOVERY_EXPLICIT_CONFIG = "explicit_config";
const char *const CSL_HOST_PLAN_DISCOVERY_IMPLICIT_PATH_LOOKUP = "implicit_path_lookup";
const char *const CSL_CSLC_SDK_MIN_VERSION = "2.10.0";
const char *const CSL_CSLC_VERSION_ARG = "--version";
const char *const CSL_SIMULATOR_PLAN_ARTIFACT_KIND = "csl_simulator_plan";
const char *const CSL_SIMULATOR_PLAN_TARGET = "wse3";
const char *const CSL_SIMULATOR_PLAN_CONTRACT = "explicit_simulator_launch";
const char *const CSL_SIMULATOR_DRIVER_PROTOCOL = "doe.csl.simulator/v1";
const char *const CSL_SIMULATOR_DRIVER_ENV_VAR = "DOE_CSL_SIM_EXECUTABLE";
const char *const CSL_SIMULATOR_RESULT_ARTIFACT_KIND = "csl_simulator_result";
const char *const CSL_SIMULATOR_RESULT_TARGET = "wse3";
const char *const CSL_SIMULATOR_RESULT_CONTRACT = "explicit_simulator_outcome";
const char *const CSL_MEMORY_PLAN_ARTIFACT_KIND = "csl_memory_plan";
const char *const CSL_MEMORY_PLAN_TARGET = "wse3";
const char *const CSL_MEMORY_PLAN_CONTRACT = "explicit_memory_plan";
const char *const CSL_RUNTIME_CONFIG_ARTIFACT_KIND = "csl_runtime_config";
const char *const CSL_RUNTIME_CONFIG_TARGET = "wse3";
const char *const CSL_RUNTIME_CONFIG_CONTRACT = "explicit_runtime_config";
const char *const CSL_FABIN_DSD = "fabin_dsd";
const char *const CSL_FABOUT_DSD = "fabout_dsd";
const char *const CSL_MEM1D_DSD = "mem1d_dsd";
const char *const CSL_MEM4D_DSD = "mem4d_dsd";

const char *csl_direction_name(CslDirection dir) {
    switch (dir) {
        case CSL_DIR_NORTH: return "NORTH";
        case CSL_DIR_SOUTH: return "SOUTH";
        case CSL_DIR_EAST: return "EAST";
        case CSL_DIR_WEST: return "WEST";
        case CSL_DIR_RAMP: return "RAMP";
        default: return NULL;
    }
}

const char *csl_arch_name(CslArch arch) {
    switch (arch) {
        case CSL_ARCH_WSE2: return "wse2";
        case CSL_ARCH_WSE3: return "wse3";
        default: return NULL;
    }
}

const char *csl_scalar_type_name(CslScalar scalar) {
    switch (scalar) {
        case CSL_SCALAR_VOID: return "void";
        case CSL_SCALAR_BOOL: return "bool";
        case CSL_SCALAR_I32:
        case CSL_SCALAR_ABSTRACT_INT: return "i32";
        case CSL_SCALAR_U32: return "u32";
        case CSL_SCALAR_F32:
        case CSL_SCALAR_ABSTRACT_FLOAT: return "f32";
        case CSL_SCALAR_F16: return "f16";
        default: return NULL;
    }
}

const char *csl_validate_q4k_buffer_size(uint64_t byte_count) {
    if (byte_count == 0) {
        return "Q4K buffer size is zero";
    }
    if (byte_count % CSL_Q4K_BLOCK_BYTES != 0) {
        return "Q4K buffer size not a multiple of block size (144)";
    }
    return NULL;
}

const char *csl_validate_q8_0_buffer_size(uint64_t byte_count) {
    if (byte_count == 0) {
        return "Q8_0 buffer size is zero";
    }
    if (byte_count % CSL_Q8_0_BLOCK_BYTES != 0) {
        return "Q8_0 buffer size not a multiple of block size (34)";
    }
    return NULL;
}

int csl_q4k_element_count(uint64_t byte_count, uint64_t *out) {
    if (byte_count % CSL_Q4K_BLOCK_BYTES != 0 || out == NULL) {
        return 0;
    }
    *out = (byte_count / CSL_Q4K_BLOCK_BYTES) * CSL_Q4K_BLOCK_SIZE;
    return 1;
}

int csl_q8_0_element_count(uint64_t byte_count, uint64_t *out) {
    if (byte_count % CSL_Q8_0_BLOCK_BYTES != 0 || out == NULL) {
        return 0;
    }
    *out = (byte_count / CSL_Q8_0_BLOCK_BYTES) * CSL_Q8_0_BLOCK_SIZE;
    return 1;
}

const char *csl_validate_output(const char *csl) {
    static const struct { const char *marker; const char *message; } required[] = {
        { "layout.csl", "missing layout.csl section" },
        { "pe_program.csl", "missing pe_program.csl section" },
        { "@set_rectangle", "layout missing @set_rectangle" },
        { "<memcpy/memcpy>", "PE program missing memcpy import" },
        { "@export_symbol(compute)", "PE program missing @export_symbol(compute)" },
    };
    size_t i;
    long depth = 0;
    const char *layout_pos;
    const char *pe_pos;
    if (csl == NULL) {
        return "missing layout.csl section";
    }
    for (i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (strstr(csl, required[i].marker) == NULL) {
            return required[i].message;
        }
    }
    if (strstr(csl, "unblock_cmd_stream") == NULL &&
        strstr(csl, "exit_task") == NULL) {
        return "PE program missing completion signal (unblock_cmd_stream or exit_task)";
    }
    for (i = 0; csl[i] != '\0'; i++) {
        if (csl[i] == '{') {
            depth += 1;
        } else if (csl[i] == '}') {
            depth -= 1;
            if (depth < 0) {
                return "unbalanced closing brace";
            }
        }
    }
    if (depth != 0) {
        return "unbalanced braces";
    }
    layout_pos = strstr(csl, "layout.csl");
    pe_pos = strstr(csl, "pe_program.csl");
    if (pe_pos <= layout_pos) {
        return "layout.csl must precede pe_program.csl";
    }
    return NULL;
}
