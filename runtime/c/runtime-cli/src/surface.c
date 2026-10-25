/* SPDX-License-Identifier: MIT
 * Full surface lifecycle through public WebGPU, with a real platform window.
 * Mirrors full/surface/wgpu_surface_commands.zig ownership and timing boundaries. */
#include "runtime_internal.h"
#include "surface_platform.h"
#include <stdio.h>
#include <stdlib.h>

static surface_entry *find_surface(doe_runtime *runtime, uint64_t handle)
{
    for (size_t i = 0; i < runtime->surface_count; i++)
        if (runtime->surfaces[i].handle == handle) return &runtime->surfaces[i];
    return NULL;
}

static void release_entry(surface_entry *entry)
{
    if (entry->acquired) wgpuTextureRelease(entry->acquired);
    if (entry->configured) wgpuSurfaceUnconfigure(entry->surface);
    wgpuSurfaceRelease(entry->surface);
    surface_host_release(entry->host);
}

void runtime_release_surfaces(doe_runtime *runtime)
{
    for (size_t i = 0; i < runtime->surface_count; i++) release_entry(&runtime->surfaces[i]);
    free(runtime->surfaces);
    runtime->surfaces = NULL;
    runtime->surface_count = runtime->surface_capacity = 0;
}

static bool create_surface(doe_runtime *runtime, uint64_t handle, doe_trace_execution *execution)
{
    if (find_surface(runtime, handle)) return runtime_fail(execution, "surface_exists", "surface handle already exists");
    if (runtime->surface_count == runtime->surface_capacity) {
        if (runtime->surface_capacity > SIZE_MAX / sizeof(surface_entry) / 2)
            return runtime_fail(execution, "out_of_memory", "surface registry capacity overflow");
        size_t capacity = runtime->surface_capacity ? runtime->surface_capacity * 2 : 4;
        surface_entry *entries = realloc(runtime->surfaces, capacity * sizeof(*entries));
        if (!entries) return runtime_fail(execution, "out_of_memory", "surface registry allocation failed");
        runtime->surfaces = entries;
        runtime->surface_capacity = capacity;
    }
    surface_entry entry = {.handle = handle};
    char message[256];
    if (!surface_host_create(runtime->instance, &entry.host, &entry.surface, message, sizeof(message)))
        return runtime_fail(execution, "surface_create_failed", "%s", message);
    runtime->surfaces[runtime->surface_count++] = entry;
    return true;
}

static bool configure_surface(doe_runtime *runtime, surface_entry *entry, const doe_surface *command,
    doe_trace_execution *execution)
{
    char message[256];
    if (!surface_host_resize(entry->host, command->width, command->height, message, sizeof(message)))
        return runtime_fail(execution, "surface_resize_failed", "%s", message);
    if (entry->acquired) {
        wgpuTextureRelease(entry->acquired);
        entry->acquired = NULL;
    }
    WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
    configuration.device = runtime->device;
    configuration.format = runtime_webgpu_format(command->format);
    configuration.width = command->width;
    configuration.height = command->height;
    configuration.usage = (WGPUTextureUsage)command->usage;
    configuration.alphaMode = (WGPUCompositeAlphaMode)command->alpha_mode;
    configuration.presentMode = (WGPUPresentMode)command->present_mode;
    runtime_push_scopes(runtime);
    wgpuSurfaceConfigure(entry->surface, &configuration);
    bool ok = runtime_pop_scopes(runtime, execution);
    if (ok) entry->configured = true;
    return ok;
}

