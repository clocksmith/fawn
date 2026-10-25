/* SPDX-License-Identifier: MIT
 * kernel_dispatch: a WGSL compute kernel from the kernel root, an explicit
 * layout built from the command's bindings, buffers keyed by handle, optional
 * warmup dispatches, then `repeat` dispatches in one compute pass and one
 * submission. */
#include "runtime_internal.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *read_source(const char *path, size_t *size)
{
    struct stat status;
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode)) return NULL;
    if (status.st_size <= 0 || (unsigned long long)status.st_size > MAX_KERNEL_SOURCE_BYTES) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = malloc((size_t)status.st_size + 1);
    size_t read = text ? fread(text, 1, (size_t)status.st_size, file) : 0;
    fclose(file);
    if (!text || read != (size_t)status.st_size) {
        free(text);
        return NULL;
    }
    text[read] = '\0';
    *size = read;
    return text;
}

static char *join(const char *root, const char *name, const char *suffix)
{
    size_t length = strlen(root) + 1 + strlen(name) + strlen(suffix) + 1;
    char *path = malloc(length);
    if (path) snprintf(path, length, "%s/%s%s", root, name, suffix);
    return path;
}

/* The doe-zig-runtime order (wgpu_commands_compute.zig resolveKernelSource):
 * the name as a path, the name under the kernel root, then name.wgsl there. */
static char *resolve_kernel(const doe_runtime *runtime, const char *kernel, char **source, size_t *size)
{
    char *candidates[3] = {0};
    candidates[0] = strdup(kernel);
    const char *root = runtime->options.kernel_root;
    if (root) {
        candidates[1] = join(root, kernel, "");
        size_t length = strlen(kernel);
        if (length < 5 || strcmp(kernel + length - 5, ".wgsl") != 0) candidates[2] = join(root, kernel, ".wgsl");
    }
    char *chosen = NULL;
    for (int i = 0; i < 3; i++) {
        if (!chosen && candidates[i] && (*source = read_source(candidates[i], size))) {
            chosen = candidates[i];
            continue;
        }
        free(candidates[i]);
    }
    return chosen;
}

static WGPUBufferBindingType binding_type(doe_binding_buffer_type type)
{
    switch (type) {
    case DOE_BINDING_UNIFORM: return WGPUBufferBindingType_Uniform;
    case DOE_BINDING_READ_ONLY_STORAGE: return WGPUBufferBindingType_ReadOnlyStorage;
    case DOE_BINDING_STORAGE: return WGPUBufferBindingType_Storage;
    }
    return WGPUBufferBindingType_Undefined;
}

static bool same_signature(const pipeline_entry *entry, const doe_kernel_binding *bindings, size_t count)
{
    if (entry->signature_count != count) return false;
    for (size_t i = 0; i < count; i++) {
        const doe_kernel_binding *a = &entry->signature[i], *b = &bindings[i];
        if (a->group != b->group || a->binding != b->binding || a->buffer_type != b->buffer_type) return false;
    }
    return true;
}

static pipeline_entry *find_pipeline(doe_runtime *runtime, const char *path, const char *entry_point,
    const doe_kernel_binding *bindings, size_t count)
{
    for (size_t i = 0; i < runtime->pipeline_count; i++) {
        pipeline_entry *entry = &runtime->pipelines[i];
        if (!strcmp(entry->source_path, path) && !strcmp(entry->entry_point, entry_point) &&
            same_signature(entry, bindings, count)) return entry;
    }
    return NULL;
}

static void release_pipeline(pipeline_entry *entry)
{
    if (entry->pipeline) wgpuComputePipelineRelease(entry->pipeline);
    for (uint32_t g = 0; g < entry->group_count; g++) if (entry->groups[g]) wgpuBindGroupLayoutRelease(entry->groups[g]);
    free(entry->source_path);
    free(entry->entry_point);
    free(entry->signature);
    *entry = (pipeline_entry){0};
}

/* Builds the module, explicit group layouts and pipeline under one error
 * scope set; any reported error discards the whole entry. */
