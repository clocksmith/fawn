/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors
 * Derived ownership contract and source attribution: ../PROVENANCE.md. */
#include "doe_dropin_policy_private.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One owner per provider image. The archive is compiled with hidden visibility. */
static _Atomic(const DoeDropinPolicyDescriptor *) bound_descriptor;
static pthread_mutex_t cold_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint behavior_publication;
static atomic_uint ownership_publication;
static DoeDropinBehavior published_behavior;

static void lock_cold(void) {
    if (pthread_mutex_lock(&cold_lock) != 0) {
        fputs("drop-in policy synchronization failure\n", stderr);
        abort();
    }
}

static void unlock_cold(void) {
    if (pthread_mutex_unlock(&cold_lock) != 0) {
        fputs("drop-in policy synchronization failure\n", stderr);
        abort();
    }
}

static bool valid_owner(uint32_t owner) {
    return owner <= DOE_DROPIN_OWNER_SHARED;
}

static bool valid_mode(uint32_t mode) {
    return mode <= DOE_DROPIN_MODE_D3D12;
}

static bool valid_bytes(DoeDropinBytes bytes) {
    return bytes.length == 0 || bytes.data != NULL;
}

static bool same_bytes(DoeDropinBytes left, DoeDropinBytes right) {
    return left.length == right.length &&
        (left.length == 0 || memcmp(left.data, right.data, left.length) == 0);
}

static bool valid_array(bool has_data, size_t count, size_t stride) {
    return count <= SIZE_MAX / stride && (count == 0 || has_data);
}

static bool valid_lane_name(DoeDropinBytes name) {
    if (!valid_bytes(name) || name.length == 0 ||
        name.length > PTRDIFF_MAX || name.length > SIZE_MAX - 1) return false;
    for (size_t i = 0; i < name.length; ++i) {
        uint8_t byte = name.data[i];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') || byte == '_')) return false;
    }
    return true;
}

static DoeDropinStatus validate_config(const DoeDropinEmbeddedConfig *config) {
    if (config == NULL) return DOE_DROPIN_INVALID_DESCRIPTOR;
    if (config->abi_version != DOE_DROPIN_POLICY_ABI_VERSION ||
        config->struct_size != sizeof(*config)) return DOE_DROPIN_ABI_MISMATCH;
    if (config->admission_profile != DOE_DROPIN_POLICY_ADMISSION_PROFILE ||
        !valid_mode(config->default_mode) || config->strict_no_fallback > 1 ||
        config->observe_environment > 1 || config->reserved != 0)
        return DOE_DROPIN_INVALID_DESCRIPTOR;
    if (config->behavior_status != DOE_DROPIN_OK &&
        config->behavior_status != DOE_DROPIN_FATAL_DEFAULT_MODE &&
        config->behavior_status != DOE_DROPIN_FATAL_LANE_MODE)
        return DOE_DROPIN_INVALID_DESCRIPTOR;
    if (config->behavior_status != DOE_DROPIN_OK &&
        (config->observe_environment != 0 || config->lane_mode_count != 0))
        return DOE_DROPIN_INVALID_DESCRIPTOR;
    if (!valid_array(config->lane_modes != NULL, config->lane_mode_count,
                     sizeof(*config->lane_modes)) ||
        !valid_array(config->ownership != NULL, config->ownership_count,
                     sizeof(*config->ownership))) return DOE_DROPIN_INVALID_DESCRIPTOR;
    for (size_t i = 0; i < config->lane_mode_count; ++i) {
        const DoeDropinLaneMode *row = &config->lane_modes[i];
        if (!valid_bytes(row->key) ||
            (!valid_mode(row->mode) && row->mode != DOE_DROPIN_MODE_USE_DEFAULT))
            return DOE_DROPIN_INVALID_DESCRIPTOR;
        for (size_t j = 0; j < i; ++j)
            if (same_bytes(row->key, config->lane_modes[j].key))
                return DOE_DROPIN_INVALID_DESCRIPTOR;
    }
    for (size_t i = 0; i < config->ownership_count; ++i) {
        const DoeDropinOwnership *row = &config->ownership[i];
        if (!valid_bytes(row->symbol) || !valid_owner(row->owner))
            return DOE_DROPIN_INVALID_DESCRIPTOR;
    }
    return DOE_DROPIN_OK;
}