static bool surface_operation(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    if (command->kind == DOE_COMMAND_SURFACE_CREATE) return create_surface(runtime, command->as.surface.handle, execution);
    surface_entry *entry = find_surface(runtime, command->as.surface.handle);
    if (!entry) return runtime_fail(execution, "surface_missing", "surface handle not found");
    switch (command->kind) {
    case DOE_COMMAND_SURFACE_CAPABILITIES: {
        WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
        WGPUStatus status = wgpuSurfaceGetCapabilities(entry->surface, runtime->adapter, &capabilities);
        bool ok = status == WGPUStatus_Success && capabilities.formatCount && capabilities.presentModeCount;
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);
        return ok || runtime_fail(execution, "surface_capabilities_failed", "surface capabilities query failed or is empty");
    }
    case DOE_COMMAND_SURFACE_CONFIGURE: return configure_surface(runtime, entry, &command->as.surface, execution);
    case DOE_COMMAND_SURFACE_ACQUIRE: {
        if (!entry->configured) return runtime_fail(execution, "surface_not_configured", "surface must be configured before acquire");
        if (entry->acquired) return runtime_fail(execution, "surface_already_acquired", "surface already has an acquired texture");
        WGPUSurfaceTexture texture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(entry->surface, &texture);
        if ((texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) || !texture.texture) {
            if (texture.texture) wgpuTextureRelease(texture.texture);
            return runtime_fail(execution, "surface_acquire_failed", "surface acquire returned status %u", (unsigned)texture.status);
        }
        entry->acquired = texture.texture;
        return true;
    }
    case DOE_COMMAND_SURFACE_PRESENT:
        if (!entry->configured) return runtime_fail(execution, "surface_not_configured", "surface must be configured before present");
        if (!entry->acquired) return runtime_fail(execution, "surface_not_acquired", "surface present requires an acquired texture");
        if (wgpuSurfacePresent(entry->surface) != WGPUStatus_Success)
            return runtime_fail(execution, "surface_present_failed", "surface present failed");
        wgpuTextureRelease(entry->acquired);
        entry->acquired = NULL;
        return true;
    case DOE_COMMAND_SURFACE_UNCONFIGURE:
        if (entry->acquired) wgpuTextureRelease(entry->acquired);
        entry->acquired = NULL;
        wgpuSurfaceUnconfigure(entry->surface);
        entry->configured = false;
        return true;
    case DOE_COMMAND_SURFACE_RELEASE:
        release_entry(entry);
        *entry = runtime->surfaces[--runtime->surface_count];
        return true;
    case DOE_COMMAND_SURFACE_CREATE: break;
    case DOE_COMMAND_UPLOAD:
    case DOE_COMMAND_BUFFER_WRITE:
    case DOE_COMMAND_BARRIER:
    case DOE_COMMAND_KERNEL_DISPATCH:
    case DOE_COMMAND_COPY:
    case DOE_COMMAND_TEXTURE_WRITE:
    case DOE_COMMAND_TEXTURE_QUERY:
    case DOE_COMMAND_TEXTURE_DESTROY:
    case DOE_COMMAND_RENDER_DRAW:
    case DOE_COMMAND_RENDER_PASS:
    case DOE_COMMAND_SAMPLER_CREATE:
    case DOE_COMMAND_SAMPLER_DESTROY:
    case DOE_COMMAND_DISPATCH:
    case DOE_COMMAND_DISPATCH_INDIRECT:
    case DOE_COMMAND_DRAW_INDIRECT:
    case DOE_COMMAND_DRAW_INDEXED_INDIRECT:
    case DOE_COMMAND_MAP_ASYNC:
    case DOE_COMMAND_ASYNC_DIAGNOSTICS:
        break; /* This owner accepts only surface commands. */
    }
    return runtime_fail(execution, "surface_command_invalid", "invalid surface command");
}

bool runtime_execute_surface(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    uint64_t start = doe_runtime_monotonic_ns();
    bool ok = surface_operation(runtime, command, execution);
    uint64_t elapsed = doe_runtime_monotonic_ns() - start;
    if (command->kind == DOE_COMMAND_SURFACE_PRESENT) execution->submit_wait_ns = elapsed;
    else execution->encode_ns = elapsed;
    if (ok) snprintf(execution->message, sizeof(execution->message), "%s real_platform_surface timing=cpu_wall_clock comparability=directional",
        doe_command_tag(command->kind));
    return ok;
}
