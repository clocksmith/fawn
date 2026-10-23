// SPDX-License-Identifier: MIT
StructuredBuffer<uint> input_values : register(t0);
RWStructuredBuffer<uint> output_values : register(u0);
[numthreads(16, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x < 16) output_values[id.x] = (input_values[id.x] ^ 0x13579bdfu) + 7u;
}
