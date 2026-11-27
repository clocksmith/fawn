/* SPDX-License-Identifier: MIT
 * Opt-in runtime receipt writer (runtime_receipt.h). Lines are bounded,
 * formatted on the stack and appended with one write; no owner state, lock or
 * WebGPU result depends on them. Bytes outside printable ASCII in a recorded
 * string are written as one \u00XX escape per byte, so every line is valid
 * JSON and the original bytes are recoverable. */
#include "runtime_receipt.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define RECEIPT_LINE_BYTES 8192u
#define RECEIPT_FORMAT_VERSION 1u

static atomic_uint_fast64_t receipt_sequence;

typedef struct receipt_line {
    char bytes[RECEIPT_LINE_BYTES];
    size_t length;
    bool overflow;
} receipt_line;

static const char *receipt_path(void)
{
    const char *path = getenv(NATIVE_RECEIPT_ENVIRONMENT);
    return path && *path ? path : NULL;
}

bool native_receipt_enabled(void)
{
    return receipt_path() != NULL;
}

static void put_bytes(receipt_line *line, const char *bytes, size_t size)
{
    if (line->overflow || size > RECEIPT_LINE_BYTES - line->length) {
        line->overflow = true;
        return;
    }
    memcpy(line->bytes + line->length, bytes, size);
    line->length += size;
}

static void put_text(receipt_line *line, const char *text)
{
    put_bytes(line, text, strlen(text));
}

static void put_unsigned(receipt_line *line, uint64_t value)
{
    char digits[24];
    const int written = snprintf(digits, sizeof(digits), "%llu", (unsigned long long)value);
    if (written < 0 || (size_t)written >= sizeof(digits)) line->overflow = true;
    else put_bytes(line, digits, (size_t)written);
}

static void put_escaped(receipt_line *line, const char *bytes, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    put_text(line, "\"");
    for (size_t i = 0; i < size; ++i) {
        const unsigned char byte = (unsigned char)bytes[i];
        if (byte == '"' || byte == '\\') {
            const char escape[2] = {'\\', (char)byte};
            put_bytes(line, escape, sizeof(escape));
        } else if (byte >= 0x20 && byte < 0x7f) {
            put_bytes(line, (const char *)&bytes[i], 1);
        } else {
            const char escape[6] = {'\\', 'u', '0', '0', hex[byte >> 4], hex[byte & 15]};
            put_bytes(line, escape, sizeof(escape));
        }
    }
    put_text(line, "\"");
}

/* A string field with its full byte length; at most NATIVE_RECEIPT_LABEL_BYTES
 * are copied. A null view with nonzero length is recorded as absent data. */
static void put_string_field(receipt_line *line, const char *name, WGPUStringView view)
{
    size_t size = 0;
    if (view.data) size = view.length == WGPU_STRLEN ? strlen(view.data) : view.length;
    const size_t copied = size < NATIVE_RECEIPT_LABEL_BYTES ? size : NATIVE_RECEIPT_LABEL_BYTES;
    put_text(line, ",\"");
    put_text(line, name);
    put_text(line, "\":");
    put_escaped(line, view.data ? view.data : "", copied);
    put_text(line, ",\"");
    put_text(line, name);
    put_text(line, "_bytes\":");
    put_unsigned(line, size);
    if (copied != size) {
        put_text(line, ",\"");
        put_text(line, name);
        put_text(line, "_truncated\":true");
    }
}

static void put_header(receipt_line *line, const char *event)
{
    /* Relaxed suffices: the counter only orders lines within this process. */
    const uint64_t sequence = atomic_fetch_add_explicit(&receipt_sequence, 1, memory_order_relaxed) + 1;
    put_text(line, "{\"doe_runtime_receipt\":");
    put_unsigned(line, RECEIPT_FORMAT_VERSION);
    put_text(line, ",\"seq\":");
    put_unsigned(line, sequence);
    put_text(line, ",\"pid\":");
#if defined(_WIN32)
    put_unsigned(line, (uint64_t)_getpid());
#else
    put_unsigned(line, (uint64_t)getpid());
    put_text(line, ",\"ppid\":");
    put_unsigned(line, (uint64_t)getppid());
#endif
    put_text(line, ",\"event\":\"");
    put_text(line, event);
    put_text(line, "\"");
}

static void report_failure(const char *path, const char *what, int code)
{
    fprintf(stderr, "doe_runtime_receipt_failed: %s %s: %s\n", what, path, strerror(code));
}

