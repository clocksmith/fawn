// SPDX-License-Identifier: MIT
Texture2D<float4> input_texture : register(t0);
SamplerState point_sampler : register(s0);

float4 vertex_main(uint vertex : SV_VertexID) : SV_Position {
    float2 positions[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(positions[vertex], 0, 1);
}

float4 fragment_main(float4 position : SV_Position) : SV_Target0 {
    return input_texture.SampleLevel(point_sampler, position.xy / float2(64, 2), 0);
}
