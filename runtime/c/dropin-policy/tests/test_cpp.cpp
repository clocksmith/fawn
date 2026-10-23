#include "doe_dropin_policy_private.h"
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <type_traits>

static const DoeDropinPolicyDescriptor descriptor = {
    DOE_DROPIN_POLICY_ABI_VERSION, sizeof(DoeDropinPolicyDescriptor),
    DOE_DROPIN_TIER_COMPUTE, 0, &doe_dropin_policy_generated_config,
    nullptr, 0, nullptr, 0
};
static_assert(std::is_standard_layout<DoeDropinPolicyDescriptor>::value);
static_assert(std::is_standard_layout<DoeDropinEmbeddedConfig>::value);
static_assert(std::is_standard_layout<DoeDropinRoute>::value);
static_assert(sizeof(DoeDropinBytes[2]) == 2 * sizeof(DoeDropinBytes));

int main() {
    assert(doe_dropin_policy_embedded_config() == &doe_dropin_policy_generated_config);
    assert(doe_dropin_policy_bind(&descriptor) == DOE_DROPIN_OK);
    DoeDropinRoute route{};
    assert(doe_dropin_policy_decide(DOE_DROPIN_OWNER_METAL, DOE_DROPIN_MODE_DAWN, 0, &route) == DOE_DROPIN_OK);
    assert(route.declared_owner == DOE_DROPIN_OWNER_METAL && route.effective_owner == DOE_DROPIN_OWNER_DAWN);
    assert(route.fallback_used == 0);
    std::puts("drop-in strict C++17 consumer: passed");
}
