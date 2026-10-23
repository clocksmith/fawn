/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "doe_dropin_policy.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define BYTES(text) { (const uint8_t *)(text), sizeof(text) - 1 }
static const DoeDropinOwnership manifest[] = {
    { BYTES("native"), DOE_DROPIN_OWNER_SHARED }
};
static const DoeDropinLaneName lanes[] = {
    { BYTES("vulkan_dawn_release"), BYTES("vulkan_dawn_release") },
    { BYTES("vulkan_dawn_directional"), BYTES("vulkan_dawn_release") }
};
static const DoeDropinLaneMode modes[] = {
    { BYTES("vulkan_dawn_release"), DOE_DROPIN_MODE_METAL },
    { BYTES("vulkan_dawn_directional"), DOE_DROPIN_MODE_D3D12 }
};
static const DoeDropinOwnership owners[] = {
    { BYTES("only_json"), DOE_DROPIN_OWNER_VULKAN },
    { BYTES("only_json"), DOE_DROPIN_OWNER_DAWN },
    { BYTES(""), DOE_DROPIN_OWNER_D3D12 },
    { BYTES("x\0y"), DOE_DROPIN_OWNER_METAL }
};
static const DoeDropinEmbeddedConfig config = {
    .abi_version = DOE_DROPIN_POLICY_ABI_VERSION,
    .struct_size = sizeof(DoeDropinEmbeddedConfig),
    .admission_profile = DOE_DROPIN_POLICY_ADMISSION_PROFILE,
    .behavior_status = DOE_DROPIN_OK,
    .default_mode = DOE_DROPIN_MODE_DAWN,
    .strict_no_fallback = 0,
    .observe_environment = 1,
    .lane_modes = modes,
    .lane_mode_count = sizeof(modes) / sizeof(modes[0]),
    .ownership = owners,
    .ownership_count = sizeof(owners) / sizeof(owners[0])
};
static const DoeDropinPolicyDescriptor descriptor = {
    .abi_version = DOE_DROPIN_POLICY_ABI_VERSION,
    .struct_size = sizeof(DoeDropinPolicyDescriptor),
    .active_tier = DOE_DROPIN_TIER_HEADLESS,
    .config = &config,
    .manifest = manifest,
    .manifest_count = sizeof(manifest) / sizeof(manifest[0]),
    .lane_names = lanes,
    .lane_name_count = sizeof(lanes) / sizeof(lanes[0])
};
static atomic_uint environment_reads;
char *__real_getenv(const char *name);
char *__wrap_getenv(const char *name);
char *__wrap_getenv(const char *name) {
    assert(strcmp(name, "FAWN_BACKEND_LANE") == 0);
    atomic_fetch_add(&environment_reads, 1);
    return __real_getenv(name);
}

static DoeDropinBytes name(const char *value) {
    return (DoeDropinBytes){ (const uint8_t *)value, strlen(value) };
}

static void assert_zero_route(const DoeDropinRoute *route) {
    const unsigned char *bytes = (const unsigned char *)route;
    for (size_t i = 0; i < sizeof(*route); ++i) assert(bytes[i] == 0);
}

static void decisions(void) {
    /* Independent explicit expected effective owners for nonstrict inputs. */
    static const uint32_t effective[5][5] = {
        { 0, 0, 0, 0, 4 }, { 0, 0, 0, 0, 4 },
        { 0, 1, 2, 3, 4 }, { 0, 1, 2, 3, 4 }, { 0, 1, 2, 3, 4 }
    };
    for (uint32_t owner = 0; owner < 5; ++owner)
        for (uint32_t mode = 0; mode < 5; ++mode)
            for (uint32_t strict = 0; strict < 2; ++strict) {
                DoeDropinRoute route;
                assert(doe_dropin_policy_decide(owner, mode, strict, &route) == DOE_DROPIN_OK);
                assert(route.declared_owner == owner);
                assert(route.effective_owner == (strict ? owner : effective[mode][owner]));
                assert(route.mode == mode && route.strict_no_fallback == strict);
                assert(route.fallback_used == 0 && route.valid_name == 1);
            }
    DoeDropinRoute route;
    memset(&route, 0xFF, sizeof(route));
    assert(doe_dropin_policy_decide(5, 0, 0, &route) == DOE_DROPIN_INVALID_ARGUMENT);
    assert_zero_route(&route);
    assert(doe_dropin_policy_decide(0, 5, 0, &route) == DOE_DROPIN_INVALID_ARGUMENT);
    assert(doe_dropin_policy_decide(0, 0, 2, &route) == DOE_DROPIN_INVALID_ARGUMENT);
    assert(doe_dropin_policy_decide(0, 0, 0, NULL) == DOE_DROPIN_INVALID_ARGUMENT);
    assert(atomic_load(&environment_reads) == 0);
}