static DoeDropinStatus validate_manifest(const DoeDropinPolicyDescriptor *descriptor) {
    if (!valid_array(descriptor->manifest != NULL, descriptor->manifest_count,
                     sizeof(*descriptor->manifest))) return DOE_DROPIN_INVALID_DESCRIPTOR;
    for (size_t i = 0; i < descriptor->manifest_count; ++i) {
        const DoeDropinOwnership *row = &descriptor->manifest[i];
        if (!valid_bytes(row->symbol) || row->symbol.length == 0 ||
            !valid_owner(row->owner)) return DOE_DROPIN_INVALID_DESCRIPTOR;
        for (size_t j = 0; j < i; ++j)
            if (same_bytes(row->symbol, descriptor->manifest[j].symbol))
                return DOE_DROPIN_INVALID_DESCRIPTOR;
    }
    return DOE_DROPIN_OK;
}

static DoeDropinStatus validate_lanes(const DoeDropinPolicyDescriptor *descriptor) {
    if (!valid_array(descriptor->lane_names != NULL, descriptor->lane_name_count,
                     sizeof(*descriptor->lane_names))) return DOE_DROPIN_INVALID_DESCRIPTOR;
    for (size_t i = 0; i < descriptor->lane_name_count; ++i) {
        const DoeDropinLaneName *row = &descriptor->lane_names[i];
        if (!valid_lane_name(row->spelling) || !valid_lane_name(row->canonical))
            return DOE_DROPIN_INVALID_DESCRIPTOR;
        for (size_t j = 0; j < i; ++j)
            if (same_bytes(row->spelling, descriptor->lane_names[j].spelling))
                return DOE_DROPIN_INVALID_DESCRIPTOR;
        bool has_canonical = false;
        for (size_t j = 0; j < descriptor->lane_name_count; ++j) {
            const DoeDropinLaneName *candidate = &descriptor->lane_names[j];
            /* Validate before comparing a forward row's borrowed spans. */
            if (!valid_lane_name(candidate->spelling) ||
                !valid_lane_name(candidate->canonical)) return DOE_DROPIN_INVALID_DESCRIPTOR;
            if (same_bytes(row->canonical, candidate->spelling) &&
                same_bytes(candidate->spelling, candidate->canonical))
                has_canonical = true;
        }
        if (!has_canonical) return DOE_DROPIN_INVALID_DESCRIPTOR;
    }
    return DOE_DROPIN_OK;
}

static DoeDropinStatus validate_descriptor(const DoeDropinPolicyDescriptor *descriptor) {
    if (descriptor->abi_version != DOE_DROPIN_POLICY_ABI_VERSION ||
        descriptor->struct_size != sizeof(*descriptor)) return DOE_DROPIN_ABI_MISMATCH;
    if (descriptor->active_tier > DOE_DROPIN_TIER_FULL || descriptor->reserved != 0)
        return DOE_DROPIN_INVALID_DESCRIPTOR;
    DoeDropinStatus status = validate_config(descriptor->config);
    if (status != DOE_DROPIN_OK) return status;
    status = validate_manifest(descriptor);
    if (status != DOE_DROPIN_OK) return status;
    return validate_lanes(descriptor);
}

DoeDropinStatus doe_dropin_policy_bind(const DoeDropinPolicyDescriptor *descriptor) {
    if (descriptor == NULL) return DOE_DROPIN_INVALID_ARGUMENT;
    const DoeDropinPolicyDescriptor *current =
        atomic_load_explicit(&bound_descriptor, memory_order_acquire);
    if (current != NULL)
        return current == descriptor ? DOE_DROPIN_OK : DOE_DROPIN_ALREADY_BOUND;
    lock_cold();
    current = atomic_load_explicit(&bound_descriptor, memory_order_acquire);
    DoeDropinStatus status;
    if (current != NULL) {
        status = current == descriptor ? DOE_DROPIN_OK : DOE_DROPIN_ALREADY_BOUND;
    } else {
        status = validate_descriptor(descriptor);
        if (status == DOE_DROPIN_OK)
            atomic_store_explicit(&bound_descriptor, descriptor, memory_order_release);
    }
    unlock_cold();
    return status;
}

static const DoeDropinOwnership *find_owner(
    const DoeDropinOwnership *rows, size_t count, DoeDropinBytes name) {
    for (size_t i = 0; i < count; ++i)
        if (same_bytes(rows[i].symbol, name)) return &rows[i];
    return NULL;
}

static DoeDropinStatus initialize_ownership(const DoeDropinPolicyDescriptor *descriptor) {
    unsigned int publication = atomic_load_explicit(&ownership_publication, memory_order_acquire);
    if (publication != 0) return publication - 1;
    lock_cold();
    publication = atomic_load_explicit(&ownership_publication, memory_order_acquire);
    if (publication == 0) {
        DoeDropinStatus status = DOE_DROPIN_OK;
        const DoeDropinEmbeddedConfig *config = descriptor->config;
        for (size_t i = 0; i < config->ownership_count; ++i) {
            const DoeDropinOwnership *row = &config->ownership[i];
            const DoeDropinOwnership *manifest = find_owner(
                descriptor->manifest, descriptor->manifest_count, row->symbol);
            if (manifest != NULL && manifest->owner != row->owner) {
                status = DOE_DROPIN_FATAL_MANIFEST_CONFLICT;
                break;
            }
        }
        publication = status + 1;
        atomic_store_explicit(&ownership_publication, publication, memory_order_release);
    }
    unlock_cold();
    return publication - 1;
}

