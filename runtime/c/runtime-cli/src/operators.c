/* SPDX-License-Identifier: MIT */
#include "operators.h"
#include "sha256.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_PREFIX "[\n"
#define MANIFEST_SUFFIX "\n]\n"

static char *concat(const char *a, const char *b)
{
    size_t a_size = strlen(a), b_size = strlen(b);
    char *out = malloc(a_size + b_size + 1);
    if (!out) return NULL;
    memcpy(out, a, a_size);
    memcpy(out + a_size, b, b_size + 1);
    return out;
}

/* <anchor>.opNNNN<suffix>, the index zero-padded to at least four digits. */
static char *op_path(const char *anchor, uint64_t index, const char *suffix)
{
    int size = snprintf(NULL, 0, "%s.op%04" PRIu64 "%s", anchor, index, suffix);
    char *out = size < 0 ? NULL : malloc((size_t)size + 1);
    if (out) snprintf(out, (size_t)size + 1, "%s.op%04" PRIu64 "%s", anchor, index, suffix);
    return out;
}

bool doe_operator_recorder_open(doe_operator_recorder *recorder, const char *anchor)
{
    *recorder = (doe_operator_recorder){0};
    recorder->anchor = concat(anchor, "");
    recorder->manifest_path = concat(anchor, ".operators.json");
    recorder->manifest = open_memstream(&recorder->manifest_bytes, &recorder->manifest_size);
    if (!recorder->anchor || !recorder->manifest_path || !recorder->manifest ||
        fputs(MANIFEST_PREFIX, recorder->manifest) < 0) {
        doe_operator_recorder_free(recorder);
        return false;
    }
    return true;
}

static bool write_profile(FILE *out, const doe_operator_input *in)
{
    bool ok = fputs(",\"profile\":{\"vendor\":", out) >= 0 && doe_trace_write_string(out, in->vendor) &&
        fputs(",\"api\":", out) >= 0 && doe_trace_write_string(out, in->api) && fputs(",\"deviceFamily\":", out) >= 0;
    ok = ok && (in->family ? doe_trace_write_string(out, in->family) : fputs("null", out) >= 0);
    return ok && fputs(",\"driver\":", out) >= 0 && doe_trace_write_string(out, in->driver) && fputc('}', out) != EOF;
}

/* The execution object: this runtime takes no GPU timestamps and reports no
 * lane, policy, shader artifact or queue family fields. */
static bool write_execution(FILE *out, const doe_operator_input *in)
{
    const doe_trace_execution *e = in->execution;
    if (!e) return fputs(",\"execution\":null", out) >= 0;
    bool ok = fputs(",\"execution\":{\"backend\":", out) >= 0 && doe_trace_write_string(out, in->backend_id) &&
        fprintf(out, ",\"status\":\"%s\",\"statusCode\":", e->ok ? "ok" : "error") >= 0 &&
        doe_trace_write_string(out, e->message);
    return ok && fprintf(out, ",\"timings\":{\"durationNs\":%" PRIu64 ",\"setupNs\":%" PRIu64 ",\"encodeNs\":%" PRIu64
        ",\"submitWaitNs\":%" PRIu64 ",\"dispatchCount\":%" PRIu64 ",\"gpuTimestampNs\":0,\"gpuTimestampAttempted\":false,"
        "\"gpuTimestampValid\":false}}", e->duration_ns, e->setup_ns, e->encode_ns, e->submit_wait_ns,
        e->dispatch_count) >= 0;
}

