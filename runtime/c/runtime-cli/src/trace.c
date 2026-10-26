/* SPDX-License-Identifier: MIT */
#include "trace.h"
#include <inttypes.h>
#include <string.h>

static uint64_t hash_byte(uint64_t value, uint8_t byte)
{
    return (value ^ (uint64_t)byte) * UINT64_C(1099511628211);
}

static uint64_t hash_bytes(uint64_t value, const void *bytes, size_t size)
{
    const uint8_t *cursor = bytes;
    for (size_t i = 0; i < size; i++) value = hash_byte(value, cursor[i]);
    return value;
}

/* Little-endian bytes of the integer, as the Zig runtime hashes them. */
static uint64_t hash_u64(uint64_t value, uint64_t input)
{
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++) bytes[i] = (uint8_t)(input >> (8 * i));
    return hash_bytes(value, bytes, sizeof(bytes));
}

/* An absent string hashes as one zero byte. */
static uint64_t hash_string(uint64_t value, const char *text)
{
    return text ? hash_bytes(value, text, strlen(text)) : hash_byte(value, 0);
}

static const doe_quirk_decision NO_DECISION = {.scope = "none", .safety = "none", .verification = "none",
    .proof = "none", .action = "none"};

/* A present index hashes as its u64 little-endian bytes, an absent one as one zero byte. */
static uint64_t hash_optional_u32(uint64_t value, bool present, uint32_t input)
{
    return present ? hash_u64(value, input) : hash_byte(value, 0);
}

static const doe_semantic NO_SEMANTIC = {0};

uint64_t doe_trace_payload_hash(uint64_t previous, uint64_t seq, const char *label, const char *tag,
    const char *kernel, const doe_quirk_decision *decision, const doe_semantic *semantic)
{
    const doe_quirk_decision *d = decision ? decision : &NO_DECISION;
    const doe_semantic *s = semantic ? semantic : &NO_SEMANTIC;
    uint64_t next = hash_u64(previous, seq);
    next = hash_string(next, label);
    next = hash_string(next, tag);
    next = hash_string(next, kernel);
    next = hash_string(next, d->matched_id);
    next = hash_string(next, d->toggle);
    next = hash_string(next, d->scope);
    next = hash_string(next, d->safety);
    next = hash_string(next, d->verification);
    next = hash_string(next, d->proof);
    next = hash_string(next, d->action);
    next = hash_string(next, s->op_id);
    next = hash_string(next, s->stage);
    next = hash_string(next, s->phase);
    next = hash_optional_u32(next, s->has_token_index, s->token_index);
    next = hash_optional_u32(next, s->has_layer_index, s->layer_index);
    next = hash_string(next, s->execution_plan_hash);
    next = hash_u64(next, d->score);
    next = hash_u64(next, d->matched_count);
    next = hash_byte(next, d->requires_lean ? 1 : 0);
    next = hash_byte(next, d->blocking ? 1 : 0);
    return next;
}

bool doe_trace_write_string(FILE *out, const char *text)
{
    if (fputc('"', out) == EOF) return false;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        int written;
        switch (*c) {
        case '"': written = fputs("\\\"", out); break;
        case '\\': written = fputs("\\\\", out); break;
        case '\n': written = fputs("\\n", out); break;
        case '\r': written = fputs("\\r", out); break;
        case '\t': written = fputs("\\t", out); break;
        default:
            written = *c < 0x20 ? fprintf(out, "\\u%04x", *c) : fputc(*c, out);
            break;
        }
        if (written < 0) return false;
    }
    return fputc('"', out) != EOF;
}

static bool write_semantic_string(FILE *out, const char *key, const char *value)
{
    return !value || (fprintf(out, ",\"%s\":", key) >= 0 && doe_trace_write_string(out, value));
}

bool doe_trace_write_semantic(FILE *out, const doe_semantic *semantic)
{
    return write_semantic_string(out, "semanticOpId", semantic->op_id) &&
        write_semantic_string(out, "semanticStage", semantic->stage) &&
        write_semantic_string(out, "semanticPhase", semantic->phase) &&
        (!semantic->has_token_index || fprintf(out, ",\"semanticTokenIndex\":%" PRIu32, semantic->token_index) >= 0) &&
        (!semantic->has_layer_index || fprintf(out, ",\"semanticLayerIndex\":%" PRIu32, semantic->layer_index) >= 0) &&
        write_semantic_string(out, "semanticExecutionPlanHash", semantic->execution_plan_hash);
}

