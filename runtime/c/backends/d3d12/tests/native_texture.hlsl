// SPDX-License-Identifier: MIT
Texture2D<float> input_texture : register(t0);
RWTexture2D<float> output_texture : register(u0);
SamplerState clamp_sampler : register(s0);

[numthreads(3, 2, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float2 uv = (float2(id.xy) + 0.5) / float2(3.0, 2.0);
    output_texture[id.xy] = input_texture.SampleLevel(clamp_sampler, uv, 0) + 0.25;
}
