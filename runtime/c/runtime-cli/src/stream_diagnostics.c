/* SPDX-License-Identifier: MIT */
#include "stream_internal.h"
#include <string.h>
#include <strings.h>

static const char *const modes[] = {"pipeline_async", "capability_introspection", "resource_table_immediates",
    "lifecycle_refcount", "pixel_local_storage", "full"};

const char *doe_diagnostic_mode_name(doe_diagnostic_mode mode)
{
    return (unsigned)mode < sizeof(modes) / sizeof(modes[0]) ? modes[mode] : NULL;
}

bool stream_parse_diagnostics(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    static const char *const allowed[] = {"kind", "command", "command_kind", "format", "target_format", "targetFormat",
        "mode", "iterations", "repeat", "feature_policy", "featurePolicy", NULL};
    if (!stream_only_members(object, allowed, index, "async_diagnostics", error)) return false;
    const char *mode = NULL, *policy = NULL;
    if (!stream_read_string(object, "mode", NULL, &mode, index, error) ||
        !stream_read_string(object, "feature_policy", "featurePolicy", &policy, index, error)) return false;
    command->as.diagnostics.mode = DOE_DIAG_PIPELINE;
    if (mode) {
        char normalized[64];
        if (strlen(mode) >= sizeof(normalized)) return stream_reject(error, index, "diagnostic mode is too long");
        for (size_t i = 0; i <= strlen(mode); i++) normalized[i] = mode[i] == '-' ? '_' : mode[i];
        size_t i = 0;
        while (i < sizeof(modes) / sizeof(modes[0]) && strcasecmp(normalized, modes[i])) i++;
        if (i == sizeof(modes) / sizeof(modes[0])) return stream_reject(error, index, "unknown diagnostic mode %s", mode);
        command->as.diagnostics.mode = (doe_diagnostic_mode)i;
    }
    command->as.diagnostics.emulate = policy && (!strcasecmp(policy, "emulate") ||
        !strcasecmp(policy, "emulate_when_unavailable") || !strcasecmp(policy, "emulate-when-unavailable"));
    if (policy && strcasecmp(policy, "strict") && !command->as.diagnostics.emulate)
        return stream_reject(error, index, "unknown diagnostic feature policy %s", policy);
    uint64_t iterations;
    if (!stream_read_u64(object, "iterations", "repeat", UINT32_MAX, false, 1, &iterations, index, error)) return false;
    if (!iterations) return stream_reject(error, index, "diagnostic iterations must be nonzero");
    command->as.diagnostics.iterations = (uint32_t)iterations;
    bool ambiguous = false;
    const doe_json_value *format = stream_one_of(object, "format", "target_format", "targetFormat", &ambiguous);
    if (ambiguous) return stream_reject(error, index, "diagnostic format is named more than once");
    command->as.diagnostics.format = DOE_FORMAT_RGBA8_UNORM;
    if (format && (format->kind != DOE_JSON_STRING || !stream_texture_format(format->string, &command->as.diagnostics.format)))
        return stream_reject(error, index, "unsupported diagnostic target format");
    return true;
}
