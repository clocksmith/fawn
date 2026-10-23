/* SPDX-License-Identifier: MIT */
#include "doe_copy_footprint.h"
#include <assert.h>
#include <stdio.h>

static void exhaustive_small(void)
{
    unsigned cases = 0;
    for (uint64_t bytes = 0; bytes <= 8; bytes++)
    for (uint64_t rows = 0; rows <= 8; rows++)
    for (uint64_t image_rows = 0; image_rows <= 8; image_rows++)
    for (uint64_t pitch = 0; pitch <= 8; pitch++)
    for (uint64_t layers = 0; layers <= 8; layers++) {
        /* Bounded, independent repeated-addition oracle, including inputs whose
         * pitch/geometry would be inadmissible in either actual caller. */
        uint64_t expected = 0;
        for (uint64_t layer = 1; layer < layers; layer++)
            for (uint64_t row = 0; row < image_rows; row++) expected += pitch;
        if (layers && rows) {
            for (uint64_t row = 1; row < rows; row++) expected += pitch;
            expected += bytes;
        }
        uint64_t actual = UINT64_MAX;
        assert(doe_copy_footprint(bytes, rows, image_rows, pitch, layers, &actual));
        assert(actual == expected);
        cases++;
    }
    printf("copy footprint: %u bounded arithmetic equivalence cases\n", cases);
}

static void boundary_cases(void)
{
    uint64_t out = 99;
    assert(!doe_copy_footprint(1, 1, 1, 1, 1, NULL));
    assert(!doe_copy_footprint(1, 1, 2, UINT64_MAX, 1, &out)); /* image stride */
    assert(out == 99);
    assert(!doe_copy_footprint(1, 1, 1, UINT64_MAX, 3, &out)); /* prior images */
    assert(out == 99);
    assert(!doe_copy_footprint(1, 3, 0, UINT64_MAX, 1, &out)); /* prior rows */
    assert(out == 99);
    assert(!doe_copy_footprint(0, 2, 1, UINT64_MAX, 2, &out)); /* first sum */
    assert(out == 99);
    assert(!doe_copy_footprint(1, 1, 1, UINT64_MAX, 2, &out)); /* final sum */
    assert(out == 99);
    assert(doe_copy_footprint(0, 2, 0, UINT64_MAX, 1, &out) && out == UINT64_MAX);
    assert(doe_copy_footprint(UINT64_MAX, 1, 0, 0, 1, &out) && out == UINT64_MAX);
    assert(doe_copy_footprint(17, 0, 3, 5, 4, &out) && out == 45);
    assert(doe_copy_footprint(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, 0, &out) && out == 0);
    assert(doe_copy_footprint(4, 3, 5, 256, 2, &out) && out == 1796);
    puts("copy footprint: overflow publication and boundary cases passed");
}

int main(void)
{
    exhaustive_small();
    boundary_cases();
    return 0;
}
