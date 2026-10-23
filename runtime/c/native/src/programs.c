/* SPDX-License-Identifier: MIT
 * Doe compute-program extension over the existing owners: the resource owner
 * claims and revalidates commands, the pending owner submits and settles, and
 * the Vulkan owner keeps one reusable recording for gpu-recorded programs. */
#include "internal.h"
#include "doe_native_programs.h"
#include <stdlib.h>

#ifndef DOE_COMPUTE_PROGRAM_CONTRACT
#error "DOE_COMPUTE_PROGRAM_CONTRACT comes from config/compute-program.schema.json"
#endif

typedef struct native_program {
    native_device *device;           /* One ordinary device lease. */
    doe_resource_commands *commands; /* Program-claimed reference. */
    doe_vk_commands *recording;      /* gpu-recorded only; NULL means host replay. */
} native_program;

uint32_t doeNativeComputeProgramContractVersion(void)
{
    return DOE_COMPUTE_PROGRAM_CONTRACT;
}

uint32_t doeNativeComputeProgramSupported(void)
{
#if defined(__linux__)
    return 1;
#else
    return 0; /* Reusable GPU recordings require Vulkan; host replay is separate. */
#endif
}

uint32_t doeNativeComputeProgramTimestampNanoseconds(void *handle, double *period_ns, uint32_t *valid_bits)
{
    const native_device *device = handle;
    if (!device || !period_ns || !valid_bits || device->backend.kind != WGPUBackendType_Vulkan ||
            !native_feature_enabled(device, WGPUFeatureName_TimestampQuery)) return 0;
    *period_ns = 1.0;
    *valid_bits = 64;
    return 1;
}

/* One recording must not depend on per-submission texture, render or
 * indirect-guard state, nor on query state it did not reset itself; those
 * programs use host replay or ordinary submission. */
static bool gpu_recordable(const doe_resource_commands *commands)
{
    size_t count = 0;
    const doe_resource_command *records = doe_resource_commands_borrow(commands, &count);
    for (size_t i = 0; i < count; ++i) {
        doe_resource_command_kind kind = records[i].kind;
        /* Timestamp writes (including compute-pass timestampWrites) lower to
         * reset+write of each index; the Vulkan owner seals only recordings
         * whose every written or resolved index is reset by the recording. */
        if (kind != DOE_COMMAND_COPY_BUFFER && kind != DOE_COMMAND_CLEAR_BUFFER &&
                kind != DOE_COMMAND_DISPATCH && kind != DOE_COMMAND_WRITE_TIMESTAMP &&
                kind != DOE_COMMAND_RESOLVE_QUERY) return false;
    }
    return count != 0;
}

static native_program *prepare(void *queue, void *handle, bool host_replay)
{
    native_device *device = queue;
    doe_resource_commands *commands = handle;
    if (!device || !commands) return NULL;
    const char *message = NULL;
    WGPUErrorType type = WGPUErrorType_Validation;
    if (doe_resource_commands_device(commands) != device) message = "compute program: commands belong to another device";
    else if (!host_replay && device->backend.kind != WGPUBackendType_Vulkan)
        message = "compute program: GPU recording requires the Vulkan backend";
    else if (!host_replay && !gpu_recordable(commands))
        message = "compute program: GPU recording admits buffer copies, clears, direct dispatches and self-contained timestamp queries only";
    native_program *program = message ? NULL : calloc(1, sizeof(*program));
    if (!message && !program) {
        type = WGPUErrorType_OutOfMemory;
        message = "compute program: allocation failed";
    }
    if (message) {
        native_error(device, type, message);
        return NULL;
    }
    native_device_retain(device);
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
    doe_resource_status ownership = failure.status == NATIVE_OK ?
        doe_resource_commands_reserve_program(commands) : DOE_RESOURCE_STATE;
    bool reserved = ownership == DOE_RESOURCE_OK;
    if (failure.status == NATIVE_OK && !reserved)
        failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (failure.status == NATIVE_OK && !host_replay) {
        doe_vk_error detail = {0};
        failure = native_failure_vulkan(doe_vk_commands_create_reusable(device->backend.value.vulkan.context,
            &program->recording, &detail), &detail);
        native_command_handle native = {.kind=WGPUBackendType_Vulkan,.value.vulkan=program->recording};
        if (failure.status == NATIVE_OK) failure = native_record_resource_commands(&native, commands);
        /* Sealed: nothing dispatched, buffers free for host writes before the first run. */
        if (failure.status == NATIVE_OK)
            failure = native_failure_vulkan(doe_vk_commands_seal(program->recording, &detail), &detail);
    }
    if (failure.status == NATIVE_OK) {
        ownership = doe_resource_commands_publish_program(commands);
        if (ownership != DOE_RESOURCE_OK) failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    }
    if (failure.status != NATIVE_OK) {
        /* Unpublished native artifacts may borrow the command's resources.
         * Release them before rolling its reservation back to ordinary use. */
        if (program->recording && doe_vk_commands_release(program->recording) != DOE_VK_OK) abort();
        program->recording = NULL;
        /* The constructor's device lease prevents finalization or a progress
         * join during this recursive resource drop under the device mutex. */
        if (reserved && doe_resource_commands_abort_program(commands) != DOE_RESOURCE_OK) abort();
    }
    native_unlock(device);
    if (failure.status == NATIVE_OK) {
        program->device = device;
        program->commands = commands;
        return program;
    }
    char text[NATIVE_FAILURE_MESSAGE_CAPACITY];
    native_failure_format(&failure, host_replay ? "compute program host replay preparation failed" :
        "compute program GPU recording preparation failed", text);
    native_error(device, ownership != DOE_RESOURCE_OK && ownership != DOE_RESOURCE_STATE ?
        native_resource_error(ownership) : native_result_error(failure.status), text);
    free(program);
    native_device_release(device);
    return NULL;
}

