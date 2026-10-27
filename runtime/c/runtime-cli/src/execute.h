/* SPDX-License-Identifier: MIT
 * Executes typed runtime commands through the public WebGPU C ABI of the C
 * runtime (libwebgpu_doe_c). One device, one queue; resources are keyed by the
 * stream's buffer handles and live until close. The selected sync mode waits per command or defers
 * completion to an explicit flush; barriers and host reads always wait. */
#ifndef DOE_RUNTIME_EXECUTE_H
#define DOE_RUNTIME_EXECUTE_H
#include "stream.h"
#include "doe_native_device_policy.h"
#include "trace.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum doe_adapter_class {
    DOE_ADAPTER_AUTO,     /* A nonfallback adapter, else the fallback (CPU) adapter. */
    DOE_ADAPTER_PHYSICAL, /* A nonfallback adapter or failure. */
    DOE_ADAPTER_SOFTWARE  /* The fallback adapter or failure. */
} doe_adapter_class;

typedef enum doe_queue_wait { DOE_WAIT_PROCESS_EVENTS, DOE_WAIT_ANY } doe_queue_wait;

typedef enum doe_timestamp_mode { DOE_TIMESTAMP_AUTO, DOE_TIMESTAMP_OFF, DOE_TIMESTAMP_REQUIRE } doe_timestamp_mode;

typedef struct doe_runtime_options {
    const char *backend_lane;
    doe_timestamp_mode timestamp_mode;
    bool no_pipeline_cache;
    const char *pipeline_cache_dir; /* Explicit path; no implicit persistence or directory creation. */
    doe_adapter_class adapter;
    doe_queue_wait queue_wait;
    uint64_t queue_wait_timeout_ns; /* 0: runtime default bound. */
    bool defer_queue_sync;
    bool public_webgpu_renderer; /* Explicit delegate lane; native proxy remains default. */
    const char *kernel_root; /* NULL: kernels resolve from the working directory only. */
    bool upload_copy_src;    /* Upload buffers use CopyDst|CopySrc, else CopyDst. */
    uint64_t upload_submit_every; /* Uploads complete every N uploads; nonzero. */
} doe_runtime_options;

typedef struct doe_runtime_adapter {
    char vendor[128], architecture[128], device[256], description[256];
    uint32_t vendor_id, device_id;
    uint32_t driver_version; /* The driver's raw version (Vulkan driverVersion), 0 when not reported. */
    const char *adapter_type; /* "discrete_gpu", "integrated_gpu", "cpu" or "unknown" */
    const char *backend;      /* "vulkan", "metal", "d3d12" or "unknown" */
} doe_runtime_adapter;

typedef struct doe_runtime doe_runtime;
bool doe_runtime_load_device_policy(const char *lane_name, DoeNativeDevicePolicy *policy,
    char *message, size_t capacity, char source_hash[65], WGPUBackendType *backend_type);

/* NULL on failure, with the reason in message. */
doe_runtime *doe_runtime_open(const doe_runtime_options *options, char *message, size_t message_size);
const doe_runtime_adapter *doe_runtime_adapter_info(const doe_runtime *runtime);
/* Embedded public-render source only when its pipeline was compiled in this run. */
const char *doe_runtime_public_render_source(const doe_runtime *runtime);
/* Executes one command and fills `execution`; false when it failed (the
 * execution record then carries the error). */
bool doe_runtime_execute(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
/* Runs the command's isolated output oracle and writes the read-back SHA-256;
 * false when the oracle could not run (a digest mismatch is the caller's). */
bool doe_runtime_validate_oracle(doe_runtime *runtime, const doe_command *command, const doe_output_oracle *oracle,
    char actual_sha256[65], doe_trace_execution *execution);
/* Compiles and caches a kernel_dispatch command's pipeline before the timed
 * command loop, as doe-zig-runtime's kernel prewarm does; other kinds do
 * nothing. False with the reason in `execution`; the command then reports the
 * same failure when it runs. */
bool doe_runtime_prewarm_kernel(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution);
/* Reads an explicit range, or the whole buffer, into a new allocation the caller
 * frees; false when the handle/range is invalid or the readback failed. */
bool doe_runtime_read_buffer_range(doe_runtime *runtime, uint64_t handle, uint64_t offset, uint64_t size,
    uint8_t **data, doe_trace_execution *execution);
bool doe_runtime_read_buffer(doe_runtime *runtime, uint64_t handle, uint8_t **data, size_t *size,
    doe_trace_execution *execution);
/* Immutable file-backed source identities actually compiled during this run,
 * in first-construction order. Distinct bytes at one path yield distinct records.
 * Borrowed strings remain valid until runtime close; no filesystem reads. */
typedef struct doe_runtime_source_receipt {
    const char *module_id, *path, *entry_point, *sha256;
    size_t byte_length;
} doe_runtime_source_receipt;
size_t doe_runtime_source_receipt_count(const doe_runtime *runtime);
bool doe_runtime_source_receipt_at(const doe_runtime *runtime, size_t index, doe_runtime_source_receipt *out);
const char *doe_runtime_diagnostic_source(bool compute);
bool doe_runtime_used_diagnostic_source(const doe_runtime *runtime, bool compute);
/* Completes outstanding queue work; elapsed time belongs to the final trace row. */
bool doe_runtime_flush(doe_runtime *runtime, doe_trace_execution *execution);
/* Optional archive I/O never overwrites a command's first error. */
void doe_runtime_flush_pipeline_cache(doe_runtime *runtime);
bool doe_runtime_write_pipeline_cache(FILE *out, const doe_runtime *runtime);
bool doe_runtime_write_device_policy(FILE *out, const doe_runtime *runtime);
/* False retains the runtime and its handles because device-loss quiescence was
 * not proved. Commands refuse after closing starts; the owner may retry close
 * after completion is established. A true result consumes the runtime. */
/* Callback retirement alone does not prove submitted GPU work completed. */
bool doe_runtime_gpu_completion_unknown(const doe_runtime *runtime);
bool doe_runtime_close(doe_runtime *runtime);
uint64_t doe_runtime_monotonic_ns(void);
#endif
