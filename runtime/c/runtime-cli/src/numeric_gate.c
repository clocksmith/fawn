/* SPDX-License-Identifier: MIT */
#include "numeric_gate.h"
#include <stdio.h>
#include <string.h>

bool doe_numeric_gate_needed(const doe_stream *stream)
{
    for (size_t i = 0; i < stream->count; i++) {
        const doe_semantic *s = &stream->commands[i].semantic;
        if (stream->commands[i].kind == DOE_COMMAND_KERNEL_DISPATCH && s->op_id && s->stage && s->phase) return true;
    }
    return false;
}

static const char *string_member(const doe_json_value *object, const char *key, bool *valid)
{
    const doe_json_value *value = doe_json_member_value(object, key);
    if (!value || value->kind == DOE_JSON_NULL) return NULL;
    if (value->kind != DOE_JSON_STRING) *valid = false;
    return value->kind == DOE_JSON_STRING ? value->string : NULL;
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* policy.zig resolveAutoDetectProfile: the first enabled profile whose op id
 * equals the command's, whose stage and phase (when the profile names them)
 * equal the command's, and whose kernel basenames include the kernel's. */
static bool profile_matches(const doe_json_value *profile, const doe_command *command, bool *valid)
{
    const doe_json_value *enabled = doe_json_member_value(profile, "enabled");
    const doe_json_value *kernels = doe_json_member_value(profile, "kernelBasenames");
    const char *op_id = string_member(profile, "semanticOpId", valid);
    const char *stage = string_member(profile, "semanticStage", valid);
    const char *phase = string_member(profile, "semanticPhase", valid);
    if (!enabled || enabled->kind != DOE_JSON_BOOL || !op_id || !kernels || kernels->kind != DOE_JSON_ARRAY) {
        *valid = false;
        return false;
    }
    const doe_semantic *s = &command->semantic;
    if (!enabled->boolean || strcmp(op_id, s->op_id)) return false;
    if ((stage && strcmp(stage, s->stage)) || (phase && strcmp(phase, s->phase))) return false;
    const char *kernel = basename_of(command->as.dispatch.kernel);
    for (size_t i = 0; i < kernels->count; i++) {
        if (kernels->items[i]->kind != DOE_JSON_STRING) { *valid = false; return false; }
        if (!strcmp(kernels->items[i]->string, kernel)) return true;
    }
    return false;
}

doe_numeric_gate_result doe_numeric_gate_check(const char *policy_text, size_t policy_size, const doe_stream *stream,
    size_t *command_index, char *profile, size_t profile_size, char *message, size_t message_size)
{
    doe_json_document document;
    doe_json_error error;
    if (doe_json_parse(policy_text, policy_size, &document, &error) != DOE_JSON_OK) {
        snprintf(message, message_size, "not JSON (%s at byte %zu)", doe_json_status_name(error.status), error.offset);
        return DOE_NUMERIC_GATE_INVALID;
    }
    const doe_json_value *profiles = document.root->kind == DOE_JSON_OBJECT ?
        doe_json_member_value(document.root, "autoDetectProfiles") : NULL;
    doe_numeric_gate_result result = DOE_NUMERIC_GATE_CLEAR;
    if (!profiles || profiles->kind != DOE_JSON_ARRAY || !profiles->count) {
        snprintf(message, message_size, "no \"autoDetectProfiles\" array");
        result = DOE_NUMERIC_GATE_INVALID;
    }
    for (size_t i = 0; result == DOE_NUMERIC_GATE_CLEAR && i < stream->count; i++) {
        const doe_command *command = &stream->commands[i];
        const doe_semantic *s = &command->semantic;
        if (command->kind != DOE_COMMAND_KERNEL_DISPATCH || !s->op_id || !s->stage || !s->phase) continue;
        for (size_t j = 0; j < profiles->count; j++) {
            const doe_json_value *entry = profiles->items[j];
            bool valid = entry->kind == DOE_JSON_OBJECT;
            bool matched = valid && profile_matches(entry, command, &valid);
            if (!valid) {
                snprintf(message, message_size, "autoDetectProfiles[%zu] is not a valid profile", j);
                result = DOE_NUMERIC_GATE_INVALID;
                break;
            }
            if (!matched) continue;
            bool id_valid = true;
            const char *id = string_member(entry, "profileId", &id_valid);
            snprintf(profile, profile_size, "%s", id ? id : "(unnamed)");
            *command_index = i;
            result = DOE_NUMERIC_GATE_OBSERVED;
            break;
        }
    }
    doe_json_free(&document);
    return result;
}