static uint8_t normalized_lane_byte(uint8_t byte) {
    if (byte == '-') return '_';
    if (byte >= 'A' && byte <= 'Z') return (uint8_t)(byte + ('a' - 'A'));
    return byte;
}

static bool matches_lane(const char *value, size_t length, DoeDropinBytes spelling) {
    if (length != spelling.length) return false;
    for (size_t i = 0; i < length; ++i)
        if (normalized_lane_byte((uint8_t)value[i]) != spelling.data[i]) return false;
    return true;
}

static uint32_t capture_mode(const DoeDropinPolicyDescriptor *descriptor) {
    const DoeDropinEmbeddedConfig *config = descriptor->config;
    if (config->observe_environment == 0) return config->default_mode;
    /* This is the sole observation; no environment pointer survives this call. */
    const char *value = getenv("FAWN_BACKEND_LANE");
    if (value == NULL) return config->default_mode;
    size_t maximum = 0;
    for (size_t i = 0; i < descriptor->lane_name_count; ++i)
        if (descriptor->lane_names[i].spelling.length > maximum)
            maximum = descriptor->lane_names[i].spelling.length;
    size_t length = 0;
    while (length <= maximum && value[length] != '\0') ++length;
    if (length == 0 || length > maximum) return config->default_mode;
    for (size_t i = 0; i < descriptor->lane_name_count; ++i) {
        const DoeDropinLaneName *lane = &descriptor->lane_names[i];
        if (!matches_lane(value, length, lane->spelling)) continue;
        for (size_t j = 0; j < config->lane_mode_count; ++j) {
            const DoeDropinLaneMode *mode = &config->lane_modes[j];
            if (same_bytes(mode->key, lane->canonical))
                return mode->mode == DOE_DROPIN_MODE_USE_DEFAULT ?
                    config->default_mode : mode->mode;
        }
        break;
    }
    return config->default_mode;
}

static DoeDropinStatus read_behavior(
    const DoeDropinPolicyDescriptor *descriptor, DoeDropinBehavior *out) {
    unsigned int publication = atomic_load_explicit(&behavior_publication, memory_order_acquire);
    if (publication == 0) {
        lock_cold();
        publication = atomic_load_explicit(&behavior_publication, memory_order_acquire);
        if (publication == 0) {
            DoeDropinStatus status = descriptor->config->behavior_status;
            if (status == DOE_DROPIN_OK) {
                published_behavior.mode = capture_mode(descriptor);
                published_behavior.strict_no_fallback = descriptor->config->strict_no_fallback;
            }
            publication = status + 1;
            atomic_store_explicit(&behavior_publication, publication, memory_order_release);
        }
        unlock_cold();
    }
    DoeDropinStatus status = publication - 1;
    if (status == DOE_DROPIN_OK) *out = published_behavior;
    return status;
}

DoeDropinStatus doe_dropin_policy_behavior(DoeDropinBehavior *out) {
    if (out == NULL) return DOE_DROPIN_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    const DoeDropinPolicyDescriptor *descriptor =
        atomic_load_explicit(&bound_descriptor, memory_order_acquire);
    if (descriptor == NULL) return DOE_DROPIN_NOT_BOUND;
    return read_behavior(descriptor, out);
}

static DoeDropinStatus read_owner(
    const DoeDropinPolicyDescriptor *descriptor, DoeDropinBytes name, uint32_t *out) {
    const DoeDropinOwnership *row = find_owner(
        descriptor->manifest, descriptor->manifest_count, name);
    if (row != NULL) {
        *out = row->owner;
        return DOE_DROPIN_OK;
    }
    DoeDropinStatus status = initialize_ownership(descriptor);
    if (status != DOE_DROPIN_OK) return status;
    row = find_owner(descriptor->config->ownership, descriptor->config->ownership_count, name);
    *out = row == NULL ? DOE_DROPIN_OWNER_SHARED : row->owner;
    return DOE_DROPIN_OK;
}

