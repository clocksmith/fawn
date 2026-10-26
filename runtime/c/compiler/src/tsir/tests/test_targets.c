/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "test_common.h"
#include "tsir_targets.h"

static void descriptor_tests(void) {
    static const char *const expected[] = {
        "1bb3a7dc6a798e8cc9eae1e660800b9163469cbda67e14c75fe6a10ef342ade9",
        "4c9fff2b7ad274f8364cc2d57e68583011948992b865731d15b917c9c08cff2b",
        "401c8879b3e684854fe5052127aba26f626846fe005df5e144a3d88e4be14ec3"
    };
    int index;
    for (index = 0; index < 3; index++) {
        const TsirTargetIdentity *base = tsir_target_descriptor((TsirBootstrapTarget)index);
        TsirTargetIdentity changed = *base;
        uint8_t original[32];
        uint8_t mutated[32];
        char hex[65];
        CHECK_STATUS(tsir_target_descriptor_hash(base, original), TSIR_OK);
        hex_digest(original, hex);
        CHECK_STR(hex, expected[index]);
        changed.fabric_per_hop_latency_ns += 999;
        CHECK_STATUS(tsir_target_descriptor_hash(&changed, mutated), TSIR_OK);
        CHECK(memcmp(original, mutated, sizeof(original)) == 0);
        changed.planner.pe_working_memory_bytes += 1;
        CHECK_STATUS(tsir_target_descriptor_hash(&changed, mutated), TSIR_OK);
        CHECK(memcmp(original, mutated, sizeof(original)) != 0);
        changed = *base;
        changed.pe_persistent_pool_bytes += 1;
        CHECK_STATUS(tsir_target_descriptor_hash(&changed, mutated), TSIR_OK);
        CHECK(memcmp(original, mutated, sizeof(original)) != 0);
        changed = *base;
        changed.sub_tile_lane_width += 1;
        CHECK_STATUS(tsir_target_descriptor_hash(&changed, mutated), TSIR_OK);
        CHECK(memcmp(original, mutated, sizeof(original)) != 0);
        changed = *base;
        changed.planner.native_numerical_modes = NULL;
        memset(mutated, 0xa5, sizeof(mutated));
        CHECK_STATUS(tsir_target_descriptor_hash(&changed, mutated), TSIR_ERR_INVALID);
        CHECK(mutated[0] == 0xa5 && mutated[31] == 0xa5);
        CHECK_STATUS(tsir_target_descriptor_hash(NULL, mutated), TSIR_ERR_INVALID);
        CHECK_STATUS(tsir_target_descriptor_hash(base, NULL), TSIR_ERR_INVALID);
    }
    CHECK(tsir_target_descriptor((TsirBootstrapTarget)99) == NULL);
}

static int print_digests(const char *root) {
    int index;
    for (index = 0; index < 6; index++) {
        TsirBootstrapTarget target = (TsirBootstrapTarget)(index % 3);
        uint8_t digest[32];
        char hex[65];
        TsirStatus status = index < 3 ?
            tsir_target_descriptor_hash(tsir_target_descriptor(target), digest) :
            tsir_target_emitter_digest(target, root, digest);
        if (status != TSIR_OK) {
            return 1;
        }
        hex_digest(digest, hex);
        puts(hex);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--digests") == 0) {
        return print_digests(argv[2]);
    }
    if (argc == 4 && strcmp(argv[1], "--emitter") == 0) {
        uint8_t digest[32];
        char hex[65];
        TsirStatus status;
        TsirBootstrapTarget target = (TsirBootstrapTarget)atoi(argv[2]);
        memset(digest, 0xa5, sizeof(digest));
        status = tsir_target_emitter_digest(target, argv[3], digest);
        hex_digest(digest, hex);
        printf("%d %s\n", (int)status, hex);
        return 0;
    }
    descriptor_tests();
    return finish_tests("targets");
}
