/* SPDX-License-Identifier: MIT
 * kernel_dispatch: a WGSL compute kernel from the kernel root, an explicit
 * layout built from the command's bindings, buffers keyed by handle, optional
 * warmup dispatches, then `repeat` dispatches in one compute pass and one
 * submission. */
#include "runtime_internal.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include "host_io.h"
#include <string.h>

static char *read_source(const char *path, size_t *size)
{
    if (!doe_host_regular_file(path)) return NULL;
    FILE *file = doe_host_fopen(path, "rb");
    if (!file) return NULL;
    long length = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (length <= 0 || (unsigned long)length > MAX_KERNEL_SOURCE_BYTES || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    char *text = malloc((size_t)length + 1);
    size_t read = text ? fread(text, 1, (size_t)length, file) : 0;
    bool ok = !ferror(file);
    if (fclose(file)) ok = false;
    if (!text || !ok || read != (size_t)length) {
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
char *runtime_resolve_kernel(const doe_runtime *runtime, const char *kernel, char **source, size_t *size)
{
    char *candidates[3] = {0};
    candidates[0] = doe_host_strdup(kernel);
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

static void source_record_clear(kernel_source_record *record)
{
    free(record->module_id);
    free(record->path);
    free(record->entry_point);
    free(record->bytes);
    *record = (kernel_source_record){0};
}
void runtime_kernel_sources_clear(doe_runtime *runtime)
{
    for (size_t i = 0; i < runtime->kernel_source_count; i++) source_record_clear(&runtime->kernel_sources[i]);
    free(runtime->kernel_sources);
    runtime->kernel_sources = NULL;
    runtime->kernel_source_count = runtime->kernel_source_capacity = 0;
}
size_t doe_runtime_source_receipt_count(const doe_runtime *runtime)
{
    return runtime ? runtime->kernel_source_count : 0;
}
bool doe_runtime_source_receipt_at(const doe_runtime *runtime, size_t index, doe_runtime_source_receipt *out)
{
    if (!runtime || !out || index >= runtime->kernel_source_count) return false;
    const kernel_source_record *record = &runtime->kernel_sources[index];
    *out = (doe_runtime_source_receipt){record->module_id, record->path, record->entry_point,
        record->sha256, record->size};
    return true;
}
bool runtime_record_kernel_source(doe_runtime *runtime, const char *module_id, const char *path,
    const char *entry_point, const char *source, size_t size, size_t *index)
{
    for (size_t i = 0; i < runtime->kernel_source_count; i++) {
        const kernel_source_record *record = &runtime->kernel_sources[i];
        if (record->size == size && !strcmp(record->module_id, module_id) && !strcmp(record->path, path) &&
            ((!entry_point && !record->entry_point) || (entry_point && record->entry_point && !strcmp(entry_point, record->entry_point))) &&
            !memcmp(record->bytes, source, size)) { *index = i; return true; }
    }
    if (size == SIZE_MAX) return false;
    kernel_source_record record = {.module_id=doe_host_strdup(module_id), .path=doe_host_strdup(path),
        .entry_point=entry_point ? doe_host_strdup(entry_point) : NULL, .bytes=malloc(size + 1), .size=size};
    if (!record.module_id || !record.path || (entry_point && !record.entry_point) || !record.bytes) {
        source_record_clear(&record);
        return false;
    }
    memcpy(record.bytes, source, size);
    record.bytes[size] = '\0';
    uint8_t digest[32];
    doe_sha256(record.bytes, size, digest);
    doe_sha256_hex(digest, record.sha256);
    if (runtime->kernel_source_count == runtime->kernel_source_capacity) {
        size_t capacity = runtime->kernel_source_capacity;
        if (capacity > SIZE_MAX / 2 / sizeof(record)) { source_record_clear(&record); return false; }
        capacity = capacity ? capacity * 2 : 8;
        kernel_source_record *grown = realloc(runtime->kernel_sources, capacity * sizeof(record));
        if (!grown) { source_record_clear(&record); return false; }
        runtime->kernel_sources = grown;
        runtime->kernel_source_capacity = capacity;
    }
    *index = runtime->kernel_source_count;
    runtime->kernel_sources[runtime->kernel_source_count++] = record;
    return true;
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
        if (a->group != b->group || a->binding != b->binding || a->resource != b->resource || a->buffer_type != b->buffer_type ||
            a->sample_type != b->sample_type || a->storage_access != b->storage_access || a->has_format != b->has_format ||
            (a->has_format && a->format != b->format)) return false;
    }
    return true;
}

static pipeline_entry *find_pipeline(doe_runtime *runtime, const char *path, const char *entry_point,
    const doe_kernel_binding *bindings, size_t count, const char *source, size_t source_size)
{
    for (size_t i = 0; i < runtime->pipeline_count; i++) {
        pipeline_entry *entry = &runtime->pipelines[i];
        const kernel_source_record *identity = &runtime->kernel_sources[entry->source_index];
        if (!strcmp(entry->source_path, path) && !strcmp(entry->entry_point, entry_point) &&
            identity->size == source_size && !memcmp(identity->bytes, source, source_size) &&
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
            if (bindings[i].resource == DOE_RESOURCE_TEXTURE) {
                layout_entry.texture.sampleType = (WGPUTextureSampleType)bindings[i].sample_type;
                layout_entry.texture.viewDimension = WGPUTextureViewDimension_2D;
            } else if (bindings[i].resource == DOE_RESOURCE_STORAGE_TEXTURE) {
                layout_entry.storageTexture.access = (WGPUStorageTextureAccess)bindings[i].storage_access;
                layout_entry.storageTexture.format = runtime_webgpu_format(bindings[i].format);
                layout_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            } else {
                layout_entry.buffer.type = binding_type(bindings[i].buffer_type);
            }
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
    char *path = runtime_resolve_kernel(runtime, command->as.dispatch.kernel, &source, &source_size);
    if (!path) {
        runtime_fail(execution, "kernel_source_missing", "kernel \"%s\" has no readable WGSL source%s",
            command->as.dispatch.kernel, runtime->options.kernel_root ? " under the kernel root" : " (no --kernel-root)");
        return NULL;
    }
    const doe_kernel_binding *bindings = command->as.dispatch.bindings;
    size_t count = command->as.dispatch.binding_count;
    pipeline_entry *cached = find_pipeline(runtime, path, command->as.dispatch.entry_point, bindings, count, source, source_size);
    if (cached) {
        size_t identity;
        bool recorded = runtime_record_kernel_source(runtime, command->as.dispatch.kernel, path,
            command->as.dispatch.entry_point, source, source_size, &identity);
        free(path);
        free(source);
        if (!recorded) { runtime_fail(execution, "out_of_memory", "source receipt allocation failed"); return NULL; }
        return cached;
    }
    if (runtime->pipeline_count == runtime->pipeline_capacity) {
        if (runtime->pipeline_capacity > SIZE_MAX / 2 / sizeof(pipeline_entry)) {
            free(path); free(source);
            runtime_fail(execution, "out_of_memory", "pipeline cache capacity overflow");
            return NULL;
        }
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
    pipeline_entry entry = {.source_path = path, .entry_point = doe_host_strdup(command->as.dispatch.entry_point)};
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
    if (built && !runtime_record_kernel_source(runtime, command->as.dispatch.kernel, path,
        command->as.dispatch.entry_point, source, source_size, &entry.source_index))
        built = runtime_fail(execution, "out_of_memory", "source receipt allocation failed");
    free(source);
    if (!built) {
        release_pipeline(&entry);
        return NULL;
    }
    runtime->pipelines[runtime->pipeline_count] = entry;
    return &runtime->pipelines[runtime->pipeline_count++];
}

/* Encodes `count` dispatches in one pass (one indirect dispatch when
 * `indirect` is set); returns the finished buffer or NULL. Independent repeats
 * are recorded the same way: webgpu.h has no barrier control, so the provider
 * orders them as it orders any dispatches in a pass. */
static WGPUCommandBuffer encode_dispatches(doe_runtime *runtime, const pipeline_entry *pipeline, WGPUBindGroup *groups,
    const doe_command *command, uint64_t count, WGPUBuffer indirect, const runtime_timestamp *timestamp)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(runtime->device, NULL);
    if (!encoder) return NULL;
    WGPUComputePassDescriptor descriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    if (timestamp && timestamp->query) descriptor.timestampWrites = &timestamp->writes;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &descriptor);
    if (pass) {
        wgpuComputePassEncoderSetPipeline(pass, pipeline->pipeline);
        for (uint32_t g = 0; g < pipeline->group_count; g++) wgpuComputePassEncoderSetBindGroup(pass, g, groups[g], 0, NULL);
        if (indirect) wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, indirect, 0);
        for (uint64_t i = 0; !indirect && i < count; i++)
            wgpuComputePassEncoderDispatchWorkgroups(pass, command->as.dispatch.x, command->as.dispatch.y, command->as.dispatch.z);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }
    if (timestamp) runtime_timestamp_resolve(timestamp, encoder);
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);
    return commands;
}

static WGPUCommandBuffer encode(doe_runtime *runtime, const pipeline_entry *pipeline, WGPUBindGroup *groups,
    const doe_command *command, uint64_t count)
{
    return encode_dispatches(runtime, pipeline, groups, command, count, NULL, NULL);
}

/* A 2D view of the binding's texture (created or replaced as Zig's
 * getOrCreateTextureFromBinding does), in the binding's or the texture's format. */
static WGPUTextureView binding_view(doe_runtime *runtime, const doe_kernel_binding *binding, doe_trace_execution *execution)
{
    WGPUTextureUsage usage = binding->resource == DOE_RESOURCE_TEXTURE ? WGPUTextureUsage_TextureBinding :
        WGPUTextureUsage_StorageBinding;
    doe_texture_format view_format;
    WGPUTexture texture = runtime_binding_texture(runtime, binding->handle, usage, binding->has_format, binding->format,
        &view_format, execution);
    if (!texture) return NULL;
    WGPUTextureViewDescriptor descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    descriptor.format = runtime_webgpu_format(view_format);
    descriptor.dimension = WGPUTextureViewDimension_2D;
    WGPUTextureView view = wgpuTextureCreateView(texture, &descriptor);
    if (!view) runtime_fail(execution, "kernel_texture_view_failed", "texture %llu view was not created", (unsigned long long)binding->handle);
    return view;
}

static bool create_groups(doe_runtime *runtime, runtime_buffer_table *buffers, const pipeline_entry *pipeline,
    const doe_command *command, WGPUBindGroup *groups, doe_trace_execution *execution)
{
    const doe_kernel_binding *bindings = command->as.dispatch.bindings;
    size_t count = command->as.dispatch.binding_count;
    WGPUBindGroupEntry entries[MAX_GROUP_BINDINGS];
    WGPUTextureView views[MAX_GROUP_BINDINGS];
    for (uint32_t g = 0; g < pipeline->group_count; g++) {
        size_t used = 0, view_count = 0;
        bool entries_ok = true;
        for (size_t i = 0; i < count && used < MAX_GROUP_BINDINGS; i++) {
            const doe_kernel_binding *binding = &bindings[i];
            if (binding->group != g) continue;
            if (binding->resource != DOE_RESOURCE_BUFFER) {
                WGPUTextureView view = binding_view(runtime, binding, execution);
                if (!view) { entries_ok = false; break; }
                views[view_count++] = view;
                WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding = binding->binding;
                entry.textureView = view;
                entries[used++] = entry;
                continue;
            }
            buffer_entry *buffer = runtime_buffer_table_find(buffers, binding->handle);
            if (!buffer) {
                if (binding->buffer_size == UINT64_MAX) {
                    entries_ok = runtime_fail(execution, "kernel_binding_size", "binding %u.%u names new buffer %llu without buffer_size",
                        g, binding->binding, (unsigned long long)binding->handle);
                    break;
                }
                if (binding->buffer_offset > UINT64_MAX - binding->buffer_size) {
                    entries_ok = runtime_fail(execution, "kernel_binding_size", "binding %u.%u range overflows", g, binding->binding);
                    break;
                }
                buffer = runtime_buffer_table_create(runtime, buffers, binding->handle, binding->buffer_offset + binding->buffer_size, execution);
                if (!buffer) { entries_ok = false; break; }
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
        groups[g] = entries_ok ? wgpuDeviceCreateBindGroup(runtime->device, &descriptor) : NULL;
        for (size_t v = 0; v < view_count; v++) wgpuTextureViewRelease(views[v]);
        if (!entries_ok) return false;
        if (!groups[g]) return runtime_fail(execution, "kernel_bind_group_failed", "bind group %u was not created", g);
    }
    return true;
}

static bool execute_pipeline(doe_runtime *runtime, const doe_command *command, WGPUBuffer indirect,
    doe_trace_execution *execution);

bool runtime_execute_kernel(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    return execute_pipeline(runtime, command, NULL, execution);
}

/* backend_execute.zig execute_dispatch_command / execute_dispatch_indirect_command:
 * the bound pipeline and bindings (the last kernel_dispatch), else
 * dispatch_noop.wgsl without bindings; one dispatch of the grid. */
bool runtime_execute_grid(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    doe_command target = {.kind = DOE_COMMAND_KERNEL_DISPATCH, .label = "kernel_dispatch"};
    if (runtime->has_last_kernel) {
        target = runtime->last_kernel;
    } else {
        target.as.dispatch.kernel = "dispatch_noop.wgsl";
        target.as.dispatch.entry_point = "main";
    }
    target.as.dispatch.x = command->as.grid.x;
    target.as.dispatch.y = command->as.grid.y;
    target.as.dispatch.z = command->as.grid.z;
    target.as.dispatch.repeat = 1;
    target.as.dispatch.warmup = 0;
    if (command->kind == DOE_COMMAND_DISPATCH) return execute_pipeline(runtime, &target, NULL, execution);
    uint64_t start = doe_runtime_monotonic_ns();
    if (!runtime->indirect_args) {
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size = 16;
        descriptor.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst;
        runtime->indirect_args = wgpuDeviceCreateBuffer(runtime->device, &descriptor);
        if (!runtime->indirect_args) return runtime_fail(execution, "dispatch_indirect_args_failed", "the indirect argument buffer was not created");
    }
    const uint32_t args[4] = {command->as.grid.x, command->as.grid.y, command->as.grid.z, 0};
    wgpuQueueWriteBuffer(runtime->queue, runtime->indirect_args, 0, args, sizeof(args));
    uint64_t args_ns = doe_runtime_monotonic_ns() - start;
    bool ok = execute_pipeline(runtime, &target, runtime->indirect_args, execution);
    execution->setup_ns += args_ns; /* The argument write is setup, as Zig's write_dispatch_indirect_args. */
    return ok;
}

static bool execute_pipeline(doe_runtime *runtime, const doe_command *command, WGPUBuffer indirect,
    doe_trace_execution *execution)
{
    uint64_t start = doe_runtime_monotonic_ns();
    pipeline_entry *pipeline = prepare_pipeline(runtime, command, execution);
    if (!pipeline) {
        execution->setup_ns = doe_runtime_monotonic_ns() - start;
        return false;
    }
    WGPUBindGroup groups[MAX_GROUPS] = {0};
    runtime_push_scopes(runtime);
    bool ok = create_groups(runtime, &runtime->buffers, pipeline, command, groups, execution);
    if (ok && command->as.dispatch.warmup) {
        ok = runtime_submit(runtime, encode(runtime, pipeline, groups, command, command->as.dispatch.warmup), execution);
        if (ok && runtime->options.defer_queue_sync) ok = runtime_wait_queue(runtime, execution);
    }
    runtime_timestamp timestamp = {0};
    if (ok) ok = runtime_timestamp_begin(runtime, &timestamp, execution);
    uint64_t setup_end = doe_runtime_monotonic_ns();
    WGPUCommandBuffer commands = ok ?
        encode_dispatches(runtime, pipeline, groups, command, command->as.dispatch.repeat, indirect, &timestamp) : NULL;
    uint64_t encode_end = doe_runtime_monotonic_ns();
    if (ok) {
        ok = runtime_submit(runtime, commands, execution);
        execution->submit_count = 1;
        execution->dispatch_count = command->as.dispatch.repeat;
    }
    ok = runtime_timestamp_finish(runtime, &timestamp, ok, execution);
    uint64_t end = doe_runtime_monotonic_ns();
    for (uint32_t g = 0; g < MAX_GROUPS; g++) if (groups[g]) wgpuBindGroupRelease(groups[g]);
    bool scopes_ok = runtime_pop_scopes(runtime, execution);
    execution->setup_ns = setup_end - start;
    execution->encode_ns = encode_end - setup_end;
    execution->submit_wait_ns = end - encode_end;
    if (ok && scopes_ok)
        memcpy(execution->shader_source_sha256, runtime->kernel_sources[pipeline->source_index].sha256,
            sizeof(execution->shader_source_sha256));
    return ok && scopes_ok;
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
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++)
        if (command->as.dispatch.bindings[i].resource != DOE_RESOURCE_BUFFER)
            return runtime_fail(execution, "output_oracle_texture_bindings", "isolated oracles over texture bindings are not implemented");
    if (target->buffer_size == UINT64_MAX || target->buffer_size == 0 || target->buffer_size % 4 != 0)
        return runtime_fail(execution, "output_oracle_buffer_size", "the oracle binding needs an explicit buffer_size multiple of 4");
    for (size_t i = 0; i < command->as.dispatch.binding_count; i++) {
        const doe_kernel_binding *binding = &command->as.dispatch.bindings[i];
        if (binding->buffer_size > UINT64_MAX - 3 || binding->buffer_offset > UINT64_MAX - 3 - binding->buffer_size)
            return runtime_fail(execution, "output_oracle_buffer_size", "binding %u.%u needs an explicit addressable buffer_size",
                binding->group, binding->binding);
    }
    pipeline_entry *pipeline = prepare_pipeline(runtime, command, execution);
    if (!pipeline) return false;
    runtime_buffer_table buffers = {0};
    WGPUBindGroup groups[MAX_GROUPS] = {0};
    uint8_t *bytes = NULL;
    runtime_push_scopes(runtime);
    bool ok = command->as.dispatch.binding_count <= MAX_GROUP_BINDINGS ||
        runtime_fail(execution, "output_oracle_bindings", "more than %d bindings", MAX_GROUP_BINDINGS);
    if (ok) ok = create_groups(runtime, &buffers, pipeline, command, groups, execution);
    buffer_entry *captured = runtime_buffer_table_find(&buffers, target->handle);
    WGPUBuffer target_buffer = captured ? captured->buffer : NULL;
    if (ok) {
        doe_command isolated = *command;
        isolated.as.dispatch.warmup = 0;
        ok = runtime_submit(runtime, encode(runtime, pipeline, groups, &isolated, oracle->dispatch_count), execution);
    }
    if (ok && target->buffer_size > SIZE_MAX) ok = runtime_fail(execution, "output_oracle_buffer_size", "the oracle range exceeds host memory");
    if (ok && !(bytes = malloc((size_t)target->buffer_size))) ok = runtime_fail(execution, "out_of_memory", "oracle readback allocation failed");
    if (ok) ok = runtime_read_back(runtime, target_buffer, target->buffer_offset, target->buffer_size, bytes, execution);
    if (ok) {
        uint8_t digest[32];
        doe_sha256(bytes, (size_t)target->buffer_size, digest);
        doe_sha256_hex(digest, actual_sha256);
    }
    free(bytes);
    for (uint32_t g = 0; g < MAX_GROUPS; g++) if (groups[g]) wgpuBindGroupRelease(groups[g]);
    runtime_buffer_table_clear(&buffers);
    bool scopes_ok = runtime_pop_scopes(runtime, execution);
    return ok && scopes_ok;
}

bool doe_runtime_prewarm_kernel(doe_runtime *runtime, const doe_command *command, doe_trace_execution *execution)
{
    *execution = (doe_trace_execution){.ok = true};
    if (command->kind != DOE_COMMAND_KERNEL_DISPATCH) return true;
    return prepare_pipeline(runtime, command, execution) != NULL;
}
