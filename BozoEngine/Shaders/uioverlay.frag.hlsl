[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D<float4> fontSampler;
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState      fontSamplerState;

float LinearToGamma(float l) {
    return (l < 0.018) ? (4.500 * l) : (1.099 * pow(l, 0.45) - 0.099);
}

float GammaToLinear(float v) {
    return (v < 0.081) ? (v / 4.5) : pow((v + 0.099) / 1.099, 1.0 / 0.45);
}

float3 LinearToGamma(float3 l) {
    float3 a = 4.500 * l;
    float3 b = 1.099 * pow(abs(l), 0.45) - 0.099;
    return float3(
        l.x < 0.018 ? a.x : b.x,
        l.y < 0.018 ? a.y : b.y,
        l.z < 0.018 ? a.z : b.z);
}

float3 GammaToLinear(float3 v) {
    float3 a = v / 4.5;
    float3 b = pow(abs((v + 0.099) / 1.099), 1.0 / 0.45);
    return float3(
        v.x < 0.081 ? a.x : b.x,
        v.y < 0.081 ? a.y : b.y,
        v.z < 0.081 ? a.z : b.z);
}

struct PSInput {
    [[vk::location(0)]] float2 inUV    : TEXCOORD0;
    [[vk::location(1)]] float4 inColor : COLOR;
};

float4 main(PSInput input) : SV_Target0 {
    float4 outColor = input.inColor * fontSampler.Sample(fontSamplerState, input.inUV);
    outColor.rgb = GammaToLinear(outColor.rgb * outColor.a);
    outColor.a   = 1.0 - GammaToLinear(1.0 - outColor.a);
    return outColor;
}
