#include "doe_dropin_policy_private.h"
#include "webgpu.h"
#include <assert.h>
#include <stdio.h>

#define RECORD(type) printf(#type " size=%zu align=%zu stride=%zu\n", sizeof(type), _Alignof(type), sizeof(type[2]) / 2)
#define FIELD(type, field) printf(#type "." #field " offset=%zu size=%zu\n", offsetof(type, field), sizeof(((type *)0)->field))
int main(void) {
    RECORD(DoeDropinBytes); FIELD(DoeDropinBytes, data); FIELD(DoeDropinBytes, length);
    RECORD(DoeDropinOwnership); FIELD(DoeDropinOwnership, symbol); FIELD(DoeDropinOwnership, owner);
    RECORD(DoeDropinLaneName); FIELD(DoeDropinLaneName, spelling); FIELD(DoeDropinLaneName, canonical);
    RECORD(DoeDropinLaneMode); FIELD(DoeDropinLaneMode, key); FIELD(DoeDropinLaneMode, mode);
    RECORD(DoeDropinEmbeddedConfig);
    FIELD(DoeDropinEmbeddedConfig, abi_version); FIELD(DoeDropinEmbeddedConfig, struct_size);
    FIELD(DoeDropinEmbeddedConfig, admission_profile); FIELD(DoeDropinEmbeddedConfig, behavior_status);
    FIELD(DoeDropinEmbeddedConfig, default_mode); FIELD(DoeDropinEmbeddedConfig, strict_no_fallback);
    FIELD(DoeDropinEmbeddedConfig, observe_environment); FIELD(DoeDropinEmbeddedConfig, reserved);
    FIELD(DoeDropinEmbeddedConfig, lane_modes); FIELD(DoeDropinEmbeddedConfig, lane_mode_count);
    FIELD(DoeDropinEmbeddedConfig, ownership); FIELD(DoeDropinEmbeddedConfig, ownership_count);
    FIELD(DoeDropinEmbeddedConfig, behavior_sha256); FIELD(DoeDropinEmbeddedConfig, ownership_sha256);
    RECORD(DoeDropinPolicyDescriptor);
    FIELD(DoeDropinPolicyDescriptor, abi_version); FIELD(DoeDropinPolicyDescriptor, struct_size);
    FIELD(DoeDropinPolicyDescriptor, active_tier); FIELD(DoeDropinPolicyDescriptor, reserved);
    FIELD(DoeDropinPolicyDescriptor, config); FIELD(DoeDropinPolicyDescriptor, manifest);
    FIELD(DoeDropinPolicyDescriptor, manifest_count); FIELD(DoeDropinPolicyDescriptor, lane_names);
    FIELD(DoeDropinPolicyDescriptor, lane_name_count);
    RECORD(DoeDropinBehavior); FIELD(DoeDropinBehavior, mode); FIELD(DoeDropinBehavior, strict_no_fallback);
    RECORD(DoeDropinRoute); FIELD(DoeDropinRoute, declared_owner); FIELD(DoeDropinRoute, effective_owner);
    FIELD(DoeDropinRoute, mode); FIELD(DoeDropinRoute, strict_no_fallback);
    FIELD(DoeDropinRoute, fallback_used); FIELD(DoeDropinRoute, valid_name);
    RECORD(WGPUStringView); FIELD(WGPUStringView, data); FIELD(WGPUStringView, length);
    assert(WGPU_STRLEN == DOE_DROPIN_POLICY_STRLEN);
    assert(doe_dropin_policy_embedded_config() == &doe_dropin_policy_generated_config);
    puts("drop-in C ABI and pinned WGPU view probe: passed");
}
