#ifndef DOE_DROPIN_POLICY_H
#define DOE_DROPIN_POLICY_H

/* Proposed private owner ABI 1. Planning only, not a shipped implementation. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOE_DROPIN_POLICY_ABI_VERSION UINT32_C(1)
#define DOE_DROPIN_POLICY_ADMISSION_PROFILE UINT32_C(1)
#define DOE_DROPIN_POLICY_STRLEN SIZE_MAX
#define DOE_DROPIN_MODE_USE_DEFAULT UINT32_MAX

/* Integer constants, not compiler-sized enum objects, cross this ABI. */
enum {
    DOE_DROPIN_OWNER_DAWN = 0,
    DOE_DROPIN_OWNER_METAL = 1,
    DOE_DROPIN_OWNER_VULKAN = 2,
    DOE_DROPIN_OWNER_D3D12 = 3,
    DOE_DROPIN_OWNER_SHARED = 4
};
enum {
    DOE_DROPIN_MODE_DAWN = 0,
    DOE_DROPIN_MODE_MIXED = 1,
    DOE_DROPIN_MODE_METAL = 2,
    DOE_DROPIN_MODE_VULKAN = 3,
    DOE_DROPIN_MODE_D3D12 = 4
};
enum {
    DOE_DROPIN_TIER_COMPUTE = 0,
    DOE_DROPIN_TIER_HEADLESS = 1,
    DOE_DROPIN_TIER_FULL = 2
};
enum {
    DOE_DROPIN_OK = 0,
    DOE_DROPIN_INVALID_ARGUMENT = 1,
    DOE_DROPIN_ABI_MISMATCH = 2,
    DOE_DROPIN_INVALID_DESCRIPTOR = 3,
    DOE_DROPIN_ALREADY_BOUND = 4,
    DOE_DROPIN_NOT_BOUND = 5,
    DOE_DROPIN_FATAL_DEFAULT_MODE = 6,
    DOE_DROPIN_FATAL_LANE_MODE = 7,
    DOE_DROPIN_FATAL_MANIFEST_CONFLICT = 8
};
typedef uint32_t DoeDropinStatus;

typedef struct DoeDropinBytes {
    const uint8_t *data;
    size_t length;
} DoeDropinBytes;

typedef struct DoeDropinOwnership {
    DoeDropinBytes symbol;
    uint32_t owner;
} DoeDropinOwnership;

typedef struct DoeDropinLaneName {
    DoeDropinBytes spelling;
    DoeDropinBytes canonical;
} DoeDropinLaneName;

typedef struct DoeDropinLaneMode {
    DoeDropinBytes key;
    uint32_t mode; /* A mode constant or DOE_DROPIN_MODE_USE_DEFAULT. */
} DoeDropinLaneMode;

/* Generated immutable input. Syntax failures are data, not generation errors. */
typedef struct DoeDropinEmbeddedConfig {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t admission_profile;
    uint32_t behavior_status; /* OK, FATAL_DEFAULT_MODE, or FATAL_LANE_MODE. */
    uint32_t default_mode;
    uint32_t strict_no_fallback; /* Exactly 0 or 1. */
    uint32_t observe_environment; /* Exactly 0 or 1. */
    uint32_t reserved; /* Must be zero. */
    const DoeDropinLaneMode *lane_modes;
    size_t lane_mode_count;
    const DoeDropinOwnership *ownership;
    size_t ownership_count;
    uint8_t behavior_sha256[32];
    uint8_t ownership_sha256[32];
} DoeDropinEmbeddedConfig;

/* All storage reachable here is borrowed, immutable, and image-lifetime. */
typedef struct DoeDropinPolicyDescriptor {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t active_tier;
    uint32_t reserved; /* Must be zero. */
    const DoeDropinEmbeddedConfig *config;
    const DoeDropinOwnership *manifest;
    size_t manifest_count;
    const DoeDropinLaneName *lane_names;
    size_t lane_name_count;
} DoeDropinPolicyDescriptor;

typedef struct DoeDropinBehavior {
    uint32_t mode;
    uint32_t strict_no_fallback;
} DoeDropinBehavior;

typedef struct DoeDropinRoute {
    uint32_t declared_owner;
    uint32_t effective_owner;
    uint32_t mode;
    uint32_t strict_no_fallback;
    uint32_t fallback_used; /* Always zero in ABI 1, including Dawn reroutes. */
    uint32_t valid_name; /* View admission, not whether a proc exists. */
} DoeDropinRoute;

/* Pure static-data accessor. It does not bind or initialize either policy. */
const DoeDropinEmbeddedConfig *doe_dropin_policy_embedded_config(void);

/* Idempotent only for this exact descriptor address. No reset or rebind. */
DoeDropinStatus doe_dropin_policy_bind(const DoeDropinPolicyDescriptor *descriptor);
DoeDropinStatus doe_dropin_policy_behavior(DoeDropinBehavior *out);
DoeDropinStatus doe_dropin_policy_owner(DoeDropinBytes name, uint32_t *out_owner);
DoeDropinStatus doe_dropin_policy_route_name(DoeDropinBytes name, DoeDropinRoute *out);
DoeDropinStatus doe_dropin_policy_route_view(
    const uint8_t *data, size_t length, DoeDropinRoute *out);

/* Pure total decision on valid codes; no binding/config/environment observation. */
DoeDropinStatus doe_dropin_policy_decide(
    uint32_t owner, uint32_t mode, uint32_t strict_no_fallback,
    DoeDropinRoute *out);

/* Borrowed static NUL-terminated text; unknown status has fixed generic text. */
const char *doe_dropin_policy_status_message(DoeDropinStatus status);

#ifdef __cplusplus
}
#endif
#endif
