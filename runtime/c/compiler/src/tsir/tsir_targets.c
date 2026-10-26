/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "tsir_targets.h"
#include "tsir_sha256.h"

#include "tsir_file.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char *const numerical_modes[] = { "f32", "f16" };
static const TsirNativeCollective native_collectives[] = {
    { .kind_name = "workgroup_barrier", .exactness_name = "bit_exact_solo" }
};

static const TsirTargetIdentity descriptors[] = {
    {
        .pe_working_memory_bytes = 16384, .max_collective_group_size = 256,
        .native_numerical_modes = numerical_modes, .native_numerical_mode_count = 2,
        .native_collectives = native_collectives, .native_collective_count = 1,
        .runtime_sized_binding_policy = TSIR_POLICY_HOST_COPIED,
        .name = "webgpu-generic", .sub_tile_lane_width = 1,
        .streaming_gemm = "none", .fabric_per_hop_latency_ns = 200
    },
    {
        .pe_working_memory_bytes = 16384, .max_collective_group_size = 1024,
        .native_numerical_modes = numerical_modes, .native_numerical_mode_count = 2,
        .native_collectives = native_collectives, .native_collective_count = 1,
        .runtime_sized_binding_policy = TSIR_POLICY_HOST_COPIED,
        .name = "msl", .sub_tile_lane_width = 1,
        .streaming_gemm = "none", .fabric_per_hop_latency_ns = 150
    },
    {
        .pe_working_memory_bytes = 16384, .max_collective_group_size = 128,
        .native_numerical_modes = numerical_modes, .native_numerical_mode_count = 2,
        .native_collectives = native_collectives, .native_collective_count = 1,
        .runtime_sized_binding_policy = TSIR_POLICY_HOST_COPIED,
        .name = "spir-v", .sub_tile_lane_width = 1,
        .streaming_gemm = "none", .fabric_per_hop_latency_ns = 200
    }
};

const TsirTargetIdentity *tsir_target_descriptor(TsirBootstrapTarget target) {
    switch (target) {
        case TSIR_TARGET_WEBGPU_GENERIC: return &descriptors[0];
        case TSIR_TARGET_MSL: return &descriptors[1];
        case TSIR_TARGET_SPIR_V: return &descriptors[2];
    }
    return NULL;
}

static void hash_text(TsirSha256 *hash, const char *text, size_t length) {
    tsir_sha256_update(hash, (const uint8_t *)text, tsir_text_length(text, length));
}

static void hash_u64(TsirSha256 *hash, uint64_t value) {
    uint8_t bytes[8];
    size_t i;
    for (i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (uint8_t)(value >> (i * 8));
    }
    tsir_sha256_update(hash, bytes, sizeof(bytes));
}

static void hash_u32(TsirSha256 *hash, uint32_t value) {
    uint8_t bytes[4];
    size_t i;
    for (i = 0; i < sizeof(bytes); i++) {
        bytes[i] = (uint8_t)(value >> (i * 8));
    }
    tsir_sha256_update(hash, bytes, sizeof(bytes));
}

TsirStatus tsir_target_descriptor_hash(const TsirTargetIdentity *descriptor,
    uint8_t out[32]) {
    const TsirTargetDescriptor *planner;
    const char *policy = NULL;
    TsirSha256 hash;
    size_t i;
    if (descriptor == NULL || out == NULL || descriptor->name == NULL ||
        descriptor->streaming_gemm == NULL) {
        return TSIR_ERR_INVALID;
    }
    planner = descriptor;
    if ((planner->native_numerical_mode_count != 0 &&
            planner->native_numerical_modes == NULL) ||
        (planner->native_collective_count != 0 &&
            planner->native_collectives == NULL) ||
        (descriptor->fused_intrinsic_count != 0 &&
            descriptor->fused_intrinsics == NULL)) {
        return TSIR_ERR_INVALID;
    }
    switch (planner->runtime_sized_binding_policy) {
        case TSIR_POLICY_HOST_COPIED: policy = "host_copied"; break;
        case TSIR_POLICY_FABRIC_STREAMED_WITH_LOADER:
            policy = "fabric_streamed_with_loader"; break;
        case TSIR_POLICY_REJECT: policy = "reject"; break;
    }
    if (policy == NULL) {
        return TSIR_ERR_INVALID;
    }
    tsir_sha256_init(&hash);
    hash_text(&hash, descriptor->name, descriptor->name_length);
    hash_u64(&hash, planner->pe_working_memory_bytes);
    hash_u64(&hash, descriptor->pe_persistent_pool_bytes);
    hash_u32(&hash, planner->fabric_color_count);
    hash_u32(&hash, planner->max_collective_group_size);
    hash_u32(&hash, descriptor->sub_tile_lane_width);
    for (i = 0; i < planner->native_numerical_mode_count; i++) {
        if (planner->native_numerical_modes[i] == NULL) {
            return TSIR_ERR_INVALID;
        }
        hash_text(&hash, planner->native_numerical_modes[i],
            planner->native_numerical_mode_lengths == NULL ? 0 :
                planner->native_numerical_mode_lengths[i]);
    }
    for (i = 0; i < planner->native_collective_count; i++) {
        const TsirNativeCollective *collective = &planner->native_collectives[i];
        if (collective->kind_name == NULL || collective->exactness_name == NULL) {
            return TSIR_ERR_INVALID;
        }
        hash_text(&hash, collective->kind_name, collective->kind_name_length);
        hash_text(&hash, "|", 0);
        hash_text(&hash, collective->exactness_name, collective->exactness_name_length);
    }
    for (i = 0; i < descriptor->fused_intrinsic_count; i++) {
        if (descriptor->fused_intrinsics[i] == NULL) {
            return TSIR_ERR_INVALID;
        }
        hash_text(&hash, descriptor->fused_intrinsics[i],
            descriptor->fused_intrinsic_lengths == NULL ? 0 :
                descriptor->fused_intrinsic_lengths[i]);
    }
    hash_text(&hash, descriptor->streaming_gemm, descriptor->streaming_gemm_length);
    hash_text(&hash, policy, 0);
    tsir_sha256_final(&hash, out);
    return TSIR_OK;
}

