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

uint64_t doe_trace_payload_hash(uint64_t previous, uint64_t seq, const char *label, const char *tag,
    const char *kernel, const doe_quirk_decision *decision)
{
    const doe_quirk_decision *d = decision ? decision : &NO_DECISION;
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
    next = hash_string(next, NULL); /* semantic op id */
    next = hash_string(next, NULL); /* semantic stage */
    next = hash_string(next, NULL); /* semantic phase */
    next = hash_byte(next, 0);      /* token index (absent) */
    next = hash_byte(next, 0);      /* layer index (absent) */
    next = hash_string(next, NULL); /* execution plan hash */
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
    ok = ok && fputs(",\"matched\":", out) >= 0 && (d->matched_id ? doe_trace_write_string(out, d->matched_id) : fputs("null", out) >= 0);
    ok = ok && fputs(",\"scope\":", out) >= 0 && doe_trace_write_string(out, d->scope);
    ok = ok && fputs(",\"safetyClass\":", out) >= 0 && doe_trace_write_string(out, d->safety);
    ok = ok && fputs(",\"verificationMode\":", out) >= 0 && doe_trace_write_string(out, d->verification);
    ok = ok && fputs(",\"proofLevel\":", out) >= 0 && doe_trace_write_string(out, d->proof);
    ok = ok && fprintf(out, ",\"requiresLean\":%s,\"blocking\":%s,\"score\":%" PRIu64 ",\"matched_count\":%" PRIu64 ",\"action\":",
        d->requires_lean ? "true" : "false", d->blocking ? "true" : "false", d->score, d->matched_count) >= 0;
    ok = ok && doe_trace_write_string(out, d->action) && fputs(",\"toggle\":", out) >= 0 &&
        doe_trace_write_string(out, d->toggle ? d->toggle : "none");
    ok = ok && fputs(",\"executionBackend\":", out) >= 0;
    ok = ok && doe_trace_write_string(out, identity->backend_id) && fputs(",\"backendId\":", out) >= 0 &&
        doe_trace_write_string(out, identity->backend_id);
    ok = ok && fprintf(out, ",\"executionStatus\":\"%s\",\"executionStatusCode\":", execution->ok ? "ok" : "error") >= 0;
    ok = ok && doe_trace_write_string(out, execution->status_code) && fputs(",\"executionStatusMessage\":", out) >= 0 &&
        doe_trace_write_string(out, execution->message);
    ok = ok && fprintf(out, ",\"executionDurationNs\":%" PRIu64 ",\"executionSetupNs\":%" PRIu64
        ",\"executionEncodeNs\":%" PRIu64 ",\"executionSubmitWaitNs\":%" PRIu64 ",\"executionDispatchCount\":%" PRIu64
        ",\"executionSubmitCount\":%" PRIu64 ",\"executionGpuTimestampNs\":0,\"executionGpuTimestampAttempted\":false,"
        "\"executionGpuTimestampValid\":false}\n",
        execution->duration_ns, execution->setup_ns, execution->encode_ns, execution->submit_wait_ns,
        execution->dispatch_count, execution->submit_count) >= 0;
    return ok;
}
