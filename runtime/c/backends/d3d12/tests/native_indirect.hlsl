// SPDX-License-Identifier: MIT
RWStructuredBuffer<uint> arguments : register(u0);
[numthreads(1, 1, 1)]
void main() {
    arguments[4] = 1u;
    arguments[5] = 1u;
    arguments[6] = 1u;
}
