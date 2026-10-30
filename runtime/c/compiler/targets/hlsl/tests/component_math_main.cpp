/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Doe contributors */
#include "../../tests/component_math_source.h"
int main() {
    uint words[33] = {0xbe800000u};
    words[32] = 0x12345678u;
    BIND(r0, 0, words, 32);
    component_math();
    for (unsigned i = 0; i < sizeof(component_math_expected) / sizeof(*component_math_expected); ++i)
        assert(words[i] == component_math_expected[i]);
    assert(words[29] == 2u && words[30] == 2u && words[31] == 2u);
    assert(words[32] == 0x12345678u && r0.beyond == 0);
    std::puts("HLSL emitted operations: exact rounding, fractional tail, minimum abs and once-only effects passed.");
}
