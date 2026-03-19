#pragma pack_matrix(column_major)

[[vk::binding(0, 0)]]
cbuffer UniformBufferObject {
    float4x4 view;
    float4x4 proj;
    float3   camPos;
};

struct PrimitivePC {
    float4x4 model;
    uint     parallaxMode;
    uint     parallaxSteps;
    float    parallaxScale;
};
[[vk::push_constant]] PrimitivePC primitive;

struct VSInput {
    [[vk::location(0)]] float3 inPos     : POSITION;
    [[vk::location(1)]] float3 inNormal  : NORMAL;
    [[vk::location(2)]] float4 inTangent : TANGENT;
    [[vk::location(3)]] float2 inUV      : TEXCOORD0;
    [[vk::location(4)]] float3 inColor   : COLOR;
};

struct VSOutput {
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 outNormal          : NORMAL;
    [[vk::location(1)]] float4 outTangent         : TANGENT;
    [[vk::location(2)]] float3 outColor           : COLOR;
    [[vk::location(3)]] float2 outUV              : TEXCOORD0;
    [[vk::location(4)]] float3 outTangentViewPos  : TEXCOORD1;
    [[vk::location(5)]] float3 outTangentFragPos  : TEXCOORD2;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.Position = mul(proj, mul(view, mul(primitive.model, float4(input.inPos, 1.0))));
    output.outNormal  = input.inNormal;
    output.outTangent = input.inTangent;
    output.outColor   = input.inColor;
    output.outUV      = input.inUV;

    float3x3 modelMat = (float3x3)primitive.model;
    float3 N = normalize(mul(modelMat, input.inNormal));
    float3 T = normalize(mul(modelMat, input.inTangent.xyz));
    float3 B = normalize(cross(N, T) * input.inTangent.w);
    float3x3 TBN = float3x3(T, B, N);

    output.outTangentViewPos = mul(TBN, camPos);
    output.outTangentFragPos = mul(TBN, mul(primitive.model, float4(input.inPos, 1.0)).xyz);
    return output;
}