static void admission(void) {
    DoeDropinRoute route;
    DoeDropinBehavior behavior = {99, 99};
    uint32_t owner = 99;
    assert(doe_dropin_policy_owner((DoeDropinBytes){ NULL, 1 }, &owner) == DOE_DROPIN_INVALID_ARGUMENT);
    assert(owner == 0);
    assert(doe_dropin_policy_owner((DoeDropinBytes){ NULL, 0 }, &owner) == DOE_DROPIN_NOT_BOUND);
    assert(doe_dropin_policy_route_name((DoeDropinBytes){ NULL, 1 }, &route) == DOE_DROPIN_INVALID_ARGUMENT);
    assert(doe_dropin_policy_route_view(NULL, SIZE_MAX, &route) == DOE_DROPIN_NOT_BOUND);
    assert_zero_route(&route);
    assert(doe_dropin_policy_behavior(&behavior) == DOE_DROPIN_NOT_BOUND);
    assert(behavior.mode == 0 && behavior.strict_no_fallback == 0);
    assert(doe_dropin_policy_bind(NULL) == DOE_DROPIN_INVALID_ARGUMENT);
    static DoeDropinPolicyDescriptor rejected;
    rejected = descriptor;
    rejected.abi_version = 99;
    assert(doe_dropin_policy_bind(&rejected) == DOE_DROPIN_ABI_MISMATCH);
    rejected = descriptor;
    rejected.manifest_count = SIZE_MAX;
    assert(doe_dropin_policy_bind(&rejected) == DOE_DROPIN_INVALID_DESCRIPTOR);
    assert(doe_dropin_policy_bind(&descriptor) == DOE_DROPIN_OK);
    assert(doe_dropin_policy_bind(&descriptor) == DOE_DROPIN_OK);
    assert(doe_dropin_policy_bind(&rejected) == DOE_DROPIN_ALREADY_BOUND);
    assert(doe_dropin_policy_bind(NULL) == DOE_DROPIN_INVALID_ARGUMENT);
    assert(atomic_load(&environment_reads) == 0);
}

static void views(void) {
    assert(setenv("FAWN_BACKEND_LANE", "VULKAN-DAWN-DIRECTIONAL", 1) == 0);
    assert(doe_dropin_policy_bind(&descriptor) == DOE_DROPIN_OK);
    uint32_t owner;
    assert(doe_dropin_policy_owner(name("native"), &owner) == DOE_DROPIN_OK);
    assert(owner == DOE_DROPIN_OWNER_SHARED);
    assert(doe_dropin_policy_owner(name("only_json"), &owner) == DOE_DROPIN_OK);
    assert(owner == DOE_DROPIN_OWNER_VULKAN);
    assert(atomic_load(&environment_reads) == 0);
    DoeDropinRoute route;
    assert(doe_dropin_policy_route_view(NULL, 99, &route) == DOE_DROPIN_OK);
    assert(route.valid_name == 0 && route.strict_no_fallback == 0);
    assert(route.declared_owner == DOE_DROPIN_OWNER_SHARED && route.mode == DOE_DROPIN_MODE_METAL);
    assert(doe_dropin_policy_route_view((const uint8_t *)"", 0, &route) == DOE_DROPIN_OK);
    assert(route.valid_name == 0);
    assert(doe_dropin_policy_route_view((const uint8_t *)"", SIZE_MAX, &route) == DOE_DROPIN_OK);
    assert(route.valid_name == 1 && route.declared_owner == DOE_DROPIN_OWNER_D3D12);
    assert(doe_dropin_policy_route_view((const uint8_t *)"x\0y", 3, &route) == DOE_DROPIN_OK);
    assert(route.declared_owner == DOE_DROPIN_OWNER_METAL);
    assert(setenv("FAWN_BACKEND_LANE", "unknown", 1) == 0);
    DoeDropinBehavior behavior;
    assert(doe_dropin_policy_behavior(&behavior) == DOE_DROPIN_OK);
    assert(behavior.mode == DOE_DROPIN_MODE_METAL);
    assert(atomic_load(&environment_reads) == 1);
    char transient[] = "only_json";
    assert(doe_dropin_policy_owner(name(transient), &owner) == DOE_DROPIN_OK);
    memset(transient, 0, sizeof(transient));
    assert(doe_dropin_policy_owner(name("only_json"), &owner) == DOE_DROPIN_OK);
    assert(owner == DOE_DROPIN_OWNER_VULKAN);
}

