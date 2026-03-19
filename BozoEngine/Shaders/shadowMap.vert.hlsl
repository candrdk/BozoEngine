#pragma pack_matrix(column_major)

[[vk::binding(0, 0)]]
cbuffer UniformBufferObject {
    float4x4 viewProj;
};

struct PushConstants { float4x4 model; };
[[vk::push_constant]] PushConstants primitive;

float4 main([[vk::location(0)]] float3 inPos : POSITION) : SV_Position {
    return mul(viewProj, mul(primitive.model, float4(inPos, 1.0)));
}
