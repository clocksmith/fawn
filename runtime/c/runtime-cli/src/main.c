/* SPDX-License-Identifier: MIT
 * doe-c-runtime: executes a runtime command stream through the C WebGPU runtime
 * and writes doe-zig-runtime trace v1 artifacts. Arguments follow
 * doe-zig-runtime; every argument or value this runtime does not implement is
 * rejected before any device work. */
#include "execute.h"
#include "oracle.h"
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
#include "host_io.h"
#include <string.h>
#include <time.h>

typedef struct totals {
    uint64_t rows, successes, errors;
    uint64_t gpu_timestamp_ns, gpu_timestamp_attempted, gpu_timestamp_valid;
    uint64_t total_ns, setup_ns, encode_ns, submit_wait_ns, dispatches, submits;
    uint64_t input_read_ns, input_parse_ns, executor_init_ns, kernel_prewarm_ns;
    uint64_t hash, previous;
    doe_oracle_evidence oracle;
    uint64_t matched, blocking, requires_lean, lean_required, skipped;
    bool semantic_tracing;
} totals;

static char *read_file(const char *path, size_t *size)
{
    FILE *file = doe_host_fopen(path, "rb");
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
    char *text = options->quirks ? read_file(options->quirks, &size) : doe_host_strdup(cli_default_quirks());
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
    return doe_host_realtime_ns();
}

static bool write_optional(FILE *out, const char *key, const char *value, bool comma)
{
    if (fprintf(out, "%s\"%s\":", comma ? "," : "", key) < 0) return false;
    return value ? doe_trace_write_string(out, value) : fputs("null", out) >= 0;
}

/* Receipts enumerate immutable source snapshots registered by compilation,
 * never paths reread after execution. Distinct versions remain visible. */
