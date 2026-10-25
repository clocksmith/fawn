/* SPDX-License-Identifier: MIT
 * doe-c-runtime: executes a runtime command stream through the C WebGPU runtime
 * and writes doe-zig-runtime trace v1 artifacts. Arguments follow
 * doe-zig-runtime; every argument or value this runtime does not implement is
 * rejected before any device work. */
#include "execute.h"
#include "numeric_gate.h"
#include "operators.h"
#include "quirks.h"
#include "stream.h"
#include "trace.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MODULE_NAME "doe-c-runtime"
#define BACKEND_ID "doe_c_vulkan"
enum { MAX_READBACKS = 8 };

typedef struct options {
    const char *commands, *kernel_root, *trace_jsonl, *trace_meta, *quirks, *numeric_policy;
    const char *vendor, *api, *family, *driver;
    bool execute, trace_stdout, validate_oracles;
    doe_quirk_mode quirk_mode;
    doe_profile profile; /* The quirk matching profile, with doe-zig-runtime's defaults. */
    uint64_t command_repeat;
    doe_runtime_options runtime;
    /* --readback-buffer HANDLE=PATH: final buffer contents written after a successful run. */
    uint64_t readback_handles[MAX_READBACKS];
    const char *readback_paths[MAX_READBACKS];
    size_t readback_count;
} options;

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

typedef struct totals {
    uint64_t rows, successes, errors;
    uint64_t total_ns, setup_ns, encode_ns, submit_wait_ns, dispatches, submits;
    uint64_t input_read_ns, input_parse_ns, executor_init_ns, kernel_prewarm_ns;
    uint64_t hash, previous;
    uint64_t oracles, oracles_matched, oracles_failed;
    uint64_t matched, blocking, requires_lean, lean_required;
    bool semantic_tracing;
    char oracle_expected[65], oracle_actual[65];
    const char *oracle_reference;
} totals;