static bool build_pipeline(doe_runtime *runtime, pipeline_entry *entry, const char *source, size_t source_size,
    const doe_kernel_binding *bindings, size_t count, doe_trace_execution *execution)
{
    uint32_t groups = 0;
    for (size_t i = 0; i < count; i++) {
        if (bindings[i].group >= MAX_GROUPS)
            return runtime_fail(execution, "kernel_binding_group", "binding group %u is not below %d", bindings[i].group, MAX_GROUPS);
        if (bindings[i].group + 1 > groups) groups = bindings[i].group + 1;
    }
    runtime_push_scopes(runtime);
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = (WGPUStringView){source, source_size};
    WGPUShaderModuleDescriptor module_descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_descriptor.nextInChain = &wgsl.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(runtime->device, &module_descriptor);
    WGPUBindGroupLayoutEntry layout_entries[MAX_GROUP_BINDINGS];
    for (uint32_t g = 0; g < groups; g++) {
        size_t used = 0;
        for (size_t i = 0; i < count && used < MAX_GROUP_BINDINGS; i++) {
            if (bindings[i].group != g) continue;
            WGPUBindGroupLayoutEntry layout_entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layout_entry.binding = bindings[i].binding;
            layout_entry.visibility = WGPUShaderStage_Compute;
            layout_entry.buffer.type = binding_type(bindings[i].buffer_type);
            layout_entries[used++] = layout_entry;
        }
        WGPUBindGroupLayoutDescriptor layout = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layout.entryCount = used;
        layout.entries = layout_entries;
        entry->groups[g] = wgpuDeviceCreateBindGroupLayout(runtime->device, &layout);
    }
    entry->group_count = groups;
    WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipeline_layout_descriptor.bindGroupLayoutCount = groups;
    pipeline_layout_descriptor.bindGroupLayouts = entry->groups;
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(runtime->device, &pipeline_layout_descriptor);
    WGPUComputePipelineDescriptor pipeline = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeline.layout = pipeline_layout;
    pipeline.compute.module = module;
    pipeline.compute.entryPoint = (WGPUStringView){entry->entry_point, WGPU_STRLEN};
    entry->pipeline = module && pipeline_layout ? wgpuDeviceCreateComputePipeline(runtime->device, &pipeline) : NULL;
    if (pipeline_layout) wgpuPipelineLayoutRelease(pipeline_layout);
    if (module) wgpuShaderModuleRelease(module);
    bool ok = runtime_pop_scopes(runtime, execution);
    if (ok && !entry->pipeline) ok = runtime_fail(execution, "kernel_pipeline_failed", "compute pipeline was not created");
    if (!ok) execution->status_code = "kernel_pipeline_failed";
    return ok;
}

static pipeline_entry *prepare_pipeline(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    char *source = NULL;
    size_t source_size = 0;
    char *path = resolve_kernel(runtime, command->as.dispatch.kernel, &source, &source_size);
    if (!path) {
        runtime_fail(execution, "kernel_source_missing", "kernel \"%s\" has no readable WGSL source%s",
            command->as.dispatch.kernel, runtime->options.kernel_root ? " under the kernel root" : " (no --kernel-root)");
        return NULL;
    }
    const doe_kernel_binding *bindings = command->as.dispatch.bindings;
    size_t count = command->as.dispatch.binding_count;
    pipeline_entry *cached = find_pipeline(runtime, path, command->as.dispatch.entry_point, bindings, count);
    if (cached) {
        free(path);
        free(source);
        return cached;
    }
    if (runtime->pipeline_count == runtime->pipeline_capacity) {
        size_t capacity = runtime->pipeline_capacity ? runtime->pipeline_capacity * 2 : 8;
        pipeline_entry *grown = realloc(runtime->pipelines, capacity * sizeof(*grown));
        if (!grown) {
            free(path);
            free(source);
            runtime_fail(execution, "out_of_memory", "out of memory");
            return NULL;
        }
        runtime->pipelines = grown;
        runtime->pipeline_capacity = capacity;
    }
    pipeline_entry entry = {.source_path = path, .entry_point = strdup(command->as.dispatch.entry_point)};
    if (count) entry.signature = malloc(count * sizeof(*bindings));
    if (!entry.entry_point || (count && !entry.signature)) {
        free(source);
        release_pipeline(&entry);
        runtime_fail(execution, "out_of_memory", "out of memory");
        return NULL;
    }
    if (count) memcpy(entry.signature, bindings, count * sizeof(*bindings));
    entry.signature_count = count;
    bool built = build_pipeline(runtime, &entry, source, source_size, bindings, count, execution);
    free(source);
    if (!built) {
        release_pipeline(&entry);
        return NULL;
    }
    runtime->pipelines[runtime->pipeline_count] = entry;
    return &runtime->pipelines[runtime->pipeline_count++];
}

/* Encodes `count` dispatches in one pass; returns the finished buffer or NULL. */
static WGPUCommandBuffer encode(doe_runtime *runtime, const pipeline_entry *pipeline, WGPUBindGroup *groups,
    const doe_command *command, uint64_t count)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
    if (!encoder) return NULL;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, NULL);
    if (pass) {
        wgpuComputePassEncoderSetPipeline(pass, pipeline->pipeline);
        for (uint32_t g = 0; g < pipeline->group_count; g++) wgpuComputePassEncoderSetBindGroup(pass, g, groups[g], 0, NULL);
        for (uint64_t i = 0; i < count; i++)
            wgpuComputePassEncoderDispatchWorkgroups(pass, command->as.dispatch.x, command->as.dispatch.y, command->as.dispatch.z);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    return commands;
}

