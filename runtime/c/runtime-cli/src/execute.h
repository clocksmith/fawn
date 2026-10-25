/* SPDX-License-Identifier: MIT
 * Executes typed runtime commands through the public WebGPU C ABI of the C
 * runtime (libwebgpu_doe_c). One device, one queue; resources are keyed by the
 * stream's buffer handles and live until close. Each command completes (queue
 * work done) before the next starts. */
#ifndef DOE_RUNTIME_EXECUTE_H
#define DOE_RUNTIME_EXECUTE_H
#include "stream.h"
#include "trace.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum doe_adapter_class {
    DOE_ADAPTER_AUTO,     /* A nonfallback adapter, else the fallback (CPU) adapter. */
    DOE_ADAPTER_PHYSICAL, /* A nonfallback adapter or failure. */
    DOE_ADAPTER_SOFTWARE  /* The fallback adapter or failure. */
} doe_adapter_class;

typedef struct doe_runtime_options {
    doe_adapter_class adapter;
    const char *kernel_root; /* NULL: kernels resolve from the working directory only. */
    bool upload_copy_src;    /* Upload buffers use CopyDst|CopySrc, else CopyDst. */
    uint64_t upload_submit_every; /* Uploads complete every N uploads; nonzero. */
} doe_runtime_options;

typedef struct doe_runtime_adapter {
    char vendor[128], architecture[128], device[256], description[256];
    uint32_t vendor_id, device_id;
    const char *adapter_type; /* "discrete_gpu", "integrated_gpu", "cpu" or "unknown" */
    const char *backend;      /* "vulkan", "metal", "d3d12" or "unknown" */
} doe_runtime_adapter;

typedef struct doe_runtime doe_runtime;

/* NULL on failure, with the reason in message. */
doe_runtime *doe_runtime_open(const doe_runtime_options *options, char *message, size_t message_size);
const doe_runtime_adapter *doe_runtime_adapter_info(const doe_runtime *runtime);
/* Executes one command and fills `execution`; false when it failed (the
 * execution record then carries the error). */
bool doe_runtime_execute(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
/* Runs the command's isolated output oracle and writes the read-back SHA-256;
 * false when the oracle could not run (a digest mismatch is the caller's). */
bool doe_runtime_validate_oracle(doe_runtime *runtime, const doe_command *command, const doe_output_oracle *oracle,
    char actual_sha256[65], doe_trace_execution *execution);
void doe_runtime_close(doe_runtime *runtime);
uint64_t doe_runtime_monotonic_ns(void);
#endif