void *doeNativeComputeProgramPrepare(void *queue, void *commands)
{
    return prepare(queue, commands, false);
}

void *doeNativeComputeProgramPrepareHostReplay(void *queue, void *commands)
{
    return prepare(queue, commands, true);
}

uint32_t doeNativeComputeProgramSubmit(void *handle)
{
    native_program *program = handle;
    if (!program) return 0;
    native_device *device = program->device;
    native_lock(device);
    native_failure failure = native_failure_status(native_device_result(device));
    if (failure.status == NATIVE_OK) failure = native_settle_pending(device).failure;
    doe_resource_status replay = failure.status == NATIVE_OK ?
        doe_resource_commands_begin_replay(program->commands) : DOE_RESOURCE_STATE;
    if (failure.status == NATIVE_OK && replay != DOE_RESOURCE_OK)
        failure = native_failure_status(NATIVE_INVALID_ARGUMENT);
    if (failure.status == NATIVE_OK) {
        native_command_handle native = {0};
        if (program->recording) {
            failure = native_failure_status(native_from_vulkan(doe_vk_commands_retain(program->recording)));
            native = (native_command_handle){.kind=WGPUBackendType_Vulkan,.value.vulkan=program->recording};
        } else {
            failure = native_command_create(&device->backend, &native);
            if (failure.status == NATIVE_OK) failure = native_record_resource_commands(&native, program->commands);
            if (failure.status != NATIVE_OK && native_command_release(&native) != NATIVE_OK) abort();
        }
        if (failure.status == NATIVE_OK) failure = native_submit_program(device, native, program->commands);
        else if (doe_resource_commands_settle_replay(program->commands, DOE_RESOURCE_EXECUTION_FAILED) !=
                DOE_RESOURCE_EXECUTION_FAILED) abort(); /* Never dispatched. */
    }
    native_unlock(device);
    if (failure.status != NATIVE_OK) {
        char text[NATIVE_FAILURE_MESSAGE_CAPACITY];
        native_failure_format(&failure, "compute program submission failed; unresolved work stays retained", text);
        native_error(device, replay != DOE_RESOURCE_OK && replay != DOE_RESOURCE_STATE ?
            native_resource_error(replay) : native_result_error(failure.status), text);
    }
    return failure.status == NATIVE_OK;
}

void doeNativeComputeProgramRelease(void *handle)
{
    native_program *program = handle;
    if (!program) return;
    native_device *device = program->device;
    native_lock(device);
    /* An unresolved submission keeps its own replay and recording holders. */
    if (doe_resource_commands_release_program(program->commands) != DOE_RESOURCE_OK) abort();
    if (program->recording && doe_vk_commands_drop(program->recording) != DOE_VK_OK) abort();
    native_unlock(device);
    free(program);
    native_device_release(device); /* Device lease held; no owner lock remains. */
}