static bool create_groups(doe_runtime *runtime, const pipeline_entry *pipeline, const doe_command *command,
    WGPUBindGroup *groups, doe_trace_execution *execution)
{
    const doe_kernel_binding *bindings = command->as.dispatch.bindings;
    size_t count = command->as.dispatch.binding_count;
    WGPUBindGroupEntry entries[MAX_GROUP_BINDINGS];
    for (uint32_t g = 0; g < pipeline->group_count; g++) {
        size_t used = 0;
        for (size_t i = 0; i < count && used < MAX_GROUP_BINDINGS; i++) {
            const doe_kernel_binding *binding = &bindings[i];
            if (binding->group != g) continue;
            buffer_entry *buffer = runtime_find_buffer(runtime, binding->handle);
            if (!buffer) {
                if (binding->buffer_size == UINT64_MAX)
                    return runtime_fail(execution, "kernel_binding_size", "binding %u.%u names new buffer %llu without buffer_size",
                        g, binding->binding, (unsigned long long)binding->handle);
                if (binding->buffer_offset > UINT64_MAX - binding->buffer_size)
                    return runtime_fail(execution, "kernel_binding_size", "binding %u.%u range overflows", g, binding->binding);
                buffer = runtime_create_buffer(runtime, binding->handle, binding->buffer_offset + binding->buffer_size, execution);
                if (!buffer) return false;
            }
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding = binding->binding;
            entry.buffer = buffer->buffer;
            entry.offset = binding->buffer_offset;
            entry.size = binding->buffer_size == UINT64_MAX ? WGPU_WHOLE_SIZE : binding->buffer_size;
            entries[used++] = entry;
        }
        WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        descriptor.layout = pipeline->groups[g];
        descriptor.entryCount = used;
        descriptor.entries = entries;
        groups[g] = wgpuDeviceCreateBindGroup(runtime->device, &descriptor);
        if (!groups[g]) return runtime_fail(execution, "kernel_bind_group_failed", "bind group %u was not created", g);
    }
    return true;
}

static bool submit_and_wait(doe_runtime *runtime, WGPUCommandBuffer commands, doe_trace_execution *execution)
{
    if (!commands) return runtime_fail(execution, "kernel_encode_failed", "command buffer was not created");
    wgpuQueueSubmit(runtime->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    return runtime_wait_queue(runtime, execution);
}

bool runtime_execute_kernel(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    uint64_t start = doe_runtime_monotonic_ns();
    pipeline_entry *pipeline = prepare_pipeline(runtime, command, execution);
    if (!pipeline) {
        execution->setup_ns = doe_runtime_monotonic_ns() - start;
        return false;
    }
    WGPUBindGroup groups[MAX_GROUPS] = {0};
    runtime_push_scopes(runtime);
    bool ok = create_groups(runtime, pipeline, command, groups, execution);
    if (ok && command->as.dispatch.warmup) {
        ok = submit_and_wait(runtime, encode(runtime, pipeline, groups, command, command->as.dispatch.warmup), execution);
    }
    uint64_t setup_end = doe_runtime_monotonic_ns();
    WGPUCommandBuffer commands = ok ? encode(runtime, pipeline, groups, command, command->as.dispatch.repeat) : NULL;
    uint64_t encode_end = doe_runtime_monotonic_ns();
    if (ok) {
        ok = submit_and_wait(runtime, commands, execution);
        execution->submit_count = 1;
        execution->dispatch_count = command->as.dispatch.repeat;
    }
    uint64_t end = doe_runtime_monotonic_ns();
    for (uint32_t g = 0; g < MAX_GROUPS; g++) if (groups[g]) wgpuBindGroupRelease(groups[g]);
    bool scopes_ok = runtime_pop_scopes(runtime, execution);
    execution->setup_ns = setup_end - start;
    execution->encode_ns = encode_end - setup_end;
    execution->submit_wait_ns = end - encode_end;
    return ok && scopes_ok;
}

typedef struct map_reply {
    bool done;
    WGPUMapAsyncStatus status;
} map_reply;

static void mapped(WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)message;
    (void)userdata2;
    map_reply *reply = userdata1;
    reply->done = true;
    reply->status = status;
}

/* Fresh zero-filled buffers for every binding (WebGPU zero-fills on
 * creation), `dispatch_count` dispatches without warmup, then the target
 * binding's range read back and hashed. The stream's handle buffers are not
 * touched, so the oracle sees no earlier command's contents. */
