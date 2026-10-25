/* SPDX-License-Identifier: MIT
 * doe-c-runtime: executes a runtime command stream through the C WebGPU runtime
 * and writes doe-zig-runtime trace v1 artifacts. Arguments follow
 * doe-zig-runtime; every argument or value this runtime does not implement is
 * rejected before any device work. */
#include "execute.h"
#include "cli_options.h"
#include "cli_inputs.h"
#include "cli_output.h"
#include "numeric_policy.h"
#include "numeric_stability.h"
#include "operators.h"
#include "plan.h"
#include "quirks.h"
#include "sha256.h"
#include "stream.h"
#include "trace.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct totals {
    uint64_t rows, successes, errors;
    uint64_t total_ns, setup_ns, encode_ns, submit_wait_ns, dispatches, submits;
    uint64_t input_read_ns, input_parse_ns, executor_init_ns, kernel_prewarm_ns;
    uint64_t hash, previous;
    uint64_t oracles, oracles_matched, oracles_failed;
    uint64_t matched, blocking, requires_lean, lean_required, skipped;
    bool semantic_tracing;
    char oracle_expected[65], oracle_actual[65];
    const char *oracle_reference;
} totals;

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

static bool load_cli_quirks(const options *options, doe_quirk_set *quirks, char *message, size_t capacity)
{
    *quirks = (doe_quirk_set){.mode = options->quirk_mode};
    if (options->quirk_mode == DOE_QUIRK_OFF) return true;
    size_t size = 0;
    char *text = options->quirks ? read_file(options->quirks, &size) : strdup(cli_default_quirks());
    if (!text) {
        snprintf(message, capacity, "cannot read %s", options->quirks ? options->quirks : "<embedded>");
        return false;
    }
    if (!options->quirks) size = strlen(text);
    bool ok = doe_quirks_load(text, size, options->quirk_mode, &options->profile, quirks, message, capacity);
    free(text);
    return ok;
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

/* shaderSourceReceipts (config/trace-meta.schema.json $defs/shaderSourceReceipt)
 * for every shader the stream compiles, in first-use order: kernel_dispatch
 * kernels (and dispatch_noop.wgsl for a grid before any kernel_dispatch), then
 * the render proxy's two modules. shaderSourceReceiptsHash is the
 * SHA-256 of the array's compact JSON as written, as the package executor
 * hashes JSON.stringify(receipts), so two runtimes given the same sources
 * agree on it. */
static bool write_shader_receipts(FILE *out, const doe_runtime *runtime, const doe_stream *stream)
{
    char *bytes = NULL;
    size_t length = 0;
    FILE *list = open_memstream(&bytes, &length);
    if (!list) return false;
    bool ok = fputc('[', list) != EOF, any = false, render = false, kernel_seen = false, noop_written = false;
    for (size_t i = 0; ok && i < stream->count; i++) {
        const doe_command *command = &stream->commands[i];
        if (doe_command_is_render(command->kind)) render = true;
        bool grid = command->kind == DOE_COMMAND_DISPATCH || command->kind == DOE_COMMAND_DISPATCH_INDIRECT;
        if (grid && !kernel_seen && !noop_written) {
            char *path = NULL, sha[65];
            size_t size = 0;
            if (doe_runtime_kernel_source(runtime, "dispatch_noop.wgsl", &path, &size, sha)) {
                ok = (!any || fputc(',', list) != EOF) && fputs("{\"moduleId\":\"dispatch_noop.wgsl\",\"sourceKind\":\"wgsl\",\"path\":", list) >= 0 &&
                    doe_trace_write_string(list, path) &&
                    fprintf(list, ",\"entryPoint\":\"main\",\"byteLength\":%zu,\"sha256\":\"%s\"}", size, sha) >= 0;
                any = true;
                free(path);
            }
            noop_written = true;
        }
        if (command->kind != DOE_COMMAND_KERNEL_DISPATCH) continue;
        kernel_seen = true;
        bool seen = false;
        for (size_t j = 0; j < i && !seen; j++) {
            const doe_command *earlier = &stream->commands[j];
            seen = earlier->kind == DOE_COMMAND_KERNEL_DISPATCH && !strcmp(earlier->as.dispatch.kernel, command->as.dispatch.kernel) &&
                !strcmp(earlier->as.dispatch.entry_point, command->as.dispatch.entry_point);
        }
        if (seen) continue;
        char *path = NULL, sha[65];
        size_t size = 0;
        if (!doe_runtime_kernel_source(runtime, command->as.dispatch.kernel, &path, &size, sha)) continue;
        ok = (!any || fputc(',', list) != EOF) && fputs("{\"moduleId\":", list) >= 0 &&
            doe_trace_write_string(list, command->as.dispatch.kernel) && fputs(",\"sourceKind\":\"wgsl\",\"path\":", list) >= 0 &&
            doe_trace_write_string(list, path) && fputs(",\"entryPoint\":", list) >= 0 &&
            doe_trace_write_string(list, command->as.dispatch.entry_point) &&
            fprintf(list, ",\"byteLength\":%zu,\"sha256\":\"%s\"}", size, sha) >= 0;
        any = true;
        free(path);
    }
    static const char *const proxy[2][2] = {{"draw_call_proxy_vertex.wgsl", "main"}, {"draw_call_proxy_fragment.wgsl", "main"}};
    for (int k = 0; ok && render && k < 2; k++) {
        char *path = NULL, sha[65];
        size_t size = 0;
        if (!doe_runtime_kernel_source(runtime, proxy[k][0], &path, &size, sha)) continue;
        ok = (!any || fputc(',', list) != EOF) && fputs("{\"moduleId\":", list) >= 0 && doe_trace_write_string(list, proxy[k][0]) &&
            fputs(",\"sourceKind\":\"wgsl\",\"path\":", list) >= 0 && doe_trace_write_string(list, path) &&
            fprintf(list, ",\"byteLength\":%zu,\"sha256\":\"%s\"}", size, sha) >= 0;
        any = true;
        free(path);
    }
    for (int compute = 0; ok && compute < 2; compute++) {
        if (!doe_runtime_used_diagnostic_source(runtime, compute != 0)) continue;
        const char *source = doe_runtime_diagnostic_source(compute != 0);
        uint8_t digest[32];
        char hash[65];
        doe_sha256(source, strlen(source), digest);
        doe_sha256_hex(digest, hash);
        ok = (!any || fputc(',', list) != EOF) && fprintf(list,
            "{\"moduleId\":\"doe-diagnostic-%s\",\"sourceKind\":\"wgsl\",\"byteLength\":%zu,\"sha256\":\"%s\"}",
            compute ? "compute" : "render", strlen(source), hash) >= 0;
        any = true;
    }
    ok = fputc(']', list) != EOF && ok;
    ok = fclose(list) == 0 && ok;
    /* An empty list when nothing compiles, so two runtimes that compile no
     * shader still agree on the receipt hash. */
    if (ok) {
        uint8_t digest[32];
        char hash[65];
        doe_sha256(bytes, length, digest);
        doe_sha256_hex(digest, hash);
        ok = fputs(",\"shaderSourceReceipts\":", out) >= 0 && fwrite(bytes, 1, length, out) == length &&
            fprintf(out, ",\"shaderSourceReceiptsHash\":\"%s\"", hash) >= 0;
    }
    free(bytes);
    return ok;
}

static bool write_meta(const char *path, const options *o, const totals *t, uint64_t command_count,
    const doe_runtime_adapter *adapter, const doe_operator_recorder *operators, const char *plan_sha256,
    const doe_runtime *runtime, const doe_stream *stream, const doe_ns_recorder *numeric)
{
    FILE *out = fopen(path, "wb");
    if (!out) return false;
    uint64_t seq_max = t->rows ? t->rows - 1 : 0;
    bool ok = fprintf(out, "{\"traceVersion\":1,\"module\":\"" MODULE_NAME "\",\"seqMax\":%" PRIu64 ",\"rowCount\":%" PRIu64
        ",\"commandCount\":%" PRIu64 ",\"matchedCount\":%" PRIu64 ",\"blockingCount\":%" PRIu64 ",\"requiresLeanCount\":%" PRIu64
        ",\"leanRequiredCount\":%" PRIu64 ","
        "\"executionRowCount\":%" PRIu64 ",\"executionSuccessCount\":%" PRIu64 ",\"executionErrorCount\":%" PRIu64
        ",\"executionSkippedCount\":%" PRIu64 ",\"executionUnsupportedCount\":0,\"executionTotalNs\":%" PRIu64
        ",\"executionSetupTotalNs\":%" PRIu64 ",\"executionEncodeTotalNs\":%" PRIu64 ",\"executionSubmitWaitTotalNs\":%" PRIu64
        ",\"executionDispatchCount\":%" PRIu64 ",\"executionSubmitCount\":%" PRIu64 ",\"hostInputReadTotalNs\":%" PRIu64
        ",\"hostInputParseTotalNs\":%" PRIu64 ",\"hostExecutorInitTotalNs\":%" PRIu64
        ",\"hostUploadPrewarmTotalNs\":0,\"hostKernelPrewarmTotalNs\":%" PRIu64
        ",\"executionGpuTimestampTotalNs\":0,\"executionGpuTimestampAttemptedCount\":0,\"executionGpuTimestampValidCount\":0,"
        "\"semanticTracingEnabled\":%s,\"semanticOpRowCount\":%" PRIu64 ",\"semanticCaptureCount\":%" PRIu64
        ",\"semanticReproCount\":%" PRIu64 ","
        "\"hash\":\"0x%" PRIx64 "\",\"previousHash\":\"0x%" PRIx64 "\",\"outputOracleCount\":%" PRIu64
        ",\"outputOracleMatchedCount\":%" PRIu64 ",\"outputOracleFailedCount\":%" PRIu64,
        seq_max, t->rows, command_count, t->matched, t->blocking, t->requires_lean, t->lean_required,
        o->execute ? t->rows : 0, t->successes, t->errors, t->skipped, t->total_ns, t->setup_ns, t->encode_ns,
        t->submit_wait_ns, t->dispatches, t->submits, t->input_read_ns, t->input_parse_ns, t->executor_init_ns,
        t->kernel_prewarm_ns, t->semantic_tracing ? "true" : "false", operators ? operators->row_count : 0,
        operators ? operators->capture_count : 0, operators ? operators->repro_count : 0,
        t->hash, t->previous, t->oracles, t->oracles_matched, t->oracles_failed) >= 0;
    if (ok && o->execute) ok = fputs(",\"executionBackend\":\"" BACKEND_ID "\",\"backendId\":\"" BACKEND_ID "\","
        "\"backendSelectionReason\":\"fixed_backend\",\"fallbackUsed\":false", out) >= 0;
    /* doe-plan-executor names the plan and its planSha256 as the host plan artifact. */
    if (ok && o->plan)
        ok = write_optional(out, "hostPlanArtifactPath", o->plan, true) &&
            write_optional(out, "hostPlanArtifactHash", plan_sha256, true);
    /* doe-zig-runtime names the operator manifest only when it holds records. */
    if (ok && operators && operators->row_count)
        ok = write_optional(out, "operatorRecordManifestPath", operators->manifest_path, true) &&
            write_optional(out, "operatorRecordManifestHash", operators->manifest_sha256, true);
    /* The upload arguments are recorded by the caller, as for doe-zig-runtime,
     * whose meta has no upload fields; the bench reads a meta uploadSubmitEvery
     * as an upload normalization input. */
    ok = ok && fprintf(out, ",\"queueSyncMode\":\"%s\",\"queueWaitMode\":\"%s\",\"quirkMode\":\"%s\""
        /* No persistent pipeline cache exists in this runtime. */
        ",\"pipelineCache\":{\"state\":\"disabled\",\"reason\":\"platform-unsupported\",\"warmupCount\":0,\"warmupNs\":0}",
        o->runtime.defer_queue_sync ? "deferred" : "per-command",
        o->runtime.queue_wait == DOE_WAIT_PROCESS_EVENTS ? "process-events" : "wait-any",
        o->quirk_mode == DOE_QUIRK_OFF ? "off" : o->quirk_mode == DOE_QUIRK_ACTIVE ? "active" : "trace") >= 0;
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
    if (ok && runtime) ok = write_shader_receipts(out, runtime, stream);
    if (ok && numeric) ok = doe_ns_write_summary(out, numeric);
    ok = ok && fputs(",\"profile\":{", out) >= 0;
    ok = ok && write_optional(out, "vendor", o->vendor, false) && write_optional(out, "api", o->api ? o->api : DOE_RUNTIME_API, true) &&
        write_optional(out, "deviceFamily", o->family, true) && write_optional(out, "driver", o->driver, true);
    /* adapterInfo in the package traces' field names (docs/workload-system.md),
     * which bench run receipts read as hostIdentity.adapter; null when no
     * adapter opened. */
    ok = ok && fputs("},\"adapterInfo\":", out) >= 0;
    if (adapter) {
        ok = ok && fputc('{', out) != EOF && write_optional(out, "vendor", adapter->vendor, false) &&
            write_optional(out, "architecture", adapter->architecture, true) &&
            write_optional(out, "device", adapter->device, true) && write_optional(out, "description", adapter->description, true) &&
            write_optional(out, "backendType", adapter->backend, true) && write_optional(out, "adapterType", adapter->adapter_type, true) &&
            fprintf(out, ",\"vendorID\":%u,\"deviceID\":%u,\"driverVersion\":%u}", adapter->vendor_id, adapter->device_id,
                adapter->driver_version) >= 0;
    } else {
        ok = ok && fputs("null", out) >= 0;
    }
    ok = ok && fputs("}\n", out) >= 0;
    if (fclose(out) != 0) ok = false;
    return ok;
}

/* Loads the numeric stability policy when doe-zig-runtime would evaluate:
 * with a trace meta and a dispatch carrying op id, stage and phase (or an
 * explicit policy, which is always validated). A dispatch whose auto-detect
 * profile uses a capture plan this runtime does not evaluate is refused
 * before device work. Returns 0, or the exit code. */
static int load_numeric_policy(const options *o, const doe_stream *stream, doe_ns_policy *policy, char **text, bool *loaded)
{
    *loaded = false;
    bool needed = false;
    for (size_t i = 0; i < stream->count && !needed; i++) needed = doe_ns_semantic_complete(&stream->commands[i]);
    /* doe-plan-executor evaluates no numeric stability. */
    if (o->plan || (!o->numeric_policy && !o->numeric_profile && !(o->trace_meta && needed))) return 0;
    const char *path = o->numeric_policy ? o->numeric_policy : DOE_NUMERIC_STABILITY_DEFAULT_POLICY;
    size_t size = 0;
    *text = read_file(path, &size);
    char message[256] = "";
    if (!*text) {
        fprintf(stderr, "doe-c-runtime: numeric stability policy %s: cannot read\n", path);
        return 2;
    }
    if (!doe_ns_policy_load(*text, size, path, o->numeric_profile, policy, message, sizeof(message))) {
        fprintf(stderr, "doe-c-runtime: numeric stability policy %s: %s\n", path, message);
        free(*text);
        *text = NULL;
        return 2;
    }
    *loaded = true;
    for (size_t i = 0; o->execute && o->trace_meta && i < stream->count; i++) {
        const doe_json_value *profile = doe_ns_auto_detect(policy, &stream->commands[i]);
        const char *plan = doe_ns_string(profile, "capturePlanId");
        if (profile && (!plan || strcmp(plan, DOE_NS_DECODE_FINAL_LOGITS_PLAN))) {
            fprintf(stderr, "doe-c-runtime: %s: command %zu: numeric stability evaluation (%s, capture plan %s) is not "
                "implemented by doe-c-runtime; doe-zig-runtime evaluates it when a trace meta is written\n", o->commands, i,
                doe_ns_string(profile, "profileId"), plan ? plan : "(none)");
            return 2;
        }
    }
    return 0;
}

/* Buffer access for the numeric stability recorder through the runtime. */
static bool ns_capture(void *context, uint64_t handle, uint64_t offset, uint64_t size, uint8_t **bytes, size_t *length,
    char *message, size_t message_size)
{
    uint8_t *data = NULL;
    size_t total = 0;
    doe_trace_execution execution;
    if (!doe_runtime_read_buffer(context, handle, &data, &total, &execution)) {
        snprintf(message, message_size, "numeric stability capture of buffer %llu: %s: %s", (unsigned long long)handle,
            execution.status_code, execution.message);
        return false;
    }
    uint64_t end = size == UINT64_MAX ? total : offset + size;
    if (offset > total || end > total || end < offset) {
        free(data);
        snprintf(message, message_size, "numeric stability capture of buffer %llu is out of range", (unsigned long long)handle);
        return false;
    }
    *length = (size_t)(end - offset);
    *bytes = malloc(*length ? *length : 1);
    if (*bytes) memcpy(*bytes, data + offset, *length);
    free(data);
    if (!*bytes) snprintf(message, message_size, "out of memory");
    return *bytes != NULL;
}

static bool ns_write(void *context, uint64_t handle, uint64_t offset, const uint32_t *words, size_t count, char *message,
    size_t message_size)
{
    doe_command write = {.kind = DOE_COMMAND_BUFFER_WRITE, .label = "buffer_write"};
    write.as.buffer_write.handle = handle;
    write.as.buffer_write.offset = offset;
    write.as.buffer_write.buffer_size = (uint64_t)count * 4;
    write.as.buffer_write.words = (uint32_t *)words;
    write.as.buffer_write.word_count = count;
    doe_trace_execution execution;
    if (doe_runtime_execute(context, &write, &execution)) return true;
    snprintf(message, message_size, "numeric stability write to buffer %llu: %s: %s", (unsigned long long)handle,
        execution.status_code, execution.message);
    return false;
}

/* The capture a semantic command requested, read after it ran; failures are
 * recorded in the operator record, as doe-zig-runtime does, not fatal. */
static void take_capture(doe_runtime *runtime, const doe_semantic *semantic, doe_operator_capture *capture,
    uint8_t **owned)
{
    *capture = (doe_operator_capture){0};
    *owned = NULL;
    if (!runtime) { capture->error_code = "capture_requires_native_execution"; return; }
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

/* Frees the commands, then the plan step ids and kernel root they point to. */
static void release_input(doe_stream *stream, doe_plan *plan, char *plan_kernel_root)
{
    doe_stream_free(stream);
    doe_plan_free(plan);
    free(plan_kernel_root);
}

/* Delay only the last row so final deferred completion time is attributed to
 * the same row as retained runtime_cli.applyDeferredFlushToTraceRows. */
typedef struct pending_trace {
    bool active;
    const doe_command *command;
    doe_quirk_decision decision;
    doe_trace_execution execution;
    uint64_t seq, timestamp, hash, previous;
} pending_trace;

static bool emit_pending(pending_trace *pending, FILE *rows, const options *options, const doe_trace_identity *identity)
{
    if (!pending->active) return true;
    bool ok = true;
    if (rows) ok = options->plan ?
        doe_trace_write_plan_row(rows, identity, pending->seq, pending->timestamp, pending->hash, pending->previous,
            pending->command, &pending->execution) :
        doe_trace_write_row(rows, identity, pending->seq, pending->timestamp, pending->hash, pending->previous,
            pending->command, &pending->decision, options->execute ? &pending->execution : NULL);
    if (options->trace_stdout && !doe_trace_write_row(stdout, identity, pending->seq, pending->timestamp,
        pending->hash, pending->previous, pending->command, &pending->decision, options->execute ? &pending->execution : NULL)) ok = false;
    if (!options->trace_stdout && !options->trace_jsonl && !options->trace_meta && !options->replay) {
        doe_command applied = *pending->command;
        if (pending->decision.temporary_buffer) {
            applied.as.copy.temporary_buffer = true;
            applied.as.copy.temporary_buffer_alignment = pending->decision.temporary_buffer_alignment;
        }
        if (!cli_write_summary(stdout, pending->seq, &applied, &pending->decision,
                options->execute ? &pending->execution : NULL, identity->backend_id)) ok = false;
    }
    pending->active = false;
    return ok;
}

int main(int argc, char **argv)
{
    options o;
    int parsed = parse_options(argc, argv, &o);
    if (parsed) return parsed == 1 ? 0 : parsed;
    totals t = {.hash = DOE_TRACE_HASH_SEED, .previous = DOE_TRACE_HASH_SEED};
    uint64_t read_start = doe_runtime_monotonic_ns();
    size_t size = 0;
    const char *input = o.plan ? o.plan : o.commands;
    char *text = input ? read_file(input, &size) : strdup(cli_default_commands());
    if (!input) { input = "<embedded>"; size = text ? strlen(text) : 0; }
    if (!text) { fprintf(stderr, "doe-c-runtime: cannot read %s\n", input); return 2; }
    uint64_t parse_start = doe_runtime_monotonic_ns();
    doe_stream stream;
    doe_stream_error stream_error;
    doe_plan plan = {0};
    char *asset_root = o.plan ? doe_plan_asset_root() : NULL;
    bool stream_ok = o.plan ? asset_root && doe_plan_parse(text, size, asset_root, &plan, &stream_error) :
        doe_stream_parse(text, size, &stream, &stream_error);
    free(text);
    free(asset_root);
    t.input_read_ns = parse_start - read_start;
    t.input_parse_ns = doe_runtime_monotonic_ns() - parse_start;
    if (!stream_ok) {
        if (o.plan && !asset_root) snprintf(stream_error.message, sizeof(stream_error.message), "out of memory");
        if (stream_error.command_index == SIZE_MAX) fprintf(stderr, "doe-c-runtime: %s: %s\n", input, stream_error.message);
        else fprintf(stderr, "doe-c-runtime: %s: command %zu: %s\n", input, stream_error.command_index, stream_error.message);
        return 2;
    }
    char *plan_kernel_root = NULL;
    if (o.plan) {
        stream = plan.stream; /* The stream owns the lowered commands; plan keeps its metadata. */
        plan.stream = (doe_stream){0};
        if (strcmp(plan.workload_id, o.workload)) {
            fprintf(stderr, "doe-c-runtime: %s: plan workloadId %s is not --workload %s\n", input, plan.workload_id, o.workload);
            doe_stream_free(&stream);
            doe_plan_free(&plan);
            return 2;
        }
        if (plan.superseded_count)
            fprintf(stderr, "doe-c-runtime: %s: %zu kernel_dispatch steps (first: command %zu) carry semantic, capture or "
                "output_oracle fields of the compatibility stream; the plan step identity replaces them, as in "
                "doe-plan-executor\n", input, plan.superseded_count, plan.first_superseded);
        if (!o.runtime.kernel_root) {
            plan_kernel_root = doe_plan_kernel_root(plan.ir_path);
            o.runtime.kernel_root = plan_kernel_root;
        }
    }
    if (stream.count > UINT64_MAX / o.command_repeat) { release_input(&stream, &plan, plan_kernel_root); fputs("doe-c-runtime: --command-repeat overflows\n", stderr); return 2; }
    if (o.emit_normalized) {
        doe_quirk_set quirks;
        char message[256] = "";
        bool ready = load_cli_quirks(&o, &quirks, message, sizeof(message));
        if (ready && o.replay) {
            size_t size = 0;
            char *text = read_file(o.replay, &size);
            if (!text) snprintf(message, sizeof(message), "cannot read replay %s", o.replay);
            ready = text && cli_validate_replay(text, size, MODULE_NAME, &stream, o.command_repeat, &quirks,
                message, sizeof(message));
            free(text);
        }
        doe_quirks_free(&quirks);
        if (!ready) {
            fprintf(stderr, "doe-c-runtime: %s\n", message);
            release_input(&stream, &plan, plan_kernel_root);
            return 2;
        }
        bool written = true;
        uint64_t seq = 0;
        for (uint64_t pass = 0; written && pass < o.command_repeat; pass++)
            for (size_t i = 0; written && i < stream.count; i++)
                written = cli_write_normalized(stdout, seq++, &stream.commands[i]);
        if (fflush(stdout)) written = false;
        if (!written) fputs("doe-c-runtime: cannot write normalized commands\n", stderr);
        release_input(&stream, &plan, plan_kernel_root);
        return written ? 0 : 1;
    }
    doe_ns_policy ns_policy = {0};
    char *ns_policy_text = NULL;
    bool ns_loaded = false;
    int refused = load_numeric_policy(&o, &stream, &ns_policy, &ns_policy_text, &ns_loaded);
    if (refused) {
        if (ns_loaded) doe_ns_policy_free(&ns_policy);
        free(ns_policy_text);
        release_input(&stream, &plan, plan_kernel_root);
        return refused;
    }
    doe_ns_recorder ns = {0};
    bool ns_enabled = o.execute && ns_loaded && o.trace_meta;
    if (ns_enabled && !doe_ns_recorder_open(&ns, &ns_policy, o.trace_meta)) {
        fprintf(stderr, "doe-c-runtime: out of memory\n");
        doe_ns_policy_free(&ns_policy);
        free(ns_policy_text);
        release_input(&stream, &plan, plan_kernel_root);
        return 1;
    }
    uint64_t command_count = (uint64_t)stream.count * o.command_repeat;
    FILE *rows = o.trace_jsonl ? fopen(o.trace_jsonl, "wb") : NULL;
    if (o.trace_jsonl && !rows) {
        fprintf(stderr, "doe-c-runtime: cannot write %s\n", o.trace_jsonl);
        release_input(&stream, &plan, plan_kernel_root);
        return 2;
    }
    char message[256] = "";
    doe_quirk_set quirks = {.mode = o.quirk_mode};
    if (!load_cli_quirks(&o, &quirks, message, sizeof(message))) {
        fprintf(stderr, "doe-c-runtime: quirks: %s\n", message);
        if (rows) fclose(rows);
        release_input(&stream, &plan, plan_kernel_root);
        return 2;
    }
    /* doe-zig-runtime anchors operator records at the trace meta, else the trace JSONL. */
    /* doe-plan-executor writes no operator records. */
    const char *operator_anchor = o.plan ? NULL : o.trace_meta ? o.trace_meta : o.trace_jsonl;
    doe_operator_recorder operators = {0};
    if (operator_anchor && !doe_operator_recorder_open(&operators, operator_anchor)) {
        fprintf(stderr, "doe-c-runtime: out of memory\n");
        if (rows) fclose(rows);
        doe_quirks_free(&quirks);
        release_input(&stream, &plan, plan_kernel_root);
        return 1;
    }
    uint64_t init_start = doe_runtime_monotonic_ns();
    bool replay_ok = true;
    if (o.replay) {
        size_t replay_size = 0;
        char *replay_text = read_file(o.replay, &replay_size);
        if (!replay_text) snprintf(message, sizeof(message), "cannot read");
        replay_ok = replay_text && cli_validate_replay(replay_text, replay_size, MODULE_NAME, &stream, o.command_repeat,
            &quirks, message, sizeof(message));
        free(replay_text);
        if (!replay_ok) fprintf(stderr, "doe-c-runtime: replay %s: %s\n", o.replay, message);
    }
    doe_runtime *runtime = o.execute && replay_ok ? doe_runtime_open(&o.runtime, message, sizeof(message)) : NULL;
    t.executor_init_ns = o.execute ? doe_runtime_monotonic_ns() - init_start : 0;
    int status = 0;
    if (!replay_ok || (o.execute && !runtime)) {
        if (replay_ok) fprintf(stderr, "doe-c-runtime: %s\n", message);
        status = 1;
    }
    /* Kernel pipelines compile before the command loop, outside execution timing;
     * doe-plan-executor compiles them inside the plan rows instead. */
    uint64_t prewarm_start = doe_runtime_monotonic_ns();
    for (size_t i = 0; runtime && status == 0 && !o.plan && i < stream.count; i++) {
        doe_trace_execution prewarm;
        if (!doe_runtime_prewarm_kernel(runtime, &stream.commands[i], &prewarm))
            fprintf(stderr, "doe-c-runtime: warning: kernel prewarm of command %zu: %s: %s\n", i, prewarm.status_code, prewarm.message);
    }
    t.kernel_prewarm_ns = o.plan || !runtime ? 0 : doe_runtime_monotonic_ns() - prewarm_start;
    const doe_trace_identity identity = {.module = MODULE_NAME, .backend_id = BACKEND_ID};
    const doe_ns_identity ns_identity = {.backend = BACKEND_ID, .vendor = o.vendor, .api = o.api, .family = o.family,
        .driver = o.driver};
    const doe_ns_io ns_io = {.context = runtime, .capture = ns_capture, .write = ns_write};
    bool stopped = false;
    pending_trace pending = {0};
    for (uint64_t pass = 0; (!o.execute || runtime) && status == 0 && !stopped && pass < o.command_repeat; pass++) {
        for (size_t i = 0; i < stream.count && status == 0 && !stopped; i++) {
            if (!emit_pending(&pending, rows, &o, &identity)) { status = 1; break; }
            const doe_command *command = &stream.commands[i];
            const doe_quirk_decision decision = doe_quirks_decide(&quirks, command->kind);
            if (doe_semantic_present(&command->semantic)) t.semantic_tracing = true;
            doe_command applied = *command;
            if (decision.temporary_buffer) {
                applied.as.copy.temporary_buffer = true;
                applied.as.copy.temporary_buffer_alignment = decision.temporary_buffer_alignment;
            }
            doe_trace_execution execution = {0};
            bool ok = !o.execute || doe_runtime_execute(runtime, &applied, &execution);
            uint64_t seq = t.rows;
            uint64_t hash = doe_trace_payload_hash(t.hash, seq, command->label, doe_command_tag(command->kind),
                command->kind == DOE_COMMAND_KERNEL_DISPATCH ? command->as.dispatch.kernel : NULL, &decision,
                &command->semantic);
            uint64_t now = realtime_ns();
            pending = (pending_trace){.active = true, .command = command, .decision = decision, .execution = execution,
                .seq = seq, .timestamp = now, .hash = hash, .previous = t.hash};
            if (operator_anchor && doe_semantic_present(&command->semantic)) {
                doe_operator_capture capture;
                uint8_t *capture_data = NULL;
                if (command->semantic.has_capture) take_capture(runtime, &command->semantic, &capture, &capture_data);
                const doe_operator_input input = {.source_index = seq, .command = command, .execution = o.execute ? &execution : NULL,
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
            /* doe-zig-runtime evaluates after a successful command; a stop route skips the rest. */
            if (ok && ns_enabled) {
                char ns_message[512];
                if (!doe_ns_record(&ns, &ns_io, &ns_identity, command, stream.count - i - 1, &stopped, ns_message,
                        sizeof(ns_message))) {
                    fprintf(stderr, "doe-c-runtime: command %zu: %s\n", i, ns_message);
                    status = 1;
                }
                if (stopped) t.skipped = command_count - t.rows;
            }
            if (ok) { if (o.execute) t.successes++; }
            else {
                t.errors++;
                fprintf(stderr, "doe-c-runtime: command %zu (%s): %s: %s\n", i, command->label,
                    execution.status_code, execution.message);
                status = 1;
            }
        }
    }
    if (runtime) {
        doe_trace_execution flush;
        bool complete = doe_runtime_flush(runtime, &flush);
        if (pending.active) {
            pending.execution.duration_ns += flush.duration_ns;
            pending.execution.submit_wait_ns += flush.submit_wait_ns;
            t.total_ns += flush.duration_ns;
            t.submit_wait_ns += flush.submit_wait_ns;
            if (!complete && pending.execution.ok) {
                pending.execution.ok = false;
                pending.execution.status_code = flush.status_code;
                memcpy(pending.execution.message, flush.message, sizeof(flush.message));
                t.successes--;
                t.errors++;
            }
        }
        if (!complete) {
            fprintf(stderr, "doe-c-runtime: final queue drain: %s: %s\n", flush.status_code, flush.message);
            status = 1;
        }
    }
    if (!emit_pending(&pending, rows, &o, &identity)) status = 1;
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
    if (fflush(stdout) != 0) status = 1;
    if (operator_anchor && !doe_operator_recorder_finish(&operators)) {
        fprintf(stderr, "doe-c-runtime: cannot write %s\n", operators.manifest_path);
        status = 1;
    }
    if (o.trace_meta && !write_meta(o.trace_meta, &o, &t, command_count, runtime ? doe_runtime_adapter_info(runtime) : NULL,
            &operators, o.plan ? plan.plan_sha256 : NULL, runtime, &stream, ns_enabled ? &ns : NULL)) {
        fprintf(stderr, "doe-c-runtime: cannot write %s\n", o.trace_meta);
        status = 1;
    }
    doe_operator_recorder_free(&operators);
    if (ns_enabled) {
        if (ns.receipts && fflush(ns.receipts) != 0) status = 1;
        doe_ns_recorder_free(&ns);
    }
    if (ns_loaded) doe_ns_policy_free(&ns_policy);
    free(ns_policy_text);
    doe_runtime_close(runtime);
    doe_quirks_free(&quirks);
    release_input(&stream, &plan, plan_kernel_root);
    return status;
}
