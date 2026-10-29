/* SPDX-License-Identifier: MIT */
#include "windows_internal.h"
#ifdef _WIN32

struct doe_dx_wait_record {
    doe_dx_native_context *context;
    doe_dx_wait_record *next;
    HANDLE event;
    uint64_t value;
    bool active, registered;
};

/* Only the existing context owner mutates this native event cleanup set.
 * Active waiters keep their record and the context's fence/device alive via
 * the common object obligation. No node contains a command or resource lease. */
static doe_dx_status collect(doe_dx_native_context *context, bool drain, doe_dx_error *error)
{
    if (drain) {
        for (doe_dx_wait_record *record = context->waits; record; record = record->next)
            if (record->active) return doe_dx_fail(DOE_DX_BUSY, 0, "wait.active", error);
    }
    doe_dx_wait_record **link = &context->waits;
    while (*link) {
        doe_dx_wait_record *record = *link;
        if (record->active) { link = &record->next; continue; }
        if (record->registered) {
            DWORD waited = WaitForSingleObject(record->event, drain ? INFINITE : 0);
            if (waited == WAIT_TIMEOUT && !drain) { link = &record->next; continue; }
            if (waited != WAIT_OBJECT_0)
                return doe_dx_fail(DOE_DX_COMPLETION_UNKNOWN, (int32_t)GetLastError(),
                    "WaitForSingleObject.lease_cleanup", error);
            record->registered = false;
        }
        if (!CloseHandle(record->event))
            return doe_dx_fail(DOE_DX_NATIVE_FAILURE, (int32_t)GetLastError(),
                "CloseHandle.lease", error);
        *link = record->next;
        free(record);
    }
    return doe_dx_fail(DOE_DX_OK, 0, "wait.collect", error);
}

doe_dx_status doe_dx_native_progress_close(doe_dx_native_context *context)
{
    /* The common owner already requires every submitted command terminal.
     * Device removal signals monitored fences too. Drain notification delivery
     * before closing any remaining registered event; failure retains ownership. */
    return collect(context, true, NULL);
}

doe_dx_status doe_dx_native_progress_hold(doe_dx_native_context *context,
    uint64_t value, doe_dx_wait_record **out, doe_dx_error *error)
{
    doe_dx_status status = collect(context, false, error);
    if (status != DOE_DX_OK) return status;
    for (doe_dx_wait_record *record = context->waits; record; record = record->next) {
        if (!record->active && record->value == value) {
            record->active = true;
            *out = record;
            return doe_dx_fail(DOE_DX_OK, 0, "wait.reuse", error);
        }
    }
    doe_dx_wait_record *record = calloc(1, sizeof(*record));
    if (!record) return doe_dx_fail(DOE_DX_OUT_OF_MEMORY, 0, "wait.allocate", error);
    record->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!record->event) {
        DWORD failure = GetLastError();
        free(record);
        return doe_dx_fail(DOE_DX_NATIVE_FAILURE, (int32_t)failure, "CreateEventW.lease", error);
    }
    record->context = context;
    record->value = value;
    HRESULT result = ID3D12Fence_SetEventOnCompletion(context->fence, value, record->event);
    if (FAILED(result)) {
        if (CloseHandle(record->event)) free(record);
        else { record->next = context->waits; context->waits = record; }
        return doe_dx_hresult(result, "ID3D12Fence.SetEventOnCompletion.lease", error);
    }
    record->active = record->registered = true;
    record->next = context->waits;
    context->waits = record;
    *out = record;
    return doe_dx_fail(DOE_DX_OK, 0, "wait.hold", error);
}

doe_dx_status doe_dx_native_progress_block(const doe_dx_wait_record *record,
    uint32_t wait_ms, doe_dx_error *error)
{
    uint64_t completed = 0;
    doe_dx_status status = doe_dx_native_completed(record->context, &completed, error);
    if (status != DOE_DX_OK) return status;
    if (completed >= record->value) return doe_dx_fail(DOE_DX_OK, 0, "wait.complete", error);
    DWORD waited = WaitForSingleObject(record->event, wait_ms);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_TIMEOUT)
        return doe_dx_fail(DOE_DX_NATIVE_FAILURE, (int32_t)GetLastError(),
            "WaitForSingleObject.lease", error);
    status = doe_dx_native_completed(record->context, &completed, error);
    if (status != DOE_DX_OK) return status;
    if (completed >= record->value) return doe_dx_fail(DOE_DX_OK, 0, "wait.complete", error);
    return doe_dx_fail(waited == WAIT_TIMEOUT ? DOE_DX_PENDING : DOE_DX_COMPLETION_UNKNOWN,
        0, "wait.not_complete", error);
}

doe_dx_status doe_dx_native_progress_unhold(doe_dx_native_context *context,
    doe_dx_wait_record *record, doe_dx_error *error)
{
    doe_dx_wait_record *linked = context->waits;
    while (linked && linked != record) linked = linked->next;
    if (!linked || !linked->active)
        return doe_dx_fail(DOE_DX_INVALID_ARGUMENT, 0, "wait.unhold", error);
    linked->active = false;
    return collect(context, false, error);
}
#endif