static bool write_shader_receipts(FILE *out, const doe_runtime *runtime)
{
    char *bytes = NULL;
    size_t length = 0;
    doe_host_output output;
    FILE *list = doe_host_output_open(&output);
    if (!list) return false;
    bool ok = fputc('[', list) != EOF, any = false;
    for (size_t i = 0; ok && i < doe_runtime_source_receipt_count(runtime); i++) {
        doe_runtime_source_receipt receipt;
        if (!doe_runtime_source_receipt_at(runtime, i, &receipt)) { ok = false; break; }
        ok = (!any || fputc(',', list) != EOF) && fputs("{\"moduleId\":", list) >= 0 &&
            doe_trace_write_string(list, receipt.module_id) && fputs(",\"sourceKind\":\"wgsl\",\"path\":", list) >= 0 &&
            doe_trace_write_string(list, receipt.path);
        if (ok && receipt.entry_point)
            ok = fputs(",\"entryPoint\":", list) >= 0 && doe_trace_write_string(list, receipt.entry_point);
        ok = ok && fprintf(list, ",\"byteLength\":%zu,\"sha256\":\"%s\"}", receipt.byte_length, receipt.sha256) >= 0;
        any = true;
    }
    const char *public_source = doe_runtime_public_render_source(runtime);
    if (public_source) {
        uint8_t digest[32];
        char hash[65];
        doe_sha256(public_source, strlen(public_source), digest);
        doe_sha256_hex(digest, hash);
        ok = (!any || fputc(',', list) != EOF) && fprintf(list,
            "{\"moduleId\":\"doe-public-render\",\"sourceKind\":\"wgsl\",\"byteLength\":%zu,\"sha256\":\"%s\"}",
            strlen(public_source), hash) >= 0;
        any = true;
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
    ok = doe_host_output_close(&output, &bytes, &length) && ok;
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

static bool encode_meta(FILE *out, const options *o, const totals *t, uint64_t command_count,
    const doe_runtime_adapter *adapter, const doe_operator_recorder *operators, const char *plan_sha256,
    const doe_runtime *runtime, const doe_ns_recorder *numeric)
{
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
        ",\"executionGpuTimestampTotalNs\":%" PRIu64 ",\"executionGpuTimestampAttemptedCount\":%" PRIu64 ",\"executionGpuTimestampValidCount\":%" PRIu64 ","
        "\"semanticTracingEnabled\":%s,\"semanticOpRowCount\":%" PRIu64 ",\"semanticCaptureCount\":%" PRIu64
        ",\"semanticReproCount\":%" PRIu64 ","
        "\"hash\":\"0x%" PRIx64 "\",\"previousHash\":\"0x%" PRIx64 "\",\"outputOracleCount\":%" PRIu64
        ",\"outputOracleMatchedCount\":%" PRIu64 ",\"outputOracleFailedCount\":%" PRIu64,
        seq_max, t->rows, command_count, t->matched, t->blocking, t->requires_lean, t->lean_required,
        o->execute || o->dry_run ? t->rows : 0, t->successes, t->errors, t->skipped, t->total_ns, t->setup_ns, t->encode_ns,
        t->submit_wait_ns, t->dispatches, t->submits, t->input_read_ns, t->input_parse_ns, t->executor_init_ns,
        t->kernel_prewarm_ns, t->gpu_timestamp_ns, t->gpu_timestamp_attempted, t->gpu_timestamp_valid, t->semantic_tracing ? "true" : "false", operators ? operators->row_count : 0,
        operators ? operators->capture_count : 0, operators ? operators->repro_count : 0,
        t->hash, t->previous, t->oracle.count, t->oracle.matched_count, t->oracle.failed_count) >= 0;
    if (ok && (o->execute || o->dry_run)) ok = fputs(",\"executionBackend\":\"" BACKEND_ID "\",\"backendId\":\"" BACKEND_ID "\"", out) >= 0;
    if (ok && o->execute) ok = fputs(",\"backendSelectionReason\":\"fixed_backend\",\"fallbackUsed\":false", out) >= 0;
    if (ok && o->backend_lane) ok = write_optional(out, "backendLane", o->backend_lane, true);
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
        ,
        o->runtime.defer_queue_sync ? "deferred" : "per-command",
        o->runtime.queue_wait == DOE_WAIT_PROCESS_EVENTS ? "process-events" : "wait-any",
        o->quirk_mode == DOE_QUIRK_OFF ? "off" : o->quirk_mode == DOE_QUIRK_ACTIVE ? "active" : "trace") >= 0;
    ok = ok && doe_runtime_write_pipeline_cache(out, runtime);
    ok = ok && doe_runtime_write_device_policy(out, runtime);
    if (ok && t->oracle.count) {
        const doe_oracle_evidence *evidence = &t->oracle;
        const doe_output_oracle *oracle = &evidence->descriptor;
        ok = write_optional(out, "outputOracleKind", doe_output_oracle_kind_name(oracle->kind), true) &&
            write_optional(out, "outputOracleReferenceId", oracle->reference_id, true) &&
            write_optional(out, "outputOracleExpectedSha256", oracle->expected_sha256, true) &&
            write_optional(out, "outputOracleActualSha256", evidence->has_actual_sha256 ? evidence->actual_sha256 : NULL, true) &&
            write_optional(out, "outputOracleReferenceClass", doe_output_oracle_reference_class_name(oracle->reference_class), true) &&
            fprintf(out, ",\"outputOracleComparedValueCount\":%" PRIu64 ",\"outputOracleMismatchCount\":%" PRIu64
                ",\"outputOracleMaxAbsoluteError\":%.17g,\"outputOracleMaxRelativeError\":%.17g"
                ",\"outputOracleAbsoluteTolerance\":%.9g,\"outputOracleRelativeTolerance\":%.9g",
                evidence->compared_value_count, evidence->mismatch_count, evidence->max_absolute_error,
                evidence->max_relative_error, (double)oracle->absolute_tolerance, (double)oracle->relative_tolerance) >= 0;
        if (ok && oracle->kind == DOE_ORACLE_FLOAT32_TOLERANCE)
            ok = write_optional(out, "outputOracleReferenceSha256", oracle->expected_sha256, true) &&
                write_optional(out, "outputOracleReferencePath", oracle->reference_path, true);
    }
    if (ok && runtime) ok = write_shader_receipts(out, runtime);
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
    return ok; /* Close status is appended after the runtime is actually retired. */
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
        if (!stream->commands[i].numeric && profile && !doe_ns_capture_plan_supported(plan)) {
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
        doe_quirks_apply(&pending->decision, &applied);
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
    totals t = {.hash = DOE_TRACE_HASH_SEED, .previous = DOE_TRACE_HASH_SEED, .semantic_tracing = o.plan != NULL};
    uint64_t read_start = doe_runtime_monotonic_ns();
    size_t size = 0;
    const char *input = o.plan ? o.plan : o.commands;
    char *text = input ? read_file(input, &size) : doe_host_strdup(cli_default_commands());
    if (!input) { input = "<embedded>"; size = text ? strlen(text) : 0; }
    if (!text) { fprintf(stderr, "doe-c-runtime: cannot read %s\n", input); return 2; }
    uint64_t parse_start = doe_runtime_monotonic_ns();
    doe_stream stream;
    doe_stream_error stream_error = {.command_index = SIZE_MAX};
    doe_plan plan = {0};
    char *asset_root = o.plan && !o.dry_run ? doe_plan_asset_root() : NULL;
    bool stream_ok = o.plan ? (o.dry_run ? doe_plan_parse_dry_run(text, size, &plan, &stream_error) :
        asset_root && doe_plan_parse(text, size, asset_root, &plan, &stream_error)) :
        doe_stream_parse(text, size, &stream, &stream_error);
    bool asset_root_failed = o.plan && !o.dry_run && !asset_root;
    free(text);
    free(asset_root);
    t.input_read_ns = parse_start - read_start;
    t.input_parse_ns = doe_runtime_monotonic_ns() - parse_start;
    if (!stream_ok) {
        if (asset_root_failed) snprintf(stream_error.message, sizeof(stream_error.message), "out of memory");
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
    char oracle_message[256] = "";
    if (!doe_oracle_preflight(&stream, o.validate_oracles, oracle_message, sizeof(oracle_message))) {
        fprintf(stderr, "doe-c-runtime: output_oracle: %s\n", oracle_message);
        release_input(&stream, &plan, plan_kernel_root);
        return 2;
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
    /* Validate inputs before opening any output. Replay may name the same file
     * as trace-jsonl (or an alias of it); opening output first destroys input. */
    char message[256] = "";
    doe_quirk_set quirks = {.mode = o.quirk_mode};
    if (!load_cli_quirks(&o, &quirks, message, sizeof(message))) {
        fprintf(stderr, "doe-c-runtime: quirks: %s\n", message);
        release_input(&stream, &plan, plan_kernel_root);
        return 2;
    }
    if (o.replay) {
        size_t replay_size = 0;
        char *replay_text = read_file(o.replay, &replay_size);
        if (!replay_text) snprintf(message, sizeof(message), "cannot read");
        bool valid = replay_text && cli_validate_replay(replay_text, replay_size, MODULE_NAME, &stream,
            o.command_repeat, &quirks, message, sizeof(message));
        free(replay_text);
        if (!valid) {
            fprintf(stderr, "doe-c-runtime: replay %s: %s\n", o.replay, message);
            doe_quirks_free(&quirks);
            release_input(&stream, &plan, plan_kernel_root);
            return 1;
        }
    }
    doe_ns_policy ns_policy = {0};
    char *ns_policy_text = NULL;
    bool ns_loaded = false;
    int refused = load_numeric_policy(&o, &stream, &ns_policy, &ns_policy_text, &ns_loaded);
    if (refused) {
        doe_quirks_free(&quirks);
        if (ns_loaded) doe_ns_policy_free(&ns_policy);
        free(ns_policy_text);
        release_input(&stream, &plan, plan_kernel_root);
        return refused;
    }
    doe_ns_recorder ns = {0};
    bool ns_enabled = o.execute && ns_loaded && o.trace_meta;
    if (ns_enabled && !doe_ns_recorder_open(&ns, &ns_policy, o.trace_meta)) {
        doe_quirks_free(&quirks);
        fprintf(stderr, "doe-c-runtime: out of memory\n");
        doe_ns_policy_free(&ns_policy);
        free(ns_policy_text);
        release_input(&stream, &plan, plan_kernel_root);
        return 1;
    }
    uint64_t command_count = (uint64_t)stream.count * o.command_repeat;
    FILE *rows = o.trace_jsonl ? doe_host_fopen(o.trace_jsonl, "wb") : NULL;
    if (o.trace_jsonl && !rows) {
        doe_quirks_free(&quirks);
        doe_ns_recorder_free(&ns);
        if (ns_loaded) doe_ns_policy_free(&ns_policy);
        free(ns_policy_text);
        fprintf(stderr, "doe-c-runtime: cannot write %s\n", o.trace_jsonl);
        release_input(&stream, &plan, plan_kernel_root);
        return 2;
    }
    /* doe-zig-runtime anchors operator records at the trace meta, else the trace JSONL. */
    /* doe-plan-executor writes no operator records. */
    const char *operator_anchor = o.plan ? NULL : o.trace_meta ? o.trace_meta : o.trace_jsonl;
    doe_operator_recorder operators = {0};
    if (operator_anchor && !doe_operator_recorder_open(&operators, operator_anchor)) {
        doe_ns_recorder_free(&ns);
        if (ns_loaded) doe_ns_policy_free(&ns_policy);
        free(ns_policy_text);
        fprintf(stderr, "doe-c-runtime: out of memory\n");
        if (rows) fclose(rows);
        doe_quirks_free(&quirks);
        release_input(&stream, &plan, plan_kernel_root);
        return 1;
    }
    uint64_t init_start = doe_runtime_monotonic_ns();
    doe_runtime *runtime = o.execute ? doe_runtime_open(&o.runtime, message, sizeof(message)) : NULL;
    t.executor_init_ns = o.execute ? doe_runtime_monotonic_ns() - init_start : 0;
    int status = 0;
    if (o.execute && !runtime) {
        fprintf(stderr, "doe-c-runtime: %s\n", message);
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
            doe_quirks_apply(&decision, &applied);
            doe_trace_execution execution = {0};
            if (o.dry_run) execution = (doe_trace_execution){.ok = true, .status_code = "dry_run",
                .dispatch_count = command->kind == DOE_COMMAND_KERNEL_DISPATCH ? 1 : 0};
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
            t.gpu_timestamp_ns += execution.gpu_timestamp_ns;
            t.gpu_timestamp_attempted += execution.gpu_timestamp_attempted;
            t.gpu_timestamp_valid += execution.gpu_timestamp_valid;
            /* doe-zig-runtime evaluates after a successful command; a stop route skips the rest. */
            if (ok && ns_enabled) {
                char ns_message[512];
                if (!doe_ns_record(&ns, &ns_io, &ns_identity, command, stream.commands + i + 1, stream.count - i - 1, &stopped, ns_message,
                        sizeof(ns_message))) {
                    fprintf(stderr, "doe-c-runtime: command %zu: %s\n", i, ns_message);
                    status = 1;
                }
                if (stopped) t.skipped = command_count - t.rows;
            }
            if (ok) { if (o.execute || o.dry_run) t.successes++; }
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
    if (runtime && status == 0 && o.validate_oracles &&
        !doe_oracle_validate(runtime, &o.runtime, &stream, &t.oracle, message, sizeof(message))) {
        fprintf(stderr, "doe-c-runtime: output_oracle: %s\n", message);
        status = 1;
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
        FILE *out = doe_host_fopen(o.readback_paths[i], "wb");
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
    if (runtime) doe_runtime_flush_pipeline_cache(runtime);
    /* Encode all borrowed runtime identities before close, but publish no final
     * metadata until its actual completion state is known. */
    doe_host_output meta = {0};
    FILE *meta_out = o.trace_meta ? doe_host_output_open(&meta) : NULL;
    bool meta_ok = !o.trace_meta || (meta_out && encode_meta(meta_out, &o, &t, command_count,
        runtime ? doe_runtime_adapter_info(runtime) : NULL, &operators, o.plan ? plan.plan_sha256 : NULL,
        runtime, ns_enabled ? &ns : NULL));
    bool had_runtime = runtime != NULL;
    bool gpu_unknown = doe_runtime_gpu_completion_unknown(runtime);
    bool closed = doe_runtime_close(runtime);
    if (!closed) status = 1;
    if (meta_ok && meta_out) {
        meta_ok = fprintf(meta_out, ",\"runtimeCloseComplete\":%s,\"gpuCompletionUnknown\":%s}\n",
            had_runtime ? (closed ? "true" : "false") : "null",
            gpu_unknown || !closed || t.oracle.completion_unknown ? "true" : "false") >= 0;
        char *bytes = NULL;
        size_t length = 0;
        meta_ok = doe_host_output_close(&meta, &bytes, &length) && meta_ok;
        FILE *out = meta_ok ? doe_host_fopen(o.trace_meta, "wb") : NULL;
        meta_ok = meta_ok && out && fwrite(bytes, 1, length, out) == length;
        if (out && fclose(out) != 0) meta_ok = false;
        free(bytes);
    } else if (meta_out) doe_host_output_discard(&meta);
    if (!meta_ok) {
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
    doe_quirks_free(&quirks);
    release_input(&stream, &plan, plan_kernel_root);
    return status;
}
