/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#define _POSIX_C_SOURCE 200809L
#include "tsir_targets.h"
#include "tsir_sha256.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static const char *const numerical_modes[] = { "f32", "f16" };
static const TsirNativeCollective native_collectives[] = {
    { "workgroup_barrier", "bit_exact_solo" }
};

static const TsirTargetIdentity descriptors[] = {
    {
        "webgpu-generic",
        { 16384, 0, 256, numerical_modes, 2, native_collectives, 1,
            TSIR_POLICY_HOST_COPIED },
        0, 1, NULL, 0, "none", 200
    },
    {
        "msl",
        { 16384, 0, 1024, numerical_modes, 2, native_collectives, 1,
            TSIR_POLICY_HOST_COPIED },
        0, 1, NULL, 0, "none", 150
    },
    {
        "spir-v",
        { 16384, 0, 128, numerical_modes, 2, native_collectives, 1,
            TSIR_POLICY_HOST_COPIED },
        0, 1, NULL, 0, "none", 200
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

static void hash_text(TsirSha256 *hash, const char *text) {
    tsir_sha256_update(hash, (const uint8_t *)text, strlen(text));
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
    planner = &descriptor->planner;
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
    hash_text(&hash, descriptor->name);
    hash_u64(&hash, planner->pe_working_memory_bytes);
    hash_u64(&hash, descriptor->pe_persistent_pool_bytes);
    hash_u32(&hash, planner->fabric_color_count);
    hash_u32(&hash, planner->max_collective_group_size);
    hash_u32(&hash, descriptor->sub_tile_lane_width);
    for (i = 0; i < planner->native_numerical_mode_count; i++) {
        if (planner->native_numerical_modes[i] == NULL) {
            return TSIR_ERR_INVALID;
        }
        hash_text(&hash, planner->native_numerical_modes[i]);
    }
    for (i = 0; i < planner->native_collective_count; i++) {
        const TsirNativeCollective *collective = &planner->native_collectives[i];
        if (collective->kind_name == NULL || collective->exactness_name == NULL) {
            return TSIR_ERR_INVALID;
        }
        hash_text(&hash, collective->kind_name);
        hash_text(&hash, "|");
        hash_text(&hash, collective->exactness_name);
    }
    for (i = 0; i < descriptor->fused_intrinsic_count; i++) {
        if (descriptor->fused_intrinsics[i] == NULL) {
            return TSIR_ERR_INVALID;
        }
        hash_text(&hash, descriptor->fused_intrinsics[i]);
    }
    hash_text(&hash, descriptor->streaming_gemm);
    hash_text(&hash, policy);
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
    int descriptor;
    struct stat before;
    struct stat after;
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
    descriptor = open(path, O_RDONLY | O_NONBLOCK);
    free(path);
    if (descriptor < 0) {
        return TSIR_ERR_INVALID;
    }
    if (fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_size < 0) {
        close(descriptor);
        return TSIR_ERR_INVALID;
    }
    if ((uintmax_t)before.st_size > (uintmax_t)source_limit) {
        close(descriptor);
        return TSIR_ERR_OVERFLOW;
    }
    file = fdopen(descriptor, "rb");
    if (file == NULL) {
        close(descriptor);
        return TSIR_ERR_INVALID;
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
    if (status == TSIR_OK && (fstat(descriptor, &after) != 0 ||
        after.st_size != before.st_size || after.st_mtime != before.st_mtime ||
        total != (size_t)before.st_size)) {
        status = TSIR_ERR_INVALID;
    }
    if (fclose(file) != 0 && status == TSIR_OK) {
        status = TSIR_ERR_INVALID;
    }
    return status;
}

TsirStatus tsir_target_emitter_digest(TsirBootstrapTarget target,
    const char *repository_root, uint8_t out[32]) {
    const char *emitter = NULL;
    bool common = false;
    TsirSha256 hash;
    TsirStatus status;
    if (repository_root == NULL || out == NULL) {
        return TSIR_ERR_INVALID;
    }
    switch (target) {
        case TSIR_TARGET_WEBGPU_GENERIC: emitter = "emit_webgpu.zig"; break;
        case TSIR_TARGET_MSL: emitter = "emit_msl.zig"; common = true; break;
        case TSIR_TARGET_SPIR_V: emitter = "emit_spir_v.zig"; common = true; break;
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
