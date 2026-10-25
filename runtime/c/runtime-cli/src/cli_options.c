/* SPDX-License-Identifier: MIT */
#include "cli_options.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_io.h"
static bool parse_u64(const char *text, uint64_t *out);

static int parse_readback(const char *value, options *out)
{
    const char *equals = value ? strchr(value, '=') : NULL;
    char handle[32];
    size_t length = equals ? (size_t)(equals - value) : 0;
    if (!equals || !length || length >= sizeof(handle) || !equals[1]) return 0;
    memcpy(handle, value, length);
    handle[length] = '\0';
    if (out->readback_count == MAX_READBACKS || !parse_u64(handle, &out->readback_handles[out->readback_count])) return 0;
    out->readback_paths[out->readback_count++] = equals + 1;
    return 1;
}

static void usage(FILE *out)
{
    fputs("doe-c-runtime [--commands <path>] [--execute] [--backend native|trace] [--kernel-root <path>]\n"
          "  [--trace] [--emit-normalized] [--trace-jsonl <path>] [--trace-meta <path>] [--replay <path>] [--command-repeat N]\n"
          "  [--vendor X] [--api " DOE_RUNTIME_API "] [--family X] [--driver X.Y.Z]\n"
          "  [--quirks <path>] [--quirk-mode off|trace|active] [--queue-sync-mode per-command|deferred]\n"
          "  [--queue-wait-mode process-events|wait-any] [--webgpu-ffi-queue-wait-timeout-ns N]\n"
          "  [--upload-buffer-usage copy-dst-copy-src|copy-dst] [--upload-submit-every N]\n"
          "  [--gpu-timestamp-mode off|auto|require] [--adapter auto|physical|software]\n"
          "  [--backend-lane <compiled Dawn delegate lane>]\n"
          "  [--validate-output-oracles]  (schema 1 sha256_exact_v1 isolated-dispatch oracles)\n"
          "  [--readback-buffer HANDLE=PATH]...  (final buffer bytes after a successful run; at most 8)\n"
          "  [--numeric-stability-policy <path>] [--numeric-stability-execution-profile <id>]\n"
          "doe-c-runtime --plan <plan.json> --workload <id> --trace-jsonl <path> --trace-meta <path>\n"
          "  [--kernel-root <path>] [profile, upload and --readback-buffer arguments as above]\n"
          "  (plan mode, as doe-plan-executor: buffer_write, buffer_load and kernel_dispatch plan steps)\n"
          "Commands: upload, buffer_write, barrier, kernel_dispatch (WGSL kernels, buffer and 2D texture\n"
          "bindings), copies in every direction, texture_write, texture_query, texture_destroy (2D\n"
          "uncompressed color textures and their mip levels), render_draw, render_pass, draw_indirect and\n"
          "draw_indexed_indirect (the default proxy pipeline),\n"
          "sampler_create, sampler_destroy, map_async, async_diagnostics and surface lifecycle commands. kernel_dispatch semantic fields and captures write operator\n"
          "records; with a trace meta, supported numeric annotations and capture plans are evaluated;\n"
          "unknown numeric stability capture plans are refused.\n"
          "Without --execute, only host traces are produced. Omitted commands/quirks use retained embedded samples.\n"
          "Every other command, field or argument is rejected. --adapter is this runtime's own;\n"
          "auto takes a nonfallback adapter, else the fallback (CPU) adapter, and the trace meta\n"
          "records which one ran.\n", out);
}

static bool parse_u64(const char *text, uint64_t *out)
{
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || *end) return false;
    *out = value;
    return true;
}

static int reject_argument(const char *argument, const char *why)
{
    fprintf(stderr, "doe-c-runtime: %s: %s\n", argument, why);
    return 2;
}

