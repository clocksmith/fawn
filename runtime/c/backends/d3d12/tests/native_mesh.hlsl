// SPDX-License-Identifier: MIT
struct VertexInput {
    float3 position : TEXCOORD0;
    float4 color : TEXCOORD1;
};
struct VertexOutput {
    float4 position : SV_Position;
    nointerpolation float4 color : COLOR0;
};
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}
float4 fragment_main(VertexOutput input) : SV_Target0 {
    return input.color;
}