/* Semantic identity exists only on kernel_dispatch commands here. */
static bool write_command_shape(FILE *out, const doe_command *command)
{
    if (command->kind != DOE_COMMAND_KERNEL_DISPATCH) return fputs(",\"commandShape\":{}", out) >= 0;
    const doe_kernel_binding *bindings = command->as.dispatch.bindings;
    size_t binding_count = command->as.dispatch.binding_count;
    bool ok = fprintf(out, ",\"commandShape\":{\"dispatchGeometry\":{\"x\":%" PRIu32 ",\"y\":%" PRIu32 ",\"z\":%" PRIu32
        "},\"repeat\":%" PRIu64, command->as.dispatch.x, command->as.dispatch.y, command->as.dispatch.z,
        command->as.dispatch.repeat) >= 0;
    if (ok && binding_count) {
        uint64_t bytes = 0;
        for (size_t i = 0; i < binding_count; i++) {
            uint64_t size = bindings[i].buffer_size;
            if (bindings[i].resource != DOE_RESOURCE_BUFFER || size == UINT64_MAX) continue; /* Whole or not a buffer. */
            bytes = bytes > UINT64_MAX - size ? UINT64_MAX : bytes + size;
        }
        ok = fprintf(out, ",\"bindingCount\":%zu,\"bindingBufferBytes\":%" PRIu64, binding_count, bytes) >= 0;
    }
    return ok && fputc('}', out) != EOF;
}

static bool write_command_head(FILE *out, const doe_operator_input *in)
{
    bool ok = fputs(",\"command\":", out) >= 0 && doe_trace_write_string(out, in->command->label);
    if (ok && in->command->kind == DOE_COMMAND_KERNEL_DISPATCH)
        ok = fputs(",\"kernel\":", out) >= 0 && doe_trace_write_string(out, in->command->as.dispatch.kernel);
    return ok && doe_trace_write_semantic(out, &in->command->semantic);
}

static bool write_file(const char *path, const void *data, size_t size)
{
    FILE *out = fopen(path, "wb");
    if (!out) return false;
    bool ok = fwrite(data, 1, size, out) == size;
    return fclose(out) == 0 && ok;
}

/* The command alone in normalized form with its semantic and capture fields:
 * doe-zig-runtime's printNormalizedCommand output plus its appended extras. */
static bool write_repro_commands(const char *path, const doe_command *command)
{
    FILE *out = fopen(path, "wb");
    if (!out) return false;
    const doe_semantic *s = &command->semantic;
    bool ok = fputs("[\n{\"seq\":0,\"kind\":", out) >= 0 && doe_trace_write_string(out, command->label);
    if (ok && command->kind == DOE_COMMAND_KERNEL_DISPATCH) {
        ok = doe_trace_write_dispatch_fields(out, command);
    }
    ok = ok && doe_trace_write_semantic(out, s);
    if (ok && s->has_capture)
        ok = fprintf(out, ",\"captureBufferHandle\":%" PRIu64 ",\"captureOffset\":%" PRIu64 ",\"captureSize\":%" PRIu64,
            s->capture_handle, s->capture_offset, s->capture_size) >= 0;
    ok = ok && fputs("}\n]\n", out) >= 0;
    return fclose(out) == 0 && ok;
}

static bool write_repro_meta(const char *path, const char *commands_path, const doe_operator_input *in)
{
    FILE *out = fopen(path, "wb");
    if (!out) return false;
    bool ok = fputs("{\"schemaVersion\":1,\"reproMode\":\"structural_same_device_backend\",\"bitwise\":false,"
        "\"commandsPath\":", out) >= 0 && doe_trace_write_string(out, commands_path) && write_command_head(out, in) &&
        write_profile(out, in) && write_execution(out, in) && write_command_shape(out, in->command) &&
        fputc('}', out) != EOF;
    return fclose(out) == 0 && ok;
}

static bool fail_path(char *message, size_t message_size, const char *path)
{
    snprintf(message, message_size, "cannot write %s", path ? path : "an operator artifact (out of memory)");
    return false;
}