static void usage(FILE *out)
{
    fputs("doe-c-runtime --commands <path> --execute [--backend native] [--kernel-root <path>]\n"
          "  [--trace] [--trace-jsonl <path>] [--trace-meta <path>] [--command-repeat N]\n"
          "  [--vendor X] [--api vulkan] [--family X] [--driver X.Y.Z]\n"
          "  [--quirks <path>] [--quirk-mode off|trace|active] [--queue-sync-mode per-command]\n"
          "  [--upload-buffer-usage copy-dst-copy-src|copy-dst] [--upload-submit-every N]\n"
          "  [--gpu-timestamp-mode off|auto] [--adapter auto|physical|software]\n"
          "  [--validate-output-oracles]  (schema 1 sha256_exact_v1 isolated-dispatch oracles)\n"
          "  [--readback-buffer HANDLE=PATH]...  (final buffer bytes after a successful run; at most 8)\n"
          "  [--numeric-stability-policy <path>]  (consulted only to refuse what doe-zig-runtime would evaluate)\n"
          "Commands: upload, buffer_write, barrier, kernel_dispatch (WGSL kernels, buffer bindings),\n"
          "copies in every direction, texture_write, texture_query, texture_destroy (2D single-mip\n"
          "uncompressed color textures), render_draw and render_pass (the default proxy pipeline),\n"
          "sampler_create and sampler_destroy. kernel_dispatch semantic fields and captures write operator\n"
          "records; a dispatch doe-zig-runtime would evaluate for numeric stability is refused.\n"
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

static int parse_options(int argc, char **argv, options *out)
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
        else if (!strcmp(flag, "--kernel-root")) TAKE(runtime.kernel_root);
        else if (!strcmp(flag, "--trace-jsonl")) TAKE(trace_jsonl);
        else if (!strcmp(flag, "--trace-meta")) TAKE(trace_meta);
        else if (!strcmp(flag, "--quirks")) TAKE(quirks);
        else if (!strcmp(flag, "--numeric-stability-policy")) TAKE(numeric_policy);
        else if (!strcmp(flag, "--vendor")) TAKE(vendor);
        else if (!strcmp(flag, "--api")) TAKE(api);
        else if (!strcmp(flag, "--family")) TAKE(family);
        else if (!strcmp(flag, "--driver")) TAKE(driver);
        else if (!strcmp(flag, "--execute")) out->execute = true;
        else if (!strcmp(flag, "--trace")) out->trace_stdout = true;
        else if (!strcmp(flag, "--validate-output-oracles")) out->validate_oracles = true;
        else if (!strcmp(flag, "--quirk-mode")) { if (!value) return reject_argument(flag, "needs a value"); quirk_mode = value; i++; }
        else if (!strcmp(flag, "--backend")) {
            if (!value || strcmp(value, "native")) return reject_argument(flag, "only \"native\" execution is implemented");
            i++;
        } else if (!strcmp(flag, "--queue-sync-mode")) {
            if (!value || strcmp(value, "per-command")) return reject_argument(flag, "only \"per-command\" is implemented");
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
            /* auto records no GPU timestamps when none are taken, which is every run here. */
            if (!value || (strcmp(value, "off") && strcmp(value, "auto")))
                return reject_argument(flag, "GPU timestamps are not implemented; use off or auto");
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
    if (!out->commands) return reject_argument("--commands", "is required (no embedded sample stream)");
    if (!out->execute) return reject_argument("--execute", "is required (trace-only mode is not implemented)");
    if (out->api && strcmp(out->api, "vulkan")) return reject_argument("--api", "only vulkan is implemented");
    /* doe-zig-runtime's defaults: trace mode and an intel/vulkan/gen12/31.0.101 profile. */
    out->quirk_mode = DOE_QUIRK_TRACE;
    if (quirk_mode && !strcmp(quirk_mode, "off")) out->quirk_mode = DOE_QUIRK_OFF;
    else if (quirk_mode && !strcmp(quirk_mode, "active")) out->quirk_mode = DOE_QUIRK_ACTIVE;
    else if (quirk_mode && strcmp(quirk_mode, "trace")) return reject_argument("--quirk-mode", "must be off, trace or active");
    if (!out->vendor) out->vendor = "intel";
    if (!out->api) out->api = "vulkan";
    if (!out->family) out->family = "gen12";
    if (!out->driver) out->driver = "31.0.101";
    out->profile = (doe_profile){.vendor = out->vendor, .api = out->api, .family = out->family};
    if (!doe_semver_parse(out->driver, out->profile.driver)) return reject_argument("--driver", "must be major[.minor[.patch]]");
    return 0;
}

static char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = NULL;
    size_t length = 0, capacity = 0;
    for (;;) {
        if (length == capacity) {
            size_t grown_capacity = capacity ? capacity * 2 : 65536;
            char *grown = realloc(text, grown_capacity);
            if (!grown) { free(text); fclose(file); return NULL; }
            text = grown;
            capacity = grown_capacity;
        }
        size_t read = fread(text + length, 1, capacity - length, file);
        length += read;
        if (read == 0) break;
    }
    bool failed = ferror(file);
    fclose(file);
    if (failed) { free(text); return NULL; }
    *size = length;
    return text;
}

static uint64_t realtime_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static bool write_optional(FILE *out, const char *key, const char *value, bool comma)
{
    if (fprintf(out, "%s\"%s\":", comma ? "," : "", key) < 0) return false;
    return value ? doe_trace_write_string(out, value) : fputs("null", out) >= 0;
}

static bool write_meta(const char *path, const options *o, const totals *t, uint64_t command_count,
    const doe_runtime_adapter *adapter, const doe_operator_recorder *operators)
{
    FILE *out = fopen(path, "wb");
    if (!out) return false;
    uint64_t seq_max = t->rows ? t->rows - 1 : 0;
    bool ok = fprintf(out, "{\"traceVersion\":1,\"module\":\"" MODULE_NAME "\",\"seqMax\":%" PRIu64 ",\"rowCount\":%" PRIu64
        ",\"commandCount\":%" PRIu64 ",\"matchedCount\":%" PRIu64 ",\"blockingCount\":%" PRIu64 ",\"requiresLeanCount\":%" PRIu64
        ",\"leanRequiredCount\":%" PRIu64 ","
        "\"executionRowCount\":%" PRIu64 ",\"executionSuccessCount\":%" PRIu64 ",\"executionErrorCount\":%" PRIu64
        ",\"executionSkippedCount\":0,\"executionUnsupportedCount\":0,\"executionTotalNs\":%" PRIu64
        ",\"executionSetupTotalNs\":%" PRIu64 ",\"executionEncodeTotalNs\":%" PRIu64 ",\"executionSubmitWaitTotalNs\":%" PRIu64
        ",\"executionDispatchCount\":%" PRIu64 ",\"executionSubmitCount\":%" PRIu64 ",\"hostInputReadTotalNs\":%" PRIu64
        ",\"hostInputParseTotalNs\":%" PRIu64 ",\"hostExecutorInitTotalNs\":%" PRIu64
        ",\"hostUploadPrewarmTotalNs\":0,\"hostKernelPrewarmTotalNs\":%" PRIu64
        ",\"executionGpuTimestampTotalNs\":0,\"executionGpuTimestampAttemptedCount\":0,\"executionGpuTimestampValidCount\":0,"
        "\"semanticTracingEnabled\":%s,\"semanticOpRowCount\":%" PRIu64 ",\"semanticCaptureCount\":%" PRIu64
        ",\"semanticReproCount\":%" PRIu64 ","
        "\"hash\":\"0x%" PRIx64 "\",\"previousHash\":\"0x%" PRIx64 "\",\"outputOracleCount\":%" PRIu64
        ",\"outputOracleMatchedCount\":%" PRIu64 ",\"outputOracleFailedCount\":%" PRIu64
        ",\"executionBackend\":\"" BACKEND_ID "\",\"backendId\":\"" BACKEND_ID "\","
        "\"backendSelectionReason\":\"fixed_backend\",\"fallbackUsed\":false",
        seq_max, t->rows, command_count, t->matched, t->blocking, t->requires_lean, t->lean_required, t->rows, t->successes, t->errors, t->total_ns, t->setup_ns, t->encode_ns,
        t->submit_wait_ns, t->dispatches, t->submits, t->input_read_ns, t->input_parse_ns, t->executor_init_ns,
        t->kernel_prewarm_ns, t->semantic_tracing ? "true" : "false", operators ? operators->row_count : 0,
        operators ? operators->capture_count : 0, operators ? operators->repro_count : 0,
        t->hash, t->previous, t->oracles, t->oracles_matched, t->oracles_failed) >= 0;
    /* doe-zig-runtime names the operator manifest only when it holds records. */
    if (ok && operators && operators->row_count)
        ok = write_optional(out, "operatorRecordManifestPath", operators->manifest_path, true) &&
            write_optional(out, "operatorRecordManifestHash", operators->manifest_sha256, true);
    ok = ok && fprintf(out, ",\"queueSyncMode\":\"per-command\",\"quirkMode\":\"%s\",\"uploadBufferUsage\":\"%s\""
        ",\"uploadSubmitEvery\":%" PRIu64
        /* No persistent pipeline cache exists in this runtime. */
        ",\"pipelineCache\":{\"state\":\"disabled\",\"reason\":\"platform-unsupported\",\"warmupCount\":0,\"warmupNs\":0}",
        o->quirk_mode == DOE_QUIRK_OFF ? "off" : o->quirk_mode == DOE_QUIRK_ACTIVE ? "active" : "trace",
        o->runtime.upload_copy_src ? "copy-dst-copy-src" : "copy-dst", o->runtime.upload_submit_every) >= 0;
    if (ok && t->oracles) {
        ok = write_optional(out, "outputOracleKind", "sha256_exact_v1", true) &&
            write_optional(out, "outputOracleReferenceId", t->oracle_reference, true) &&
            write_optional(out, "outputOracleExpectedSha256", t->oracle_expected, true) &&
            write_optional(out, "outputOracleActualSha256", t->oracle_actual[0] ? t->oracle_actual : NULL, true) &&
            /* Schema 1 oracles are independent references compared by exact digest. */
            write_optional(out, "outputOracleReferenceClass", "independent_v1", true) &&
            fputs(",\"outputOracleComparedValueCount\":0,\"outputOracleMismatchCount\":0,\"outputOracleMaxAbsoluteError\":0,"
                "\"outputOracleMaxRelativeError\":0,\"outputOracleAbsoluteTolerance\":0,\"outputOracleRelativeTolerance\":0", out) >= 0;
    }
    ok = ok && fputs(",\"profile\":{", out) >= 0;
    ok = ok && write_optional(out, "vendor", o->vendor, false) && write_optional(out, "api", o->api ? o->api : "vulkan", true) &&
        write_optional(out, "deviceFamily", o->family, true) && write_optional(out, "driver", o->driver, true);
    ok = ok && fputs("},\"adapter\":{", out) >= 0;
    if (adapter) {
        ok = ok && write_optional(out, "vendor", adapter->vendor, false) && write_optional(out, "architecture", adapter->architecture, true) &&
            write_optional(out, "device", adapter->device, true) && write_optional(out, "description", adapter->description, true) &&
            write_optional(out, "adapterType", adapter->adapter_type, true) && write_optional(out, "backend", adapter->backend, true) &&
            fprintf(out, ",\"vendorId\":%u,\"deviceId\":%u", adapter->vendor_id, adapter->device_id) >= 0;
    }
    ok = ok && fputs("}}\n", out) >= 0;
    if (fclose(out) != 0) ok = false;
    return ok;
}

/* Refuses, before device work, a stream doe-zig-runtime would evaluate for
 * numeric stability: it loads the policy whenever it writes a trace meta. An
 * explicit policy is read and checked even without a trace meta. */
static int check_numeric_stability(const options *o, const doe_stream *stream)
{
    if (!o->numeric_policy && !(o->trace_meta && doe_numeric_gate_needed(stream))) return 0;
    const char *path = o->numeric_policy ? o->numeric_policy : DOE_NUMERIC_STABILITY_DEFAULT_POLICY;
    size_t size = 0;
    char *text = read_file(path, &size);
    if (!text) {
        fprintf(stderr, "doe-c-runtime: numeric stability policy %s: cannot read (needed to find the semantic "
            "operations doe-zig-runtime would evaluate)\n", path);
        return 2;
    }
    size_t index = 0;
    char profile[128] = "", message[256] = "";
    doe_numeric_gate_result result = doe_numeric_gate_check(text, size, stream, &index, profile, sizeof(profile),
        message, sizeof(message));
    free(text);
    if (result == DOE_NUMERIC_GATE_INVALID) {
        fprintf(stderr, "doe-c-runtime: numeric stability policy %s: %s\n", path, message);
        return 2;
    }
    if (result == DOE_NUMERIC_GATE_OBSERVED && o->trace_meta) {
        fprintf(stderr, "doe-c-runtime: %s: command %zu: numeric stability evaluation (%s) is not implemented by "
            "doe-c-runtime; doe-zig-runtime evaluates it when a trace meta is written\n", o->commands, index, profile);
        return 2;
    }
    return 0;
}

/* The capture a semantic command requested, read after it ran; failures are
 * recorded in the operator record, as doe-zig-runtime does, not fatal. */
static void take_capture(doe_runtime *runtime, const doe_semantic *semantic, doe_operator_capture *capture,
    uint8_t **owned)
{
    *capture = (doe_operator_capture){0};
    *owned = NULL;
    uint8_t *data = NULL;
    size_t size = 0;
    doe_trace_execution execution;
    if (!doe_runtime_read_buffer(runtime, semantic->capture_handle, &data, &size, &execution)) {
        /* doe-zig-runtime reports an unknown handle as InvalidArgument. */
        capture->error_code = strcmp(execution.status_code, "readback_missing") ? execution.status_code : "InvalidArgument";
        return;
    }
    if (semantic->capture_offset > size || semantic->capture_size > size - semantic->capture_offset) {
        free(data);
        capture->error_code = "InvalidArgument";
        return;
    }
    *owned = data;
    capture->ok = true;
    capture->data = data + semantic->capture_offset;
    capture->size = (size_t)semantic->capture_size;
}

int main(int argc, char **argv)
{
    options o;
    int parsed = parse_options(argc, argv, &o);
    if (parsed) return parsed == 1 ? 0 : parsed;
    totals t = {.hash = DOE_TRACE_HASH_SEED, .previous = DOE_TRACE_HASH_SEED};
    uint64_t read_start = doe_runtime_monotonic_ns();
    size_t size = 0;
    char *text = read_file(o.commands, &size);
    if (!text) { fprintf(stderr, "doe-c-runtime: cannot read %s\n", o.commands); return 2; }
    uint64_t parse_start = doe_runtime_monotonic_ns();
    doe_stream stream;
    doe_stream_error stream_error;
    bool stream_ok = doe_stream_parse(text, size, &stream, &stream_error);
    free(text);
    t.input_read_ns = parse_start - read_start;
    t.input_parse_ns = doe_runtime_monotonic_ns() - parse_start;
    if (!stream_ok) {
        if (stream_error.command_index == SIZE_MAX) fprintf(stderr, "doe-c-runtime: %s: %s\n", o.commands, stream_error.message);
        else fprintf(stderr, "doe-c-runtime: %s: command %zu: %s\n", o.commands, stream_error.command_index, stream_error.message);
        return 2;
    }
    if (stream.count > UINT64_MAX / o.command_repeat) { doe_stream_free(&stream); return reject_argument("--command-repeat", "overflows"); }
    int refused = check_numeric_stability(&o, &stream);
    if (refused) { doe_stream_free(&stream); return refused; }
    uint64_t command_count = (uint64_t)stream.count * o.command_repeat;
    FILE *rows = o.trace_jsonl ? fopen(o.trace_jsonl, "wb") : NULL;
    if (o.trace_jsonl && !rows) {
        fprintf(stderr, "doe-c-runtime: cannot write %s\n", o.trace_jsonl);
        doe_stream_free(&stream);
        return 2;
    }
    char message[256] = "";
    /* Without --quirks no quirk is loaded; doe-zig-runtime would load its embedded sample quirk. */
    doe_quirk_set quirks = {.mode = o.quirk_mode};
    if (o.quirks && o.quirk_mode != DOE_QUIRK_OFF) {
        size_t quirks_size = 0;
        char *quirks_text = read_file(o.quirks, &quirks_size);
        bool loaded = quirks_text && doe_quirks_load(quirks_text, quirks_size, o.quirk_mode, &o.profile, &quirks, message,
            sizeof(message));
        free(quirks_text);
        if (!loaded) {
            fprintf(stderr, "doe-c-runtime: --quirks %s: %s\n", o.quirks, quirks_text ? message : "cannot read");
            if (rows) fclose(rows);
            doe_stream_free(&stream);
            return 2;
        }
    }
    /* doe-zig-runtime anchors operator records at the trace meta, else the trace JSONL. */
    const char *operator_anchor = o.trace_meta ? o.trace_meta : o.trace_jsonl;
    doe_operator_recorder operators = {0};
    if (operator_anchor && !doe_operator_recorder_open(&operators, operator_anchor)) {
        fprintf(stderr, "doe-c-runtime: out of memory\n");
        if (rows) fclose(rows);
        doe_quirks_free(&quirks);
        doe_stream_free(&stream);
        return 1;
    }
    uint64_t init_start = doe_runtime_monotonic_ns();
    doe_runtime *runtime = doe_runtime_open(&o.runtime, message, sizeof(message));
    t.executor_init_ns = doe_runtime_monotonic_ns() - init_start;
    int status = 0;
    if (!runtime) {
        fprintf(stderr, "doe-c-runtime: %s\n", message);
        status = 1;
    }
    /* Kernel pipelines compile before the command loop, outside execution timing. */
    uint64_t prewarm_start = doe_runtime_monotonic_ns();
    for (size_t i = 0; runtime && status == 0 && i < stream.count; i++) {
        doe_trace_execution prewarm;
        if (!doe_runtime_prewarm_kernel(runtime, &stream.commands[i], &prewarm))
            fprintf(stderr, "doe-c-runtime: warning: kernel prewarm of command %zu: %s: %s\n", i, prewarm.status_code, prewarm.message);
    }
    t.kernel_prewarm_ns = doe_runtime_monotonic_ns() - prewarm_start;
    const doe_trace_identity identity = {.module = MODULE_NAME, .backend_id = BACKEND_ID};
    for (uint64_t pass = 0; runtime && status == 0 && pass < o.command_repeat; pass++) {
        for (size_t i = 0; i < stream.count && status == 0; i++) {
            const doe_command *command = &stream.commands[i];
            const doe_quirk_decision decision = doe_quirks_decide(&quirks, command->kind);
            if (doe_semantic_present(&command->semantic)) t.semantic_tracing = true;
            doe_command applied = *command;
            if (decision.temporary_buffer) {
                applied.as.copy.temporary_buffer = true;
                applied.as.copy.temporary_buffer_alignment = decision.temporary_buffer_alignment;
            }
            doe_trace_execution execution;
            bool ok = doe_runtime_execute(runtime, &applied, &execution);
            uint64_t seq = t.rows;
            uint64_t hash = doe_trace_payload_hash(t.hash, seq, command->label, doe_command_tag(command->kind),
                command->kind == DOE_COMMAND_KERNEL_DISPATCH ? command->as.dispatch.kernel : NULL, &decision,
                &command->semantic);
            uint64_t now = realtime_ns();
            if (rows && !doe_trace_write_row(rows, &identity, seq, now, hash, t.hash, command, &decision, &execution)) status = 1;
            if (o.trace_stdout && !doe_trace_write_row(stdout, &identity, seq, now, hash, t.hash, command, &decision, &execution))
                status = 1;
            if (operator_anchor && doe_semantic_present(&command->semantic)) {
                doe_operator_capture capture;
                uint8_t *capture_data = NULL;
                if (command->semantic.has_capture) take_capture(runtime, &command->semantic, &capture, &capture_data);
                const doe_operator_input input = {.source_index = seq, .command = command, .execution = &execution,
                    .backend_id = BACKEND_ID, .vendor = o.vendor, .api = o.api, .family = o.family, .driver = o.driver,
                    .hash = hash, .previous = t.hash, .trace_meta_path = o.trace_meta,
                    .capture = command->semantic.has_capture ? &capture : NULL};
                char record_message[512];
                if (!doe_operator_record(&operators, &input, record_message, sizeof(record_message))) {
                    fprintf(stderr, "doe-c-runtime: %s\n", record_message);
                    status = 1;
                }
                free(capture_data);
            }
            if (decision.matched_id) {
                t.matched++;
                if (!strcmp(decision.verification, "lean_required")) t.lean_required++;
            }
            if (decision.requires_lean) t.requires_lean++;
            if (decision.blocking) t.blocking++;
            t.previous = t.hash;
            t.hash = hash;
            t.rows++;
            t.total_ns += execution.duration_ns;
            t.setup_ns += execution.setup_ns;
            t.encode_ns += execution.encode_ns;
            t.submit_wait_ns += execution.submit_wait_ns;
            t.dispatches += execution.dispatch_count;
            t.submits += execution.submit_count;
            if (ok) t.successes++;
            else {
                t.errors++;
                fprintf(stderr, "doe-c-runtime: command %zu (%s): %s: %s\n", i, command->label,
                    execution.status_code, execution.message);
                status = 1;
            }
        }
    }
    for (size_t i = 0; runtime && status == 0 && o.validate_oracles && i < stream.count; i++) {
        const doe_command *command = &stream.commands[i];
        if (command->kind != DOE_COMMAND_KERNEL_DISPATCH || !command->as.dispatch.oracle) continue;
        doe_output_oracle oracle;
        char reason[256];
        t.oracles++;
        if (!doe_output_oracle_parse(command->as.dispatch.oracle, &oracle, reason, sizeof(reason))) {
            fprintf(stderr, "doe-c-runtime: command %zu output_oracle: %s\n", i, reason);
            t.oracles_failed++;
            status = 1;
            continue;
        }
        memcpy(t.oracle_expected, oracle.expected_sha256, sizeof(t.oracle_expected));
        t.oracle_reference = oracle.reference_id;
        t.oracle_actual[0] = '\0';
        doe_trace_execution execution;
        if (!doe_runtime_validate_oracle(runtime, command, &oracle, t.oracle_actual, &execution)) {
            fprintf(stderr, "doe-c-runtime: command %zu output_oracle: %s: %s\n", i, execution.status_code, execution.message);
            t.oracles_failed++;
            status = 1;
        } else if (strcmp(t.oracle_actual, oracle.expected_sha256)) {
            fprintf(stderr, "doe-c-runtime: command %zu output_oracle mismatch: expected %s, read back %s\n", i,
                oracle.expected_sha256, t.oracle_actual);
            t.oracles_failed++;
            status = 1;
        } else {
            t.oracles_matched++;
        }
    }
    for (size_t i = 0; runtime && status == 0 && i < o.readback_count; i++) {
        uint8_t *data = NULL;
        size_t size = 0;
        doe_trace_execution execution;
        if (!doe_runtime_read_buffer(runtime, o.readback_handles[i], &data, &size, &execution)) {
            fprintf(stderr, "doe-c-runtime: --readback-buffer %llu: %s: %s\n", (unsigned long long)o.readback_handles[i],
                execution.status_code, execution.message);
            status = 1;
            break;
        }
        FILE *out = fopen(o.readback_paths[i], "wb");
        bool written = out && fwrite(data, 1, size, out) == size;
        if (out && fclose(out) != 0) written = false;
        free(data);
        if (!written) {
            fprintf(stderr, "doe-c-runtime: cannot write %s\n", o.readback_paths[i]);
            status = 1;
        }
    }
    if (rows && fclose(rows) != 0) status = 1;
    if (operator_anchor && !doe_operator_recorder_finish(&operators)) {
        fprintf(stderr, "doe-c-runtime: cannot write %s\n", operators.manifest_path);
        status = 1;
    }
    if (o.trace_meta && !write_meta(o.trace_meta, &o, &t, command_count, runtime ? doe_runtime_adapter_info(runtime) : NULL,
            &operators)) {
        fprintf(stderr, "doe-c-runtime: cannot write %s\n", o.trace_meta);
        status = 1;
    }
    doe_operator_recorder_free(&operators);
    doe_runtime_close(runtime);
    doe_quirks_free(&quirks);
    doe_stream_free(&stream);
    return status;
}
