/* SPDX-License-Identifier: MIT
 * Opt-in runtime receipt: a positive record, written only by this provider, of
 * the public WebGPU events it actually handled. Unset or empty
 * DOE_RUNTIME_RECEIPT leaves every operation unchanged. */
#ifndef DOE_NATIVE_RUNTIME_RECEIPT_H
#define DOE_NATIVE_RUNTIME_RECEIPT_H
#include "webgpu.h"
#include <stdbool.h>
#include <stddef.h>

#define NATIVE_RECEIPT_ENVIRONMENT "DOE_RUNTIME_RECEIPT"
/* Label bytes copied into one line; the full length is still recorded. */
#define NATIVE_RECEIPT_LABEL_BYTES 256u

typedef enum native_receipt_scope {
    NATIVE_RECEIPT_COMMAND_ENCODER,
    NATIVE_RECEIPT_COMPUTE_PASS,
    NATIVE_RECEIPT_RENDER_PASS,
    NATIVE_RECEIPT_RENDER_BUNDLE
} native_receipt_scope;

typedef enum native_receipt_lifecycle {
    NATIVE_RECEIPT_INSTANCE_CREATED,
    NATIVE_RECEIPT_DEVICE_CREATED
} native_receipt_lifecycle;

/* True when DOE_RUNTIME_RECEIPT names a file. Read on every call, so a caller
 * may skip preparing an event's data when it is false. */
bool native_receipt_enabled(void);

/* Each call appends at most one JSON line with one write(2) on an O_APPEND
 * descriptor opened and closed for that line, the process id, the parent
 * process id (POSIX) and a process-wide monotonic sequence number. Callers hold
 * no owner lock. A file that cannot be opened or fully written is reported on
 * stderr; the WebGPU operation itself is never changed. */
void native_receipt_lifecycle_event(native_receipt_lifecycle event);
/* The info of an adapter returned with Success status; borrowed for the call. */
void native_receipt_adapter(const WGPUAdapterInfo *info);
/* A pushDebugGroup (marker false) or insertDebugMarker (marker true) label
 * received for the scope. accepted is the recording owner's verdict. */
void native_receipt_debug(native_receipt_scope scope, bool marker, WGPUStringView label, bool accepted);
/* An object label received through a descriptor or SetLabel. */
void native_receipt_label(WGPUStringView label, bool accepted);
#endif
