/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Experimental standalone compiler ABI; not the production WebGPU ABI. */
#ifndef DOE_COMPILER_H
#define DOE_COMPILER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DOE_COMPILER_ABI_VERSION 1u
#define DOE_COMPILER_MAX_SOURCE_BYTES 65536u
#define DOE_COMPILER_MAX_TOKENS 2048u
#define DOE_COMPILER_MAX_NAME_BYTES 63u
#define DOE_COMPILER_MAX_BINDINGS 8u
#define DOE_COMPILER_MAX_WORDS 4096u

typedef enum {
    DOE_COMPILER_OK = 0,
    DOE_COMPILER_INPUT_ERROR = 1,
    DOE_COMPILER_PARSE_ERROR = 2,
    DOE_COMPILER_UNSUPPORTED = 3,
    DOE_COMPILER_SEMANTIC_ERROR = 4,
    DOE_COMPILER_OUTPUT_TOO_SMALL = 5,
    DOE_COMPILER_ABI_MISMATCH = 6
} DoeCompilerStatus;
typedef enum { DOE_COMPILER_U32 = 1, DOE_COMPILER_F32 = 2 } DoeCompilerScalar;
typedef enum { DOE_COMPILER_READ = 1, DOE_COMPILER_READ_WRITE = 2 } DoeCompilerAccess;
typedef struct {
    char name[64];
    uint32_t group, binding;
    DoeCompilerScalar scalar;
    DoeCompilerAccess access;
    uint32_t element_stride, runtime_sized;
} DoeCompilerBinding;
typedef struct {
    char entry_point[64];
    uint32_t workgroup_size[3];
    uint32_t binding_count;
    DoeCompilerBinding bindings[DOE_COMPILER_MAX_BINDINGS];
} DoeCompilerReflection;
typedef struct {
    DoeCompilerStatus status;
    size_t offset;
    uint32_t line, column;
    char message[192];
} DoeCompilerDiagnostic;
typedef struct {
    size_t word_count;
    DoeCompilerReflection reflection;
    DoeCompilerDiagnostic diagnostic;
} DoeCompilerResult;
/* Caller supplies disjoint valid buffers, result_size bytes for result and
 * word_capacity native uint32_t slots. Source need not have a terminating NUL.
 * NULL source/words are admitted only with zero respective length/capacity.
 * ABI/version or result-size mismatch returns ABI_MISMATCH without touching
 * result or words. NULL result returns INPUT_ERROR. Otherwise failure zeroes
 * word_count/reflection, reports an original-source diagnostic and leaves words
 * untouched. No pointers are retained. Calls are independent and thread-safe.
 * CLI serializes native output words explicitly as little-endian SPIR-V. */
DoeCompilerStatus doe_compiler_compile(
    uint32_t abi_version, const char *source, size_t source_length,
    uint32_t *words, size_t word_capacity,
    DoeCompilerResult *result, size_t result_size);
#ifdef __cplusplus
}
#endif
#endif
