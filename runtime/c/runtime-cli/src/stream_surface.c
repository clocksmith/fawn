/* SPDX-License-Identifier: MIT */
#include "stream_internal.h"
#include <string.h>
#include "host_io.h"

static bool surface_number(const doe_json_value *object, const char *a, const char *b, const char *c,
    uint32_t *out, size_t index, doe_stream_error *error)
{
    bool ambiguous = false;
    const doe_json_value *value = stream_one_of(object, a, b, c, &ambiguous);
    uint64_t number;
    if (ambiguous || !value || !doe_json_as_u64(value, UINT32_MAX, &number) || !number)
        return stream_reject(error, index, "surface %s requires one nonzero u32 value", a);
    *out = (uint32_t)number;
    return true;
}

bool stream_parse_surface(const doe_json_value *object, doe_command *command, size_t index, doe_stream_error *error)
{
    static const char *const basic[] = {"kind", "command", "command_kind", "handle", "surface_handle", "surfaceHandle",
        "resource_handle", "resourceHandle", NULL};
    static const char *const configure[] = {"kind", "command", "command_kind", "handle", "surface_handle", "surfaceHandle",
        "resource_handle", "resourceHandle", "width", "target_width", "targetWidth", "height", "target_height", "targetHeight",
        "format", "target_format", "targetFormat", "usage", "alpha_mode", "alphaMode", "present_mode", "presentMode",
        "tone_mapping_mode", "toneMappingMode", "desired_maximum_frame_latency", "desiredMaximumFrameLatency", NULL};
    if (!stream_only_members(object, command->kind == DOE_COMMAND_SURFACE_CONFIGURE ? configure : basic, index, "surface", error))
        return false;
    const doe_json_value *handle = NULL;
    for (size_t i = 3; basic[i]; i++) {
        const doe_json_value *candidate = doe_json_member_value(object, basic[i]);
        if (candidate && handle) return stream_reject(error, index, "surface handle is named more than once");
        if (candidate) handle = candidate;
    }
    doe_surface *surface = &command->as.surface;
    if (!handle || !doe_json_as_u64(handle, UINT64_MAX, &surface->handle) || !surface->handle)
        return stream_reject(error, index, "a nonzero surface handle is required");
    if (command->kind != DOE_COMMAND_SURFACE_CONFIGURE) return true;
    if (!surface_number(object, "width", "target_width", "targetWidth", &surface->width, index, error) ||
        !surface_number(object, "height", "target_height", "targetHeight", &surface->height, index, error)) return false;
    bool ambiguous = false;
    const doe_json_value *format = stream_one_of(object, "format", "target_format", "targetFormat", &ambiguous);
    surface->format = DOE_FORMAT_RGBA8_UNORM;
    if (ambiguous || (format && (format->kind != DOE_JSON_STRING || !stream_texture_format(format->string, &surface->format))))
        return stream_reject(error, index, "unsupported or ambiguous surface format");
    if (!stream_read_u64(object, "usage", NULL, UINT64_MAX, false, 16, &surface->usage, index, error)) return false;
    if (!surface->usage) surface->usage = 16;
    const char *alpha = NULL, *present = NULL, *tone = NULL;
    if (!stream_read_string(object, "alpha_mode", "alphaMode", &alpha, index, error) ||
        !stream_read_string(object, "present_mode", "presentMode", &present, index, error) ||
        !stream_read_string(object, "tone_mapping_mode", "toneMappingMode", &tone, index, error)) return false;
    const char *const alphas[] = {"auto", "opaque", "premultiplied", "unpremultiplied", "inherit"};
    surface->alpha_mode = 0;
    if (alpha) {
        while (surface->alpha_mode < 5 && doe_host_casecmp(alpha, alphas[surface->alpha_mode])) surface->alpha_mode++;
        if (surface->alpha_mode == 5) return stream_reject(error, index, "unknown surface alpha mode");
    }
    surface->present_mode = 1;
    if (present) {
        if (!doe_host_casecmp(present, "fifo")) surface->present_mode = 1;
        else if (!doe_host_casecmp(present, "fifo-relaxed") || !doe_host_casecmp(present, "fifo_relaxed")) surface->present_mode = 2;
        else if (!doe_host_casecmp(present, "immediate")) surface->present_mode = 3;
        else if (!doe_host_casecmp(present, "mailbox")) surface->present_mode = 4;
        else return stream_reject(error, index, "unknown surface present mode");
    }
    uint64_t latency;
    if (!stream_read_u64(object, "desired_maximum_frame_latency", "desiredMaximumFrameLatency", UINT32_MAX, false, 2,
        &latency, index, error)) return false;
    /* These retained canvas hints have no representation in pinned webgpu.h. */
    if ((tone && doe_host_casecmp(tone, "standard")) || latency != 2)
        return stream_reject(error, index, "public WebGPU surfaces support standard tone mapping and default frame latency only");
    return true;
}
