/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#ifndef DOE_COMPILER_HOST_H
#define DOE_COMPILER_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOE_COMPILER_HOST_ABI_VERSION 1u

/* Allocating host companion, NOT the allocation-free doe_compiler ABI.
 * Calls are synchronous; inputs are borrowed only until return. This library
 * owns successful result bytes, which must be released with host_release, not
 * the caller's allocator (in particular across Windows CRT boundaries).
 * No implicit Zig executable, runtime provider, or GPU execution is involved.
 * The version and exact record sizes are checked before touching outputs.
 */
typedef enum DoeCompilerHostStatus {
    DOE_COMPILER_HOST_OK = 0,
    DOE_COMPILER_HOST_INVALID_ARGUMENT = 1,
    DOE_COMPILER_HOST_ABI_MISMATCH = 2,
    DOE_COMPILER_HOST_OUT_OF_MEMORY = 3,
    DOE_COMPILER_HOST_UNSUPPORTED = 4,
    DOE_COMPILER_HOST_LIMIT_EXCEEDED = 5,
    DOE_COMPILER_HOST_REJECTED = 6,
    DOE_COMPILER_HOST_COMPILE_FAILED = 7,
    DOE_COMPILER_HOST_TOOLCHAIN_UNAVAILABLE = 8,
    DOE_COMPILER_HOST_IO_ERROR = 9
} DoeCompilerHostStatus;

typedef struct DoeCompilerHostBuffer {
    uint8_t *data;
    size_t size;
} DoeCompilerHostBuffer;

/* domain: 0 host preflight, 1 TSIR status, 2 DXIL diagnostic kind.
 * code preserves the owner's precise status, not just the host classification.
 * Stage/message are owned NUL-terminated copies; messages may be truncated to
 * 255 bytes. Source line/column are zero when the owner supplied no location.
 * diagnostic may be NULL only with diagnostic_size == 0.
 */
typedef struct DoeCompilerHostDiagnostic {
    uint32_t domain;
    uint32_t code;
    uint32_t line;
    uint32_t column;
    int32_t system_error;
    char stage[48];
    char message[256];
} DoeCompilerHostDiagnostic;

/* Output must initially be {NULL, 0}; an occupied result is rejected rather
 * than leaked or implicitly destroyed. Every failed call preserves it.
 * Successful text output has an additional NUL byte not counted in size.
 * Source/input spans need not be NUL terminated; path/kernel names must be.
 * All passed records and spans must be mutually disjoint valid storage.
 */
DoeCompilerHostStatus doe_compiler_host_tsir_manifest(uint32_t abi_version,
    const char *repository_root, DoeCompilerHostBuffer *output,
    size_t output_size, DoeCompilerHostDiagnostic *diagnostic,
    size_t diagnostic_size);

/* Three native-target entries from actual typed WGSL, as a JSON array.
 * kernel is one of the bootstrap exactness contracts, not a source classifier.
 * repository_root supplies retained emitter bytes for reference identity.
 */
DoeCompilerHostStatus doe_compiler_host_tsir_source(uint32_t abi_version,
    const char *repository_root, const char *kernel, const char *source,
    size_t source_size, DoeCompilerHostBuffer *output, size_t output_size,
    DoeCompilerHostDiagnostic *diagnostic, size_t diagnostic_size);

/* Exact retained bootstrap status JSON. An unsupported input document can be
 * a successful transport carrying status=not_implemented; inspect the JSON.
 * Allocation, malformed-hex and execution failures remain failing calls.
 */
DoeCompilerHostStatus doe_compiler_host_tsir_oracle(uint32_t abi_version,
    const char *kernel, const char *inputs_json, size_t inputs_size,
    DoeCompilerHostBuffer *output, size_t output_size,
    DoeCompilerHostDiagnostic *diagnostic, size_t diagnostic_size);

/* Shared typed WGSL->HLSL owner followed by an explicitly selected real DXC.
 * executable and cache_directory are required nonempty paths; no environment
 * lookup or native-placeholder fallback. Output is validated DXIL, not device
 * execution. Unsupported source and absent DXC remain explicit failures.
 */
DoeCompilerHostStatus doe_compiler_host_dxil(uint32_t abi_version,
    const char *source, size_t source_size, const char *dxc_executable,
    const char *cache_directory, DoeCompilerHostBuffer *output,
    size_t output_size, DoeCompilerHostDiagnostic *diagnostic,
    size_t diagnostic_size);

/* Accepts NULL or a zeroed/previously released record. Other records must be
 * unmodified successful results from this library. Clears the record. */
void doe_compiler_host_release(DoeCompilerHostBuffer *output);

#ifdef __cplusplus
}
#endif
#endif
