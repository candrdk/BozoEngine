#pragma pack_matrix(column_major)

[[vk::binding(0, 0)]]
cbuffer BlurUBO
{
    float4x4 _proj;
    float4x4 _invProj;
    float4 _params; // DirectionSampleCount SliceCount WorldRadius Power
    float2 texelSize; // float2(1/width, 1/height)
};

[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] Texture2D<float4> texAO;
[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] SamplerState      texAOState;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] Texture2D<float4> texDepth;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] SamplerState      texDepthState;

float4 main([[vk::location(0)]] float2 inUV : TEXCOORD0) : SV_Target0 {
    float centerAO    = texAO.Sample(texAOState, inUV).r;
    float centerDepth = texDepth.Sample(texDepthState, inUV).r;

    float totalWeight = 1.0;
    float totalAO     = centerAO;

    // 5x5 edge-preserving bilateral blur
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            if (x == 0 && y == 0) continue;

            float2 sampleUV = inUV + float2(x, y) * texelSize.xy;

            float sampleAO    = texAO.Sample(texAOState, sampleUV).r;
            float sampleDepth = texDepth.Sample(texDepthState, sampleUV).r;

            // Bilateral weight: suppress blurring across depth discontinuities
            float depthDiff = abs(centerDepth - sampleDepth);
            float bilateralW = exp(-depthDiff * depthDiff * 10000.0);

            // Spatial Gaussian weight
            float spatialW = exp(-0.5 * (x * x + y * y));

            float weight = bilateralW * spatialW;
            totalAO     += sampleAO * weight;
            totalWeight += weight;
        }
    }

    float ao = totalAO / totalWeight;
    return float4(ao, ao, ao, 1.0);
}
