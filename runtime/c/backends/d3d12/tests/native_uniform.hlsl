// SPDX-License-Identifier: MIT
StructuredBuffer<uint> input_values : register(t0);
RWStructuredBuffer<uint> output_values : register(u0);
cbuffer Parameters : register(b0) {
    uint xor_mask;
    uint addend;
};
[numthreads(16, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x < 16) output_values[id.x] = (input_values[id.x] ^ xor_mask) + addend;
}