static void emit(const char *path, receipt_line *line)
{
    put_text(line, "}\n");
    if (line->overflow) {
        /* Unreachable with the bounded fields above; never write a partial line. */
        report_failure(path, "line overflow", EOVERFLOW);
        return;
    }
#if defined(_WIN32)
    const int descriptor = _open(path, _O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY | _O_NOINHERIT,
        _S_IREAD | _S_IWRITE);
#else
    const int descriptor = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
#endif
    if (descriptor < 0) {
        report_failure(path, "open", errno);
        return;
    }
#if defined(_WIN32)
    const int written = _write(descriptor, line->bytes, (unsigned)line->length);
    const bool complete = written >= 0 && (size_t)written == line->length;
#else
    ssize_t written;
    do written = write(descriptor, line->bytes, line->length);
    while (written < 0 && errno == EINTR);
    const bool complete = written >= 0 && (size_t)written == line->length;
#endif
    const int write_error = written < 0 ? errno : EIO;
    if (!complete) report_failure(path, "write", write_error);
#if defined(_WIN32)
    const int closed = _close(descriptor);
#else
    const int closed = close(descriptor);
#endif
    if (closed != 0 && complete) report_failure(path, "close", errno);
}

void native_receipt_lifecycle_event(native_receipt_lifecycle event)
{
    const char *path = receipt_path();
    if (!path) return;
    receipt_line line = {.length = 0};
    put_header(&line, event == NATIVE_RECEIPT_INSTANCE_CREATED ? "instance_created" : "device_created");
    emit(path, &line);
}

void native_receipt_adapter(const WGPUAdapterInfo *info)
{
    const char *path = receipt_path();
    if (!path || !info) return;
    receipt_line line = {.length = 0};
    put_header(&line, "adapter_returned");
    put_text(&line, ",\"backend_type\":");
    put_unsigned(&line, (uint64_t)info->backendType);
    put_text(&line, ",\"adapter_type\":");
    put_unsigned(&line, (uint64_t)info->adapterType);
    put_text(&line, ",\"vendor_id\":");
    put_unsigned(&line, info->vendorID);
    put_text(&line, ",\"device_id\":");
    put_unsigned(&line, info->deviceID);
    put_string_field(&line, "vendor", info->vendor);
    put_string_field(&line, "architecture", info->architecture);
    put_string_field(&line, "device", info->device);
    put_string_field(&line, "description", info->description);
    emit(path, &line);
}

static const char *debug_event(native_receipt_scope scope, bool marker)
{
    switch (scope) {
    case NATIVE_RECEIPT_COMMAND_ENCODER:
        return marker ? "command_encoder_insert_debug_marker" : "command_encoder_push_debug_group";
    case NATIVE_RECEIPT_COMPUTE_PASS:
        return marker ? "compute_pass_insert_debug_marker" : "compute_pass_push_debug_group";
    case NATIVE_RECEIPT_RENDER_PASS:
        return marker ? "render_pass_insert_debug_marker" : "render_pass_push_debug_group";
    case NATIVE_RECEIPT_RENDER_BUNDLE:
        return marker ? "render_bundle_insert_debug_marker" : "render_bundle_push_debug_group";
    }
    return "unknown_debug_scope";
}

void native_receipt_debug(native_receipt_scope scope, bool marker, WGPUStringView label, bool accepted)
{
    const char *path = receipt_path();
    if (!path) return;
    receipt_line line = {.length = 0};
    put_header(&line, debug_event(scope, marker));
    put_string_field(&line, "label", label);
    put_text(&line, accepted ? ",\"accepted\":true" : ",\"accepted\":false");
    emit(path, &line);
}

void native_receipt_label(WGPUStringView label, bool accepted)
{
    const char *path = receipt_path();
    if (!path) return;
    receipt_line line = {.length = 0};
    put_header(&line, "object_label");
    put_string_field(&line, "label", label);
    put_text(&line, accepted ? ",\"accepted\":true" : ",\"accepted\":false");
    emit(path, &line);
}

void native_receipt_snapshot(const char *event, bool accepted, const DoeNativeSnapshotLedger *ledger,
    uint64_t resources, const char *detail)
{
    const char *path = receipt_path();
    if (!path || !event) return;
    receipt_line line = {.length = 0};
    put_header(&line, event);
    put_text(&line, accepted ? ",\"accepted\":true" : ",\"accepted\":false");
    if (resources) {
        put_text(&line, ",\"resources\":");
        put_unsigned(&line, resources);
    }
    if (ledger) {
        static const char *const names[] = {"declared_bytes", "branches_begun", "branches_ended",
            "last_prepare_bytes", "total_prepare_bytes", "last_prepare_copies", "last_marked_units",
            "last_marked_bytes", "prepare_submits", "branch_submits", "slot_views"};
        const uint64_t values[] = {ledger->declaredBytes, ledger->branchesBegun, ledger->branchesEnded,
            ledger->lastPrepareBytes, ledger->totalPrepareBytes, ledger->lastPrepareCopies,
            ledger->lastMarkedUnits, ledger->lastMarkedBytes, ledger->prepareSubmits,
            ledger->branchSubmits, ledger->slotViews};
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            put_text(&line, ",\"");
            put_text(&line, names[i]);
            put_text(&line, "\":");
            put_unsigned(&line, values[i]);
        }
    }
    if (detail) put_string_field(&line, "detail", (WGPUStringView){detail, strlen(detail)});
    emit(path, &line);
}
