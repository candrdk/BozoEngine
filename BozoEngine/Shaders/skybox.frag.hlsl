[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] TextureCube<float4> samplerSkybox;
[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] SamplerState        samplerSkyboxState;

float4 main([[vk::location(0)]] float3 texCoords : TEXCOORD0) : SV_Target0 {
    return samplerSkybox.Sample(samplerSkyboxState, texCoords);
}
