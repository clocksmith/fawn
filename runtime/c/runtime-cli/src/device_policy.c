/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include "sha256.h"
#include "doe_native_device_policy.h"
#include "json.h"
#include "host_io.h"
#include <stdlib.h>
#include <string.h>

static const char *string_field(const doe_json_value *object, const char *name) {
    const doe_json_value *value = doe_json_member_value(object, name);
    return value && value->kind == DOE_JSON_STRING && strlen(value->string) == value->string_size ? value->string : NULL;
}

bool doe_runtime_load_device_policy(const char *lane_name, DoeNativeDevicePolicy *policy,
    char *message, size_t capacity, char source_hash[65], WGPUBackendType *backend_type) {
    char normalized[96];
    size_t length = strlen(lane_name);
    if (length >= sizeof(normalized)) { snprintf(message, capacity, "lane name too long"); return false; }
    for (size_t i = 0; i <= length; ++i) {
        char ch = lane_name[i] == '-' ? '_' : lane_name[i];
        normalized[i] = ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
    }
    const char *path = "config/backend-runtime-policy.json";
    FILE *file = doe_host_fopen(path, "rb");
    if (!file) { snprintf(message, capacity, "cannot read canonical lane policy %s", path); return false; }
    enum { MAX_POLICY_BYTES = 1024 * 1024 };
    char *bytes = malloc(MAX_POLICY_BYTES + 1u);
    if (!bytes) { fclose(file); snprintf(message, capacity, "lane policy allocation failed"); return false; }
    size_t size = fread(bytes, 1, MAX_POLICY_BYTES + 1u, file);
    bool read_ok = !ferror(file) && size <= MAX_POLICY_BYTES;
    fclose(file);
    doe_json_document document = {0}; doe_json_error error = {0};
    bool ok = read_ok && doe_json_parse(bytes, size, &document, &error) == DOE_JSON_OK;
    if (ok && source_hash) {
        uint8_t digest[32]; doe_sha256(bytes, size, digest); doe_sha256_hex(digest, source_hash);
    }
    free(bytes);
    if (!ok) { snprintf(message, capacity, "invalid or oversized canonical lane policy"); return false; }
    const doe_json_value *lane = doe_json_member_value(doe_json_member_value(document.root, "lanes"), normalized);
    const char *backend = string_field(lane, "defaultBackend");
    const char *queue = string_field(lane, "queueFamilyPolicy");
    const char *sync = string_field(lane, "deferredSubmissionSyncPolicy");
    const doe_json_value *fallback = doe_json_member_value(lane, "allowFallback");
    const doe_json_value *strict = doe_json_member_value(lane, "strictNoFallback");
    DoeNativeDevicePolicy selected = {0};
    const char *reason = NULL;
    WGPUBackendType selected_backend = backend && !strcmp(backend,"doe_vulkan") ? WGPUBackendType_Vulkan :
        backend && !strcmp(backend,"doe_metal") ? WGPUBackendType_Metal :
        backend && !strcmp(backend,"doe_d3d12") ? WGPUBackendType_D3D12 : WGPUBackendType_Undefined;
    if (selected_backend == WGPUBackendType_Undefined) reason = "lane does not select an implemented native device policy";
    else if (!fallback || fallback->kind != DOE_JSON_BOOL || fallback->boolean ||
        !strict || strict->kind != DOE_JSON_BOOL || !strict->boolean) reason = "lane fallback contract unsupported";
    else if (!queue) reason = "lane lacks queueFamilyPolicy";
    else if (!strcmp(queue, "prefer_graphics_compute")) selected.queue_selection = DOE_NATIVE_QUEUE_PREFER_GRAPHICS_COMPUTE;
    else if (!strcmp(queue, "require_compute_only")) selected.queue_selection = DOE_NATIVE_QUEUE_REQUIRE_COMPUTE_ONLY;
    else reason = "unimplemented queueFamilyPolicy";
    if (!reason) {
        if (sync && !strcmp(sync, "prefer_timeline_semaphore")) selected.synchronization = DOE_NATIVE_SYNC_PREFER_TIMELINE;
        else if (!sync || strcmp(sync, "require_fence_pool")) reason = "unimplemented deferredSubmissionSyncPolicy";
    }
    /* Map only policies implemented by the shared native owners. */
    if (!reason && doe_json_member_value(lane, "uploadPathPolicy")) {
        const char *upload = string_field(lane, "uploadPathPolicy");
        if (upload && !strcmp(upload, "staged_copy_only")) selected.upload_policy = DOE_NATIVE_UPLOAD_STAGED_COPY;
        else if (upload && !strcmp(upload, "allow_mapped_shortcuts")) selected.upload_policy = DOE_NATIVE_UPLOAD_ALLOW_MAPPED;
        else reason = "unimplemented uploadPathPolicy";
    }
    if (!reason && doe_json_member_value(lane, "vulkanSubgroupSizePolicy")) {
        const char *subgroup = string_field(lane, "vulkanSubgroupSizePolicy");
        if (subgroup && !strcmp(subgroup, "prefer_32")) selected.subgroup_policy = 1;
        else if (subgroup && !strcmp(subgroup, "suppress_for_workgroup_memory_256")) selected.subgroup_policy = 2;
        else if (subgroup && !strcmp(subgroup, "suppress_for_workgroup_memory_256_or_single_invocation")) selected.subgroup_policy = 3;
        else reason = "unimplemented vulkanSubgroupSizePolicy";
    }
    if (!reason && selected_backend == WGPUBackendType_Metal &&
        (selected.queue_selection == DOE_NATIVE_QUEUE_REQUIRE_COMPUTE_ONLY ||
         selected.synchronization != DOE_NATIVE_SYNC_PREFER_TIMELINE || selected.subgroup_policy))
        reason = "unsupported Metal queue/synchronization/subgroup combination";
    if (!reason && selected_backend == WGPUBackendType_D3D12 &&
        (selected.queue_selection == DOE_NATIVE_QUEUE_REQUIRE_COMPUTE_ONLY ||
         selected.synchronization != DOE_NATIVE_SYNC_PREFER_TIMELINE ||
         selected.upload_policy == DOE_NATIVE_UPLOAD_ALLOW_MAPPED || selected.subgroup_policy))
        reason = "unsupported D3D12 queue/synchronization/upload/subgroup combination";
    if (reason) snprintf(message, capacity, "lane %s: %s", lane_name, reason);
    else { *policy = selected; if (backend_type) *backend_type = selected_backend; }
    doe_json_free(&document);
    return !reason;
}

bool doe_runtime_write_device_policy(FILE *out, const doe_runtime *runtime) {
    if (!runtime || !runtime->options.backend_lane) return true;
    const DoeNativeDevicePolicy *policy = &runtime->device_policy;
    return fprintf(out, ",\"nativeDevicePolicy\":{\"sourceSha256\":\"%s\",\"queueSelection\":%u,"
        "\"synchronization\":%u,\"uploadPolicy\":%u,\"subgroupPolicy\":%u,\"observedQueueSyncBits\":%u}",
        runtime->device_policy_hash, policy->queue_selection, policy->synchronization,
        policy->upload_policy, policy->subgroup_policy, runtime->observed_sync_bits) >= 0;
}