bool doe_operator_record(doe_operator_recorder *recorder, const doe_operator_input *in, char *message,
    size_t message_size)
{
    const doe_semantic *s = &in->command->semantic;
    if (!doe_semantic_present(s)) return true;
    static const doe_operator_capture NOT_TAKEN = {.error_code = "capture_not_taken"};
    const doe_operator_capture *capture = in->capture ? in->capture : &NOT_TAKEN;
    char *capture_path = NULL;
    char *commands_path = op_path(recorder->anchor, in->source_index, ".repro.commands.json");
    char *meta_path = op_path(recorder->anchor, in->source_index, ".repro.meta.json");
    char capture_sha256[65] = "";
    bool ok = commands_path && meta_path;
    if (!ok) fail_path(message, message_size, NULL);
    if (ok && s->has_capture && capture->ok) {
        capture_path = op_path(recorder->anchor, in->source_index, ".capture.bin");
        ok = capture_path && write_file(capture_path, capture->data, capture->size);
        if (!ok) fail_path(message, message_size, capture_path);
        uint8_t digest[32];
        doe_sha256(capture->data, capture->size, digest);
        doe_sha256_hex(digest, capture_sha256);
    }
    if (ok && !write_repro_commands(commands_path, in->command)) ok = fail_path(message, message_size, commands_path);
    if (ok && !write_repro_meta(meta_path, commands_path, in)) ok = fail_path(message, message_size, meta_path);
    FILE *out = recorder->manifest;
    if (ok) {
        ok = (!recorder->row_count || fputs(",\n", out) >= 0) &&
            fprintf(out, "{\"schemaVersion\":1,\"sourceIndex\":%" PRIu64, in->source_index) >= 0 &&
            write_command_head(out, in) &&
            fprintf(out, ",\"trace\":{\"hash\":\"0x%" PRIx64 "\",\"previousHash\":\"0x%" PRIx64 "\"", in->hash,
                in->previous) >= 0;
        if (ok && in->trace_meta_path)
            ok = fputs(",\"traceMetaPath\":", out) >= 0 && doe_trace_write_string(out, in->trace_meta_path);
        /* No shader artifact manifest is written by this runtime. */
        ok = ok && fputc('}', out) != EOF && write_profile(out, in) && write_execution(out, in) &&
            write_command_shape(out, in->command) && fputs(",\"shaderArtifacts\":{},\"capture\":", out) >= 0;
        if (ok && !s->has_capture) ok = fputs("null", out) >= 0;
        else if (ok) {
            ok = fprintf(out, "{\"bufferHandle\":%" PRIu64 ",\"offset\":%" PRIu64 ",\"size\":%" PRIu64 ",\"status\":\"%s\"",
                s->capture_handle, s->capture_offset, s->capture_size,
                capture->ok ? "ok" : !in->execution ? "unsupported" : "error") >= 0;
            if (ok && capture->ok)
                ok = fputs(",\"path\":", out) >= 0 && doe_trace_write_string(out, capture_path) &&
                    fprintf(out, ",\"sha256\":\"%s\"", capture_sha256) >= 0;
            else if (ok)
                ok = fputs(",\"errorCode\":", out) >= 0 && doe_trace_write_string(out, capture->error_code);
            ok = ok && fputc('}', out) != EOF;
        }
        ok = ok && fputs(",\"repro\":{\"commandsPath\":", out) >= 0 && doe_trace_write_string(out, commands_path) &&
            fputs(",\"metaPath\":", out) >= 0 && doe_trace_write_string(out, meta_path) &&
            fputs(",\"rerunMode\":\"structural_same_device_backend\",\"bitwise\":false}}", out) >= 0;
        if (!ok) fail_path(message, message_size, recorder->manifest_path);
    }
    if (ok) {
        recorder->row_count++;
        recorder->repro_count++;
        if (s->has_capture && capture->ok) recorder->capture_count++;
    }
    free(capture_path);
    free(commands_path);
    free(meta_path);
    return ok;
}

bool doe_operator_recorder_finish(doe_operator_recorder *recorder)
{
    if (!recorder->manifest) return false;
    bool ok = fputs(MANIFEST_SUFFIX, recorder->manifest) >= 0;
    ok = fclose(recorder->manifest) == 0 && ok;
    recorder->manifest = NULL;
    if (!ok || !recorder->manifest_bytes) return false;
    uint8_t digest[32];
    doe_sha256(recorder->manifest_bytes, recorder->manifest_size, digest);
    doe_sha256_hex(digest, recorder->manifest_sha256);
    return write_file(recorder->manifest_path, recorder->manifest_bytes, recorder->manifest_size);
}

void doe_operator_recorder_free(doe_operator_recorder *recorder)
{
    if (recorder->manifest) fclose(recorder->manifest);
    free(recorder->manifest_bytes);
    free(recorder->anchor);
    free(recorder->manifest_path);
    *recorder = (doe_operator_recorder){0};
}