DoeDropinStatus doe_dropin_policy_owner(DoeDropinBytes name, uint32_t *out_owner) {
    if (out_owner == NULL) return DOE_DROPIN_INVALID_ARGUMENT;
    *out_owner = 0;
    if (!valid_bytes(name)) return DOE_DROPIN_INVALID_ARGUMENT;
    const DoeDropinPolicyDescriptor *descriptor =
        atomic_load_explicit(&bound_descriptor, memory_order_acquire);
    if (descriptor == NULL) return DOE_DROPIN_NOT_BOUND;
    return read_owner(descriptor, name, out_owner);
}

DoeDropinStatus doe_dropin_policy_decide(
    uint32_t owner, uint32_t mode, uint32_t strict_no_fallback, DoeDropinRoute *out) {
    if (out == NULL) return DOE_DROPIN_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!valid_owner(owner) || !valid_mode(mode) || strict_no_fallback > 1)
        return DOE_DROPIN_INVALID_ARGUMENT;
    out->declared_owner = owner;
    out->effective_owner = owner;
    out->mode = mode;
    out->strict_no_fallback = strict_no_fallback;
    out->valid_name = 1;
    if (strict_no_fallback == 0 && owner != DOE_DROPIN_OWNER_SHARED &&
        (mode == DOE_DROPIN_MODE_DAWN || mode == DOE_DROPIN_MODE_MIXED))
        out->effective_owner = DOE_DROPIN_OWNER_DAWN;
    return DOE_DROPIN_OK;
}

static DoeDropinStatus read_route(
    const DoeDropinPolicyDescriptor *descriptor, DoeDropinBytes name, DoeDropinRoute *out) {
    uint32_t owner;
    DoeDropinStatus status = read_owner(descriptor, name, &owner);
    if (status != DOE_DROPIN_OK) return status;
    DoeDropinBehavior behavior;
    status = read_behavior(descriptor, &behavior);
    if (status != DOE_DROPIN_OK) return status;
    return doe_dropin_policy_decide(owner, behavior.mode, behavior.strict_no_fallback, out);
}

DoeDropinStatus doe_dropin_policy_route_name(DoeDropinBytes name, DoeDropinRoute *out) {
    if (out == NULL) return DOE_DROPIN_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!valid_bytes(name)) return DOE_DROPIN_INVALID_ARGUMENT;
    const DoeDropinPolicyDescriptor *descriptor =
        atomic_load_explicit(&bound_descriptor, memory_order_acquire);
    if (descriptor == NULL) return DOE_DROPIN_NOT_BOUND;
    return read_route(descriptor, name, out);
}

DoeDropinStatus doe_dropin_policy_route_view(
    const uint8_t *data, size_t length, DoeDropinRoute *out) {
    if (out == NULL) return DOE_DROPIN_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    const DoeDropinPolicyDescriptor *descriptor =
        atomic_load_explicit(&bound_descriptor, memory_order_acquire);
    if (descriptor == NULL) return DOE_DROPIN_NOT_BOUND;
    if (data == NULL || length == 0) {
        DoeDropinBehavior behavior;
        DoeDropinStatus status = read_behavior(descriptor, &behavior);
        if (status != DOE_DROPIN_OK) return status;
        status = doe_dropin_policy_decide(DOE_DROPIN_OWNER_SHARED, behavior.mode, 0, out);
        out->valid_name = 0;
        return status;
    }
    if (length == DOE_DROPIN_POLICY_STRLEN) length = strlen((const char *)data);
    return read_route(descriptor, (DoeDropinBytes){ data, length }, out);
}

const char *doe_dropin_policy_status_message(DoeDropinStatus status) {
    switch (status) {
        case DOE_DROPIN_OK: return "drop-in policy: OK";
        case DOE_DROPIN_INVALID_ARGUMENT: return "drop-in policy: invalid argument";
        case DOE_DROPIN_ABI_MISMATCH: return "drop-in policy: ABI mismatch";
        case DOE_DROPIN_INVALID_DESCRIPTOR: return "drop-in policy: invalid descriptor";
        case DOE_DROPIN_ALREADY_BOUND: return "drop-in policy: already bound to another descriptor";
        case DOE_DROPIN_NOT_BOUND: return "drop-in policy: not bound";
        case DOE_DROPIN_FATAL_DEFAULT_MODE:
            return "dropin-abi-behavior.json: unrecognized defaultMode value";
        case DOE_DROPIN_FATAL_LANE_MODE:
            return "dropin-abi-behavior.json: unrecognized laneModes value";
        case DOE_DROPIN_FATAL_MANIFEST_CONFLICT:
            return "drop-in symbol ownership config disagrees with the proc manifest";
        default: return "drop-in policy: unknown status";
    }
}

const DoeDropinEmbeddedConfig *doe_dropin_policy_embedded_config(void) {
    return &doe_dropin_policy_generated_config;
}
