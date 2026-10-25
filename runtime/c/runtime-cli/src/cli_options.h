/* SPDX-License-Identifier: MIT */
#ifndef DOE_C_CLI_OPTIONS_H
#define DOE_C_CLI_OPTIONS_H
#include "execute.h"
#include "quirks.h"
/* The trace identity names the webgpu.h provider this binary links: the C
 * library by default, Dawn for the dawn-c-runtime build. */
#ifndef DOE_RUNTIME_MODULE
#define DOE_RUNTIME_MODULE "doe-c-runtime"
#endif
#ifdef __APPLE__
#define DOE_RUNTIME_API "metal"
#ifndef DOE_RUNTIME_BACKEND_ID
#define DOE_RUNTIME_BACKEND_ID "doe_c_metal"
#endif
#else
#define DOE_RUNTIME_API "vulkan"
#ifndef DOE_RUNTIME_BACKEND_ID
#define DOE_RUNTIME_BACKEND_ID "doe_c_vulkan"
#endif
#endif
#define MODULE_NAME DOE_RUNTIME_MODULE
#define BACKEND_ID DOE_RUNTIME_BACKEND_ID
enum { MAX_READBACKS = 8 };

typedef struct options {
    const char *commands, *kernel_root, *trace_jsonl, *trace_meta, *quirks, *numeric_policy, *numeric_profile;
    const char *plan, *workload, *replay; /* Plan mode: doe-plan-executor's --plan and --workload. */
    const char *vendor, *api, *family, *driver;
    bool execute, trace_stdout, validate_oracles, trace_backend;
    doe_quirk_mode quirk_mode;
    doe_profile profile; /* The quirk matching profile, with doe-zig-runtime's defaults. */
    uint64_t command_repeat;
    doe_runtime_options runtime;
    /* --readback-buffer HANDLE=PATH: final buffer contents written after a successful run. */
    uint64_t readback_handles[MAX_READBACKS];
    const char *readback_paths[MAX_READBACKS];
    size_t readback_count;
} options;

int parse_options(int argc, char **argv, options *out);
#endif