bool runtime_validate_oracle(doe_runtime *runtime, const doe_command *command, const doe_output_oracle *oracle,
    char actual_sha256[65], doe_trace_execution *execution)
{
    *execution = (doe_trace_execution){.ok = true};
    const doe_kernel_binding *target = NULL;
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *binding = &command->as.dispatch.bindings[i];
        if (binding->group == oracle->group && binding->binding == oracle->binding) target = binding;
    }
    if (!target) return runtime_fail(execution, "output_oracle_binding_missing", "the oracle binding %u.%u is not bound", oracle->group, oracle->binding);
    if (target->buffer_size == UINT64_MAX || target->buffer_size == 0 || target->buffer_size % 4 != 0)
        return runtime_fail(execution, "output_oracle_buffer_size", "the oracle binding needs an explicit buffer_size multiple of 4");
    pipeline_entry *pipeline = prepare_pipeline(runtime, command, execution);
    if (!pipeline) return false;
    WGPUBuffer buffers[MAX_GROUP_BINDINGS] = {0};
    WGPUBuffer target_buffer = NULL;
    WGPUBindGroup groups[MAX_GROUPS] = {0};
    WGPUBuffer readback = NULL;
    runtime_push_scopes(runtime);
    bool ok = command->as.dispatch.binding_count <= MAX_GROUP_BINDINGS ||
        runtime_fail(execution, "output_oracle_bindings", "more than %d bindings", MAX_GROUP_BINDINGS);
    WGPUBindGroupEntry entries[MAX_GROUP_BINDINGS];
    for (uint32_t g = 0; ok && g < pipeline->group_count; g++) {
        size_t used = 0;
        for (size_t i = 0; ok && i < command->as.dispatch.binding_count; i++) {
            const doe_kernel_binding *binding = &command->as.dispatch.bindings[i];
            if (binding->group != g) continue;
            if (binding->buffer_size == UINT64_MAX || binding->buffer_offset > UINT64_MAX - 3 - binding->buffer_size) {
                ok = runtime_fail(execution, "output_oracle_buffer_size", "binding %u.%u needs buffer_size", g, binding->binding);
                break;
            }
            WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
            descriptor.size = (binding->buffer_offset + binding->buffer_size + 3) & ~UINT64_C(3);
            descriptor.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform | WGPUBufferUsage_CopySrc;
            buffers[i] = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
            if (binding == target) target_buffer = buffers[i];
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding = binding->binding;
            entry.buffer = buffers[i];
            entry.offset = binding->buffer_offset;
            entry.size = binding->buffer_size;
            entries[used++] = entry;
        }
        WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        descriptor.layout = pipeline->groups[g];
        descriptor.entryCount = used;
        descriptor.entries = entries;
        if (ok) groups[g] = wgpuDeviceCreateBindGroup(runtime->device, &descriptor);
    }
    if (ok) {
        doe_command isolated = *command;
        isolated.as.dispatch.warmup = 0;
        ok = submit_and_wait(runtime, encode(runtime, pipeline, groups, &isolated, oracle->dispatch_count), execution);
    }
    if (ok) {
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size = target->buffer_size;
        descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        readback = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, target_buffer, target->buffer_offset, readback, 0, target->buffer_size);
        WGPUCommandBuffer copy = wgpuCommandEncoderFinish(encoder, NULL);
        wgpuCommandEncoderRelease(encoder);
        ok = submit_and_wait(runtime, copy, execution);
    }
    if (ok) {
        map_reply reply = {0};
        WGPUBufferMapCallbackInfo callback = {.mode = WGPUCallbackMode_WaitAnyOnly, .callback = mapped, .userdata1 = &reply};
        if (!runtime_wait_future(runtime, wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, target->buffer_size, callback)) ||
            reply.status != WGPUMapAsyncStatus_Success) {
            ok = runtime_fail(execution, "output_oracle_readback", "oracle readback did not map");
        } else {
            const void *bytes = wgpuBufferGetConstMappedRange(readback, 0, target->buffer_size);
            if (!bytes) {
                ok = runtime_fail(execution, "output_oracle_readback", "oracle readback has no mapped range");
            } else {
                uint8_t digest[32];
                doe_sha256(bytes, (size_t)target->buffer_size, digest);
                doe_sha256_hex(digest, actual_sha256);
            }
            wgpuBufferUnmap(readback);
        }
    }
    if (readback) wgpuBufferRelease(readback);
    for (uint32_t g = 0; g < MAX_GROUPS; g++) if (groups[g]) wgpuBindGroupRelease(groups[g]);
    for (size_t i = 0; i < MAX_GROUP_BINDINGS; i++) if (buffers[i]) wgpuBufferRelease(buffers[i]);
    bool scopes_ok = runtime_pop_scopes(runtime, execution);
    return ok && scopes_ok;
}
