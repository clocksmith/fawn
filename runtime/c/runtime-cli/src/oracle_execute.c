/* SPDX-License-Identifier: MIT
 * Validation contexts never contribute to timed execution counters. */
#include "oracle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool same_adapter(const doe_runtime_adapter *a, const doe_runtime_adapter *b)
{
    return a && b && a->vendor_id == b->vendor_id && a->device_id == b->device_id &&
        a->driver_version == b->driver_version && !strcmp(a->backend, b->backend) &&
        !strcmp(a->adapter_type, b->adapter_type) && !strcmp(a->device, b->device);
}

static bool same_sources(const doe_runtime *timed, const doe_runtime *graph)
{
    if (doe_runtime_source_receipt_count(timed) != doe_runtime_source_receipt_count(graph)) return false;
    for (size_t i = 0; i < doe_runtime_source_receipt_count(graph); i++) {
        doe_runtime_source_receipt candidate;
        if (!doe_runtime_source_receipt_at(graph, i, &candidate)) return false;
        bool found = false;
        for (size_t j = 0; j < doe_runtime_source_receipt_count(timed); j++) {
            doe_runtime_source_receipt original;
            if (!doe_runtime_source_receipt_at(timed, j, &original)) return false;
            if (!strcmp(candidate.path, original.path) && !strcmp(candidate.entry_point, original.entry_point) &&
                candidate.byte_length == original.byte_length && !strcmp(candidate.sha256, original.sha256)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static const doe_kernel_binding *target(const doe_command *command, const doe_output_oracle *oracle)
{
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *binding = &command->as.dispatch.bindings[i];
        if (binding->group == oracle->group && binding->binding == oracle->binding && binding->resource == DOE_RESOURCE_BUFFER)
            return binding;
    }
    return NULL;
}

bool doe_oracle_validate(doe_runtime *timed_runtime, const doe_runtime_options *options,
    const doe_stream *stream, doe_oracle_evidence *evidence, char *message, size_t capacity)
{
    *evidence = (doe_oracle_evidence){0};
    bool has_graph = false;
    for (size_t i = 0; i < stream->count; i++) {
        const doe_command *command = &stream->commands[i];
        if (command->kind != DOE_COMMAND_KERNEL_DISPATCH || !command->as.dispatch.oracle) continue;
        doe_output_oracle oracle;
        if (!doe_output_oracle_parse(command->as.dispatch.oracle, &oracle, message, capacity)) return false;
        has_graph |= oracle.scope == DOE_ORACLE_COMMAND_GRAPH;
    }
    doe_runtime *graph = NULL;
    bool ok = true;
    if (has_graph) {
        doe_runtime_options validation = *options;
        validation.timestamp_mode = DOE_TIMESTAMP_OFF;
        validation.defer_queue_sync = false;
        graph = doe_runtime_open(&validation, message, capacity);
        if (!graph) return false;
        if (!same_adapter(doe_runtime_adapter_info(timed_runtime), doe_runtime_adapter_info(graph))) {
            snprintf(message, capacity, "oracle graph selected a different adapter from timed execution");
            ok = false;
        }
        for (size_t i = 0; ok && i < stream->count; i++) {
            doe_trace_execution execution;
            if (!doe_runtime_prewarm_kernel(graph, &stream->commands[i], &execution)) {
                snprintf(message, capacity, "oracle pipeline prewarm: %s: %s", execution.status_code, execution.message);
                ok = false;
            }
        }
        if (ok && !same_sources(timed_runtime, graph)) {
            snprintf(message, capacity, "oracle compiled source differs from timed pipeline source");
            ok = false;
        }
    }
    for (size_t i = 0; ok && i < stream->count; i++) {
        const doe_command *command = &stream->commands[i];
        bool annotated = command->kind == DOE_COMMAND_KERNEL_DISPATCH && command->as.dispatch.oracle;
        doe_output_oracle oracle = {0};
        if (annotated && !doe_output_oracle_parse(command->as.dispatch.oracle, &oracle, message, capacity)) {
            ok = false;
            break;
        }
        doe_trace_execution execution = {.ok = true};
        if (graph && (!annotated || oracle.scope == DOE_ORACLE_COMMAND_GRAPH)) {
            doe_command replay = *command;
            if (replay.kind == DOE_COMMAND_KERNEL_DISPATCH) replay.as.dispatch.oracle = NULL;
            if (!doe_runtime_execute(graph, &replay, &execution)) {
                snprintf(message, capacity, "oracle graph command %zu: %s: %s", i, execution.status_code, execution.message);
                ok = false;
                break;
            }
        }
        if (!annotated) continue;
        evidence->count++;
        evidence->descriptor = oracle;
        evidence->has_actual_sha256 = false;
        evidence->actual_sha256[0] = '\0';
        evidence->compared_value_count = 0;
        evidence->mismatch_count = 0;
        evidence->max_absolute_error = 0;
        evidence->max_relative_error = 0;
        bool matches = false;
        if (oracle.scope == DOE_ORACLE_ISOLATED_DISPATCH) {
            ok = doe_runtime_validate_oracle(timed_runtime, command, &oracle, evidence->actual_sha256, &execution);
            if (ok) {
                evidence->has_actual_sha256 = true;
                matches = !strcmp(evidence->actual_sha256, oracle.expected_sha256);
            } else snprintf(message, capacity, "isolated oracle command %zu: %s: %s", i, execution.status_code, execution.message);
        } else {
            const doe_kernel_binding *binding = target(command, &oracle);
            uint8_t *bytes = NULL;
            if (!binding) {
                snprintf(message, capacity, "oracle target binding disappeared at command %zu", i);
                evidence->failed_count++;
                ok = false;
                break;
            }
            ok = doe_runtime_flush(graph, &execution) &&
                doe_runtime_read_buffer_range(graph, binding->handle, binding->buffer_offset,
                    binding->buffer_size, &bytes, &execution);
            if (ok) ok = doe_oracle_compare_bytes(&oracle, bytes, (size_t)binding->buffer_size,
                evidence, &matches, message, capacity);
            else snprintf(message, capacity, "oracle capture command %zu: %s: %s", i, execution.status_code, execution.message);
            free(bytes);
        }
        if (ok && matches) evidence->matched_count++;
        else {
            evidence->failed_count++;
            if (ok) snprintf(message, capacity, "oracle mismatch at command %zu", i);
        }
        if (ok && graph && oracle.scope == DOE_ORACLE_ISOLATED_DISPATCH) {
            doe_command replay = *command;
            replay.as.dispatch.oracle = NULL;
            if (!doe_runtime_execute(graph, &replay, &execution)) {
                snprintf(message, capacity, "oracle graph command %zu: %s: %s", i, execution.status_code, execution.message);
                ok = false;
            }
        }
    }
    evidence->completion_unknown |= doe_runtime_gpu_completion_unknown(graph);
    if (graph && !doe_runtime_close(graph)) {
        evidence->completion_unknown = true;
        if (ok) snprintf(message, capacity, "oracle runtime close is unconfirmed; resources retained");
        ok = false;
    }
    return ok && evidence->failed_count == 0;
}