/* The execution fields that end every row, then the row's closing brace. */
static bool write_execution(FILE *out, const doe_trace_identity *identity, const doe_trace_execution *execution)
{
    if (!execution) return fputs("}\n", out) >= 0;
    bool ok = fputs(",\"executionBackend\":", out) >= 0;
    ok = ok && doe_trace_write_string(out, identity->backend_id) && fputs(",\"backendId\":", out) >= 0 &&
        doe_trace_write_string(out, identity->backend_id);
    ok = ok && fprintf(out, ",\"executionStatus\":\"%s\",\"executionStatusCode\":", execution->ok ? "ok" : "error") >= 0;
    ok = ok && doe_trace_write_string(out, execution->status_code) && fputs(",\"executionStatusMessage\":", out) >= 0 &&
        doe_trace_write_string(out, execution->message);
    if (execution->ok && execution->shader_source_sha256[0])
        ok = ok && fputs(",\"shaderSourceSha256\":", out) >= 0 &&
            doe_trace_write_string(out, execution->shader_source_sha256);
    return ok && fprintf(out, ",\"executionDurationNs\":%" PRIu64 ",\"executionSetupNs\":%" PRIu64
        ",\"executionEncodeNs\":%" PRIu64 ",\"executionSubmitWaitNs\":%" PRIu64 ",\"executionDispatchCount\":%" PRIu64
        ",\"executionSubmitCount\":%" PRIu64 ",\"executionGpuTimestampNs\":%" PRIu64 ",\"executionGpuTimestampAttempted\":%s,"
        "\"executionGpuTimestampValid\":%s}\n",
        execution->duration_ns, execution->setup_ns, execution->encode_ns, execution->submit_wait_ns,
        execution->dispatch_count, execution->submit_count, execution->gpu_timestamp_ns,
        execution->gpu_timestamp_attempted ? "true" : "false", execution->gpu_timestamp_valid ? "true" : "false") >= 0;
}

bool doe_trace_write_row(FILE *out, const doe_trace_identity *identity, uint64_t seq, uint64_t timestamp_ns,
    uint64_t hash, uint64_t previous, const doe_command *command, const doe_quirk_decision *decision,
    const doe_trace_execution *execution)
{
    const doe_quirk_decision *d = decision ? decision : &NO_DECISION;
    bool ok = fprintf(out, "{\"traceVersion\":1,\"module\":") >= 0 && doe_trace_write_string(out, identity->module);
    ok = ok && fprintf(out, ",\"opCode\":\"dispatch\",\"seq\":%" PRIu64 ",\"timestampMonoNs\":%" PRIu64
        ",\"hash\":\"0x%" PRIx64 "\",\"previousHash\":\"0x%" PRIx64 "\",\"command\":",
        seq, timestamp_ns, hash, previous) >= 0;
    ok = ok && doe_trace_write_string(out, command->label);
    if (ok && command->kind == DOE_COMMAND_KERNEL_DISPATCH) {
        ok = fputs(",\"kernel\":", out) >= 0 && doe_trace_write_string(out, command->as.dispatch.kernel);
    }
    ok = ok && doe_trace_write_semantic(out, &command->semantic);
    ok = ok && fputs(",\"matched\":", out) >= 0 && (d->matched_id ? doe_trace_write_string(out, d->matched_id) : fputs("null", out) >= 0);
    ok = ok && fputs(",\"scope\":", out) >= 0 && doe_trace_write_string(out, d->scope);
    ok = ok && fputs(",\"safetyClass\":", out) >= 0 && doe_trace_write_string(out, d->safety);
    ok = ok && fputs(",\"verificationMode\":", out) >= 0 && doe_trace_write_string(out, d->verification);
    ok = ok && fputs(",\"proofLevel\":", out) >= 0 && doe_trace_write_string(out, d->proof);
    ok = ok && fprintf(out, ",\"requiresLean\":%s,\"blocking\":%s,\"score\":%" PRIu64 ",\"matched_count\":%" PRIu64 ",\"action\":",
        d->requires_lean ? "true" : "false", d->blocking ? "true" : "false", d->score, d->matched_count) >= 0;
    ok = ok && doe_trace_write_string(out, d->action) && fputs(",\"toggle\":", out) >= 0 &&
        doe_trace_write_string(out, d->toggle ? d->toggle : "none");
    return ok && write_execution(out, identity, execution);
}

bool doe_trace_write_plan_row(FILE *out, const doe_trace_identity *identity, uint64_t seq, uint64_t timestamp_ns,
    uint64_t hash, uint64_t previous, const doe_command *command, const doe_trace_execution *execution)
{
    bool ok = fputs("{\"traceVersion\":1,\"module\":", out) >= 0 && doe_trace_write_string(out, identity->module) &&
        fputs(",\"opCode\":", out) >= 0 && doe_trace_write_string(out, command->label);
    ok = ok && fprintf(out, ",\"seq\":%" PRIu64 ",\"timestampMonoNs\":%" PRIu64 ",\"hash\":\"0x%" PRIx64
        "\",\"previousHash\":\"0x%" PRIx64 "\",\"command\":", seq, timestamp_ns, hash, previous) >= 0;
    ok = ok && doe_trace_write_string(out, command->label);
    if (ok && command->kind == DOE_COMMAND_KERNEL_DISPATCH)
        ok = fputs(",\"kernel\":", out) >= 0 && doe_trace_write_string(out, command->as.dispatch.kernel);
    return ok && doe_trace_write_semantic(out, &command->semantic) && write_execution(out, identity, execution);
}

/* Shared by retained normalized command output and semantic repro bundles. */
bool doe_trace_write_dispatch_fields(FILE *out, const doe_command *command)
{
    return fputs(",\"kernel\":", out) >= 0 && doe_trace_write_string(out, command->as.dispatch.kernel) &&
        fprintf(out, ",\"x\":%" PRIu32 ",\"y\":%" PRIu32 ",\"z\":%" PRIu32 ",\"repeat\":%" PRIu64
            ",\"repeatSynchronization\":\"%s\"", command->as.dispatch.x, command->as.dispatch.y,
            command->as.dispatch.z, command->as.dispatch.repeat,
            command->as.dispatch.repeat_independent ? "independent" : "dependent") >= 0;
}