static TsirStatus hash_source_file(TsirSha256 *hash, const char *root,
    const char *name) {
    static const char directory[] = "/runtime/zig/src/compiler/tsir/";
    const size_t source_limit = 1024u * 1024u;
    size_t root_len = strlen(root);
    size_t name_len = strlen(name);
    char *path;
    FILE *file;
    TsirFileStamp before;
    TsirFileStamp after;
    uint8_t bytes[4096];
    size_t total = 0;
    TsirStatus status = TSIR_OK;
    if (root_len > SIZE_MAX - sizeof(directory) - name_len) {
        return TSIR_ERR_OVERFLOW;
    }
    path = malloc(root_len + sizeof(directory) + name_len);
    if (path == NULL) {
        return TSIR_ERR_NOMEM;
    }
    memcpy(path, root, root_len);
    memcpy(path + root_len, directory, sizeof(directory) - 1);
    memcpy(path + root_len + sizeof(directory) - 1, name, name_len + 1);
    status = tsir_file_open(path, &file, &before);
    int first_errno = errno;
    free(path);
    errno = first_errno;
    if (status != TSIR_OK) {
        return status;
    }
    if (before.size > (uintmax_t)source_limit) {
        fclose(file);
        errno = 0;
        return TSIR_ERR_OVERFLOW;
    }
    for (;;) {
        size_t count = fread(bytes, 1, sizeof(bytes), file);
        if (count > source_limit - total) {
            status = TSIR_ERR_OVERFLOW;
            break;
        }
        total += count;
        tsir_sha256_update(hash, bytes, count);
        if (count < sizeof(bytes)) {
            if (ferror(file)) {
                status = TSIR_ERR_INVALID;
            }
            break;
        }
    }
    first_errno = status == TSIR_ERR_INVALID ? errno : 0;
    if (status == TSIR_OK) {
        status = tsir_file_stamp(file, &after);
        if (status != TSIR_OK) first_errno = errno;
        else if (after.size != before.size || after.modified != before.modified ||
            total != (size_t)before.size) {
            status = TSIR_ERR_INVALID;
            first_errno = EIO;
        }
    }
    if (fclose(file) != 0 && status == TSIR_OK) {
        status = TSIR_ERR_INVALID;
        first_errno = errno;
    }
    if (status != TSIR_OK) errno = first_errno;
    return status;
}

TsirStatus tsir_emitter_code_digest(TsirKernelBackend backend,
    const char *repository_root, uint8_t out[32]) {
    const char *emitter = NULL;
    bool common = false;
    TsirSha256 hash;
    TsirStatus status;
    if (repository_root == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    switch (backend) {
        case TSIR_KERNEL_WEBGPU: emitter = "emit_webgpu.zig"; break;
        case TSIR_KERNEL_MSL: emitter = "emit_msl.zig"; common = true; break;
        case TSIR_KERNEL_SPIR_V: emitter = "emit_spir_v.zig"; common = true; break;
        case TSIR_KERNEL_DXIL: emitter = "emit_dxil.zig"; common = true; break;
        case TSIR_KERNEL_CSL: return TSIR_ERR_UNSUPPORTED;
    }
    if (emitter == NULL) {
        return TSIR_ERR_INVALID;
    }
    tsir_sha256_init(&hash);
    status = hash_source_file(&hash, repository_root, emitter);
    if (status == TSIR_OK && common) {
        status = hash_source_file(&hash, repository_root, "emit_text_skeleton.zig");
    }
    if (status == TSIR_OK) {
        status = hash_source_file(&hash, repository_root, "emit_kernel_body.zig");
    }
    if (status == TSIR_OK) {
        status = hash_source_file(&hash, repository_root, "emit_semantic_metadata.zig");
    }
    if (status == TSIR_OK) {
        tsir_sha256_final(&hash, out);
    }
    return status;
}

TsirStatus tsir_target_emitter_digest(TsirBootstrapTarget target,
    const char *repository_root, uint8_t out[32]) {
    switch (target) {
        case TSIR_TARGET_WEBGPU_GENERIC:
            return tsir_emitter_code_digest(TSIR_KERNEL_WEBGPU, repository_root, out);
        case TSIR_TARGET_MSL:
            return tsir_emitter_code_digest(TSIR_KERNEL_MSL, repository_root, out);
        case TSIR_TARGET_SPIR_V:
            return tsir_emitter_code_digest(TSIR_KERNEL_SPIR_V, repository_root, out);
    }
    return TSIR_ERR_INVALID;
}