int parse_options(int argc, char **argv, options *out)
{
    *out = (options){.command_repeat = 1, .runtime = {.adapter = DOE_ADAPTER_AUTO, .upload_copy_src = true,
        .upload_submit_every = 1}};
    const char *quirk_mode = NULL;
    for (int i = 1; i < argc; i++) {
        const char *flag = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
#define TAKE(field) do { if (!value) return reject_argument(flag, "needs a value"); out->field = value; i++; } while (0)
        if (!strcmp(flag, "--help") || !strcmp(flag, "-h")) { usage(stdout); return 1; }
        else if (!strcmp(flag, "--commands")) TAKE(commands);
        else if (!strcmp(flag, "--plan")) TAKE(plan);
        else if (!strcmp(flag, "--workload")) TAKE(workload);
        else if (!strcmp(flag, "--backend-lane")) TAKE(backend_lane);
        else if (!strcmp(flag, "--kernel-root")) TAKE(runtime.kernel_root);
        else if (!strcmp(flag, "--trace-jsonl")) TAKE(trace_jsonl);
        else if (!strcmp(flag, "--trace-meta")) TAKE(trace_meta);
        else if (!strcmp(flag, "--replay")) TAKE(replay);
        else if (!strcmp(flag, "--quirks")) TAKE(quirks);
        else if (!strcmp(flag, "--numeric-stability-policy")) TAKE(numeric_policy);
        else if (!strcmp(flag, "--numeric-stability-execution-profile")) TAKE(numeric_profile);
        else if (!strcmp(flag, "--vendor")) TAKE(vendor);
        else if (!strcmp(flag, "--api")) TAKE(api);
        else if (!strcmp(flag, "--family")) TAKE(family);
        else if (!strcmp(flag, "--driver")) TAKE(driver);
        else if (!strcmp(flag, "--execute")) out->execute = true;
        else if (!strcmp(flag, "--trace")) out->trace_stdout = true;
        else if (!strcmp(flag, "--emit-normalized")) out->emit_normalized = true;
        else if (!strcmp(flag, "--validate-output-oracles")) out->validate_oracles = true;
        else if (!strcmp(flag, "--quirk-mode")) { if (!value) return reject_argument(flag, "needs a value"); quirk_mode = value; i++; }
        else if (!strcmp(flag, "--backend")) {
            if (value && !strcmp(value, "trace")) out->trace_backend = true;
            else if (value && !strcmp(value, "native")) out->trace_backend = false;
            else return reject_argument(flag, "must be native or trace");
            i++;
        } else if (!strcmp(flag, "--queue-sync-mode")) {
            if (value && !strcmp(value, "per-command")) out->runtime.defer_queue_sync = false;
            else if (value && !strcmp(value, "deferred")) out->runtime.defer_queue_sync = true;
            else return reject_argument(flag, "must be per-command or deferred");
            i++;
        } else if (!strcmp(flag, "--queue-wait-mode")) {
            if (value && !strcmp(value, "process-events")) out->runtime.queue_wait = DOE_WAIT_PROCESS_EVENTS;
            else if (value && !strcmp(value, "wait-any")) out->runtime.queue_wait = DOE_WAIT_ANY;
            else return reject_argument(flag, "must be process-events or wait-any");
            i++;
        } else if (!strcmp(flag, "--webgpu-ffi-queue-wait-timeout-ns")) {
            if (!parse_u64(value, &out->runtime.queue_wait_timeout_ns) || !out->runtime.queue_wait_timeout_ns)
                return reject_argument(flag, "must be a positive integer");
            i++;
        } else if (!strcmp(flag, "--upload-buffer-usage")) {
            if (value && !strcmp(value, "copy-dst-copy-src")) out->runtime.upload_copy_src = true;
            else if (value && !strcmp(value, "copy-dst")) out->runtime.upload_copy_src = false;
            else return reject_argument(flag, "must be copy-dst-copy-src or copy-dst");
            i++;
        } else if (!strcmp(flag, "--upload-submit-every")) {
            if (!parse_u64(value, &out->runtime.upload_submit_every) || !out->runtime.upload_submit_every)
                return reject_argument(flag, "must be a positive integer");
            i++;
        } else if (!strcmp(flag, "--readback-buffer")) {
            if (!parse_readback(value, out)) return reject_argument(flag, "must be HANDLE=PATH (at most 8)");
            i++;
        } else if (!strcmp(flag, "--command-repeat")) {
            if (!parse_u64(value, &out->command_repeat) || !out->command_repeat) return reject_argument(flag, "must be a positive integer");
            i++;
        } else if (!strcmp(flag, "--gpu-timestamp-mode")) {
            if (value && !strcmp(value, "off")) out->runtime.timestamp_mode = DOE_TIMESTAMP_OFF;
            else if (value && !strcmp(value, "auto")) out->runtime.timestamp_mode = DOE_TIMESTAMP_AUTO;
            else if (value && !strcmp(value, "require")) out->runtime.timestamp_mode = DOE_TIMESTAMP_REQUIRE;
            else return reject_argument(flag, "must be off, auto or require");
            i++;
        } else if (!strcmp(flag, "--adapter")) {
            if (value && !strcmp(value, "auto")) out->runtime.adapter = DOE_ADAPTER_AUTO;
            else if (value && !strcmp(value, "physical")) out->runtime.adapter = DOE_ADAPTER_PHYSICAL;
            else if (value && !strcmp(value, "software")) out->runtime.adapter = DOE_ADAPTER_SOFTWARE;
            else return reject_argument(flag, "must be auto, physical or software");
            i++;
        } else {
            return reject_argument(flag, "is not implemented by doe-c-runtime");
        }
#undef TAKE
    }
    if (out->backend_lane) {
#ifdef DOE_RUNTIME_DAWN
#ifdef _WIN32
        const char *lane = "d3d12_dawn_release";
#elif defined(__APPLE__)
        const char *lane = "metal_dawn_release";
#else
        const char *lane = "vulkan_dawn_release";
#endif
        char normalized[64];
        size_t length = strlen(out->backend_lane);
        if (length >= sizeof(normalized)) return reject_argument("--backend-lane", "lane name is too long");
        for (size_t i = 0; i <= length; i++) normalized[i] = out->backend_lane[i] == '-' ? '_' : out->backend_lane[i];
#if !defined(__APPLE__) && !defined(_WIN32)
        if (!doe_host_casecmp(normalized, "vulkan_dawn_directional")) strcpy(normalized, "vulkan_dawn_release");
#endif
        if (doe_host_casecmp(normalized, lane)) return reject_argument("--backend-lane", "does not match this compiled Dawn provider");
        out->backend_lane = lane;
        out->runtime.public_webgpu_renderer = true;
#else
        return reject_argument("--backend-lane", "native lane configuration is not implemented by this provider");
#endif
    }
    if (out->plan) {
        /* doe-plan-executor always executes and writes both artifacts; it has no
         * quirks, repeats, oracles or numeric stability policy. */
        if (out->commands) return reject_argument("--plan", "excludes --commands");
        if (!out->workload || !out->trace_jsonl || !out->trace_meta)
            return reject_argument("--plan", "needs --workload, --trace-jsonl and --trace-meta");
        if (out->quirks || quirk_mode || out->command_repeat != 1 || out->validate_oracles || out->numeric_policy ||
            out->numeric_profile ||
            out->trace_stdout || out->replay || out->trace_backend || out->emit_normalized)
            return reject_argument("--plan", "takes no --quirks, --quirk-mode, --command-repeat, oracle, numeric "
                "stability or --trace arguments");
        out->execute = true;
        quirk_mode = "off";
    } else if (out->workload) {
        return reject_argument("--workload", "needs --plan");
    }
    if (out->emit_normalized && (out->validate_oracles || out->readback_count))
        return reject_argument("--emit-normalized", "excludes output oracles and readbacks");
    if (out->execute && out->trace_backend) return reject_argument("--execute", "requires backend native");
    if (!out->execute && (out->readback_count || out->validate_oracles))
        return reject_argument("readback/output oracles", "require --execute");
    if (out->api) {
        static const char *const apis[] = {"vulkan", "metal", "d3d12", "webgpu"};
        const char *canonical = NULL;
        for (size_t i = 0; i < sizeof(apis) / sizeof(*apis); i++)
            if (!doe_host_casecmp(out->api, apis[i])) canonical = apis[i];
        if (!canonical) return reject_argument("--api", "must be vulkan, metal, d3d12 or webgpu");
        out->api = canonical;
    }
    if (out->execute && out->api && strcmp(out->api, DOE_RUNTIME_API)) return reject_argument("--api", "this build uses " DOE_RUNTIME_API);
    /* doe-zig-runtime's defaults: trace mode and an intel/vulkan/gen12/31.0.101 profile. */
    out->quirk_mode = DOE_QUIRK_TRACE;
    if (quirk_mode && !strcmp(quirk_mode, "off")) out->quirk_mode = DOE_QUIRK_OFF;
    else if (quirk_mode && !strcmp(quirk_mode, "active")) out->quirk_mode = DOE_QUIRK_ACTIVE;
    else if (quirk_mode && strcmp(quirk_mode, "trace")) return reject_argument("--quirk-mode", "must be off, trace or active");
    if (!out->vendor) out->vendor = "intel";
    if (!out->api) out->api = DOE_RUNTIME_API;
    if (!out->family) out->family = "gen12";
    if (!out->driver) out->driver = "31.0.101";
    out->profile = (doe_profile){.vendor = out->vendor, .api = out->api, .family = out->family};
    if (!doe_semver_parse(out->driver, out->profile.driver)) return reject_argument("--driver", "must be major[.minor[.patch]]");
    return 0;
}