static void lazy_conflict(void) {
    static const DoeDropinOwnership conflict[] = {
        { BYTES("safe"), DOE_DROPIN_OWNER_METAL },
        { BYTES("native"), DOE_DROPIN_OWNER_SHARED },
        { BYTES("native"), DOE_DROPIN_OWNER_DAWN }
    };
    static DoeDropinEmbeddedConfig bad;
    static DoeDropinPolicyDescriptor selected;
    bad = config;
    bad.ownership = conflict;
    bad.ownership_count = sizeof(conflict) / sizeof(conflict[0]);
    bad.behavior_status = DOE_DROPIN_FATAL_DEFAULT_MODE;
    bad.observe_environment = 0;
    bad.lane_modes = NULL;
    bad.lane_mode_count = 0;
    selected = descriptor;
    selected.config = &bad;
    assert(doe_dropin_policy_bind(&selected) == DOE_DROPIN_OK);
    uint32_t owner;
    assert(doe_dropin_policy_owner(name("native"), &owner) == DOE_DROPIN_OK);
    assert(owner == DOE_DROPIN_OWNER_SHARED);
    DoeDropinRoute route;
    assert(doe_dropin_policy_route_name(name("safe"), &route) == DOE_DROPIN_FATAL_MANIFEST_CONFLICT);
    assert_zero_route(&route);
    assert(doe_dropin_policy_route_view(NULL, 1, &route) == DOE_DROPIN_FATAL_DEFAULT_MODE);
    assert_zero_route(&route);
    assert(doe_dropin_policy_owner(name("other"), &owner) == DOE_DROPIN_FATAL_MANIFEST_CONFLICT);
    assert(owner == 0 && atomic_load(&environment_reads) == 0);
}

static void no_environment(void) {
    static DoeDropinEmbeddedConfig local;
    static DoeDropinPolicyDescriptor selected;
    local = config;
    local.default_mode = DOE_DROPIN_MODE_METAL;
    local.observe_environment = 0;
    local.lane_modes = NULL;
    local.lane_mode_count = 0;
    selected = descriptor;
    selected.config = &local;
    assert(doe_dropin_policy_bind(&selected) == DOE_DROPIN_OK);
    DoeDropinBehavior behavior;
    assert(doe_dropin_policy_behavior(&behavior) == DOE_DROPIN_OK);
    assert(behavior.mode == DOE_DROPIN_MODE_METAL);
    assert(atomic_load(&environment_reads) == 0);
}

static void *concurrent_query(void *unused) {
    (void)unused;
    for (unsigned int i = 0; i < 2000; ++i) {
        assert(doe_dropin_policy_bind(&descriptor) == DOE_DROPIN_OK);
        DoeDropinRoute route;
        assert(doe_dropin_policy_route_name(name(i % 2 ? "native" : "only_json"), &route) == DOE_DROPIN_OK);
        assert(route.mode == DOE_DROPIN_MODE_METAL && route.fallback_used == 0);
        assert(route.declared_owner == (i % 2 ? DOE_DROPIN_OWNER_SHARED : DOE_DROPIN_OWNER_VULKAN));
    }
    return NULL;
}

static void concurrency(void) {
    assert(setenv("FAWN_BACKEND_LANE", "VULKAN-DAWN-DIRECTIONAL", 1) == 0);
    pthread_t threads[24];
    for (size_t i = 0; i < 24; ++i) assert(pthread_create(&threads[i], NULL, concurrent_query, NULL) == 0);
    for (size_t i = 0; i < 24; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert(atomic_load(&environment_reads) == 1);
}

static void run_fresh(void (*test)(void)) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) { test(); _exit(0); }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void) {
    run_fresh(decisions);
    run_fresh(admission);
    run_fresh(views);
    run_fresh(lazy_conflict);
    run_fresh(no_environment);
    run_fresh(concurrency);
    puts("drop-in C author API: 6 fresh-process cases, 50 decisions, 48000 concurrent routes passed");
    return 0;
}
