#pragma pack_matrix(column_major)

[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] Texture2D<float4> samplerAlbedo;
[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] SamplerState      samplerAlbedoState;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] Texture2D<float4> samplerNormal;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] SamplerState      samplerNormalState;
[[vk::combinedImageSampler]] [[vk::binding(2, 1)]] Texture2D<float4> samplerMetallicRoughness;
[[vk::combinedImageSampler]] [[vk::binding(2, 1)]] SamplerState      samplerMetallicRoughnessState;

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

struct PSInput {
    [[vk::location(0)]] float3 inNormal         : NORMAL;
    [[vk::location(1)]] float4 inTangent        : TANGENT;
    [[vk::location(2)]] float3 inColor          : COLOR;
    [[vk::location(3)]] float2 inUV             : TEXCOORD0;
    [[vk::location(4)]] float3 inTangentViewPos : TEXCOORD1;
    [[vk::location(5)]] float3 inTangentFragPos : TEXCOORD2;
};

struct PSOutput {
    [[vk::location(0)]] float4 outAlbedo           : SV_Target0;
    [[vk::location(1)]] float4 outNormal           : SV_Target1;
    [[vk::location(2)]] float4 outMetallicRoughness: SV_Target2;
};

float3 get_view_space_normal(PSInput input, float2 uv) {
    float3 n = normalize(input.inNormal);
    float3 t = normalize(input.inTangent.xyz - n * dot(input.inTangent.xyz, n));
    float3 b = cross(n, t) * input.inTangent.w;
    float3x3 tbn = float3x3(t, b, n);
    float3 objectSpaceNormal = mul(samplerNormal.Sample(samplerNormalState, uv).xyz * 2.0 - 1.0, tbn);
    return normalize(mul(view, mul(primitive.model, float4(objectSpaceNormal, 0.0))).xyz);
}

float2 parallax(float2 uv, float3 vdir) {
    float height = 1.0 - samplerNormal.Sample(samplerNormalState, uv).a;
    return uv - vdir.xy * height * primitive.parallaxScale;
}

float2 fged_parallax(float2 uv, float3 vdir) {
    const int k = int(primitive.parallaxSteps);
    uint w, h, mips;
    samplerNormal.GetDimensions(0, w, h, mips);
    float2 scale = (primitive.parallaxScale * 5000.0f) / (float2(w, h) * 2.0f * float(k));
    float2 pdir = vdir.xy * scale;
    for (int i = 0; i < k; i++) {
        float p = (samplerNormal.Sample(samplerNormalState, uv).z * 2.0 - 1.0)
                * (1.0 - samplerNormal.Sample(samplerNormalState, uv).a);
        uv -= pdir * p;
    }
    return uv;
}

float2 steep_parallax(float2 uv, float3 vdir) {
    const float minLayers = min(8.0f, (float)primitive.parallaxSteps);
    const float maxLayers = (float)primitive.parallaxSteps;
    float numLayers = lerp(maxLayers, minLayers, max(dot(float3(0.0, 0.0, 1.0), vdir), 0.0));
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    float2 deltaUV = vdir.xy * primitive.parallaxScale / numLayers;
    float currentDepth = 1.0 - samplerNormal.Sample(samplerNormalState, uv).a;
    while (currentLayerDepth < currentDepth) {
        uv -= deltaUV;
        currentDepth = 1.0 - samplerNormal.Sample(samplerNormalState, uv).a;
        currentLayerDepth += layerDepth;
    }
    return uv;
}

float2 parallax_occlusion(float2 uv, float3 vdir) {
    float numLayers = (float)primitive.parallaxSteps;
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;
    float2 deltaUV = vdir.xy * primitive.parallaxScale / numLayers;
    float2 currentUV = uv;
    float currentDepth = 1.0 - samplerNormal.Sample(samplerNormalState, currentUV).a;
    while (currentLayerDepth < currentDepth) {
        currentUV -= deltaUV;
        currentDepth = 1.0 - samplerNormal.Sample(samplerNormalState, currentUV).a;
        currentLayerDepth += layerDepth;
    }
    float2 prevUV = currentUV + deltaUV;
    float nextDepth = currentDepth - currentLayerDepth;
    float prevDepth = 1.0 - samplerNormal.Sample(samplerNormalState, prevUV).a - currentLayerDepth + layerDepth;
    return lerp(currentUV, prevUV, nextDepth / (nextDepth - prevDepth));
}

PSOutput main(PSInput input) {
    float3 tangentViewDir = normalize(input.inTangentViewPos - input.inTangentFragPos);
    tangentViewDir.y *= -1.0;

    float2 uv = input.inUV;
    if (primitive.parallaxMode > 0) {
        switch (primitive.parallaxMode) {
            case 1: uv = parallax(input.inUV, tangentViewDir);           break;
            case 2: uv = fged_parallax(input.inUV, tangentViewDir);      break;
            case 3: uv = steep_parallax(input.inUV, tangentViewDir);     break;
            case 4: uv = parallax_occlusion(input.inUV, tangentViewDir); break;
            default: break;
        }
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) discard;
    }

    PSOutput output;
    output.outAlbedo            = float4(samplerAlbedo.Sample(samplerAlbedoState, uv).rgb, 1.0);
    output.outNormal            = float4(get_view_space_normal(input, uv) * 0.5 + 0.5, 1.0);
    output.outMetallicRoughness = float4(samplerMetallicRoughness.Sample(samplerMetallicRoughnessState, uv).xyz, 1.0);
    return output;
}
