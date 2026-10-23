/* SPDX-License-Identifier: MIT
 * Attribute-erased emitted-operation control, not Metal compiler validation. */
#include <cassert>
#include <cstdio>
#define vertex
#define fragment
#include "graphics.metal"
int main() {
    DoeStage0_1 input{};
    input.field0 = 17;
    input.field1 = 9;
    auto output = doe_entry0(input, true, 5);
    assert(output.field4 == 1709 && output.field5 == 5);
    output = doe_entry0(input, false, 5);
    assert(output.field4 == 1710 && output.field5 == 0);
    input.field0 = 0xffffffffu;
    input.field1 = 31;
    output = doe_entry0(input, true, 5);
    assert(output.field4 == (0xffffffffu * 100u + 31u) && output.field5 == 5);
    std::puts("Metal graphics operation control: formal/leaf identity, helper call, branch and return mapping passed.");
}
