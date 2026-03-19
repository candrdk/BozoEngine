#pragma pack_matrix(column_major)

[[vk::binding(0, 0)]]
cbuffer UniformBufferObject {
    float4x4 view;
    float4x4 proj;
    float3   camPos;
};

struct VSOutput {
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 texCoords : TEXCOORD0;
};

VSOutput main([[vk::location(0)]] float3 inPosition : POSITION) {
    VSOutput output;
    output.texCoords = inPosition.xyz;
    output.Position  = mul(proj, mul(view, float4(inPosition, 0.0)));
    return output;
}
