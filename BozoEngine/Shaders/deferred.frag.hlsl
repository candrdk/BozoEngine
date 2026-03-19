#pragma pack_matrix(column_major)

struct DirLight {
    float3 direction;
    float3 ambient;
    float3 diffuse;
    float3 specular;
};

struct PointLight {
    float3 position;
    float3 ambient;
    float3 diffuse;
    float3 specular;
};

struct ShadowData {
    float4 a;
    float4 b;
    float4x4 shadowMat;
    float4 cascadeScales[3];
    float4 cascadeOffsets[3];
    float4 shadowOffsets[2];
};

#define MAX_POINT_LIGHTS 4

[[vk::binding(0, 0)]]
cbuffer UniformBufferObject {
    float4x4 view;
    float4x4 invProj;
    float4 position;
    ShadowData shadow;
    int pointLightCount;
    DirLight dirLight;
    PointLight pointLights[MAX_POINT_LIGHTS];
};

struct PushConstants { uint renderMode; uint colorCascades; uint enablePCF; };
[[vk::push_constant]] PushConstants pc;

[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] Texture2D<float4>   samplerAlbedo;
[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] SamplerState        samplerAlbedoState;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] Texture2D<float4>   samplerNormal;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] SamplerState        samplerNormalState;
[[vk::combinedImageSampler]] [[vk::binding(2, 1)]] Texture2D<float4>   samplerMetallicRoughness;
[[vk::combinedImageSampler]] [[vk::binding(2, 1)]] SamplerState        samplerMetallicRoughnessState;
[[vk::combinedImageSampler]] [[vk::binding(3, 1)]] Texture2D<float4>   samplerDepth;
[[vk::combinedImageSampler]] [[vk::binding(3, 1)]] SamplerState        samplerDepthState;
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2DArray<float> samplerShadowMap;
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerComparisonState samplerShadowMapState;

// View matrix is affine (rotation + translation), so use the efficient inverse
float4x4 InverseView(float4x4 v) {
    float3x3 R  = (float3x3)v;
    float3x3 Rt = transpose(R);
    float3   t  = float3(v[0][3], v[1][3], v[2][3]);
    float4x4 result;
    result[0] = float4(Rt[0], -dot(Rt[0], t));
    result[1] = float4(Rt[1], -dot(Rt[1], t));
    result[2] = float4(Rt[2], -dot(Rt[2], t));
    result[3] = float4(0, 0, 0, 1);
    return result;
}

float3 reconstruct_pos_view_space(float2 inUV) {
    float z = samplerDepth.Sample(samplerDepthState, inUV).r;
    float4 clipSpacePosition = float4(inUV * 2.0 - 1.0, z, 1.0);
    float4 viewSpacePosition = mul(invProj, clipSpacePosition);
    return viewSpacePosition.xyz / viewSpacePosition.w;
}

float3 shade_directional_light(DirLight light, float3 n, float3 v, float2 inUV) {
    const float alpha = 200.0;
    float3 l = -normalize(mul((float3x3)view, light.direction));
    float3 directColor = light.diffuse * clamp(dot(n, l), 0.0, 1.0);
    float3 diffuse = (light.ambient + directColor) * samplerAlbedo.Sample(samplerAlbedoState, inUV).rgb;
    float3 h = normalize(l + v);
    float highlight = pow(clamp(dot(n, h), 0.0, 1.0), alpha) * (float)(dot(n, l) > 0.0);
    float3 specular = light.diffuse * light.specular * highlight;
    return diffuse + specular;
}

float smooth_attenuation(float r, float2 attenuationConstants) {
    r = clamp(r, 0.0, 2.0 / attenuationConstants.y);
    float r2 = r * r;
    float attenuation = r2 * attenuationConstants.x * (sqrt(r2) * attenuationConstants.y - 3.0) + 1.0;
    return clamp(attenuation, 0.0, 1.0);
}

float3 shade_point_light(PointLight light, float3 n, float3 v, float3 p, float2 inUV) {
    const float alpha = 200.0;
    float3 lpos = mul(view, float4(light.position, 1.0)).xyz;
    float3 l = normalize(lpos - p);
    float3 directColor = light.diffuse * clamp(dot(n, l), 0.0, 1.0);
    float3 diffuse = (light.ambient + directColor) * samplerAlbedo.Sample(samplerAlbedoState, inUV).rgb;
    float3 h = normalize(l + v);
    float highlight = pow(clamp(dot(n, h), 0.0, 1.0), alpha) * (float)(dot(n, l) > 0.0);
    float3 specular = light.diffuse * light.specular * highlight;
    float d    = length(lpos - p);
    float rmax = 4.0;
    return (diffuse + specular) * smooth_attenuation(d, float2(1.0 / (rmax * rmax), 2.0 / rmax));
}

float SampleShadowMap(float4 p) {
    return samplerShadowMap.SampleCmpLevelZero(samplerShadowMapState, float3(p.x, p.y, p.z), p.w);
}

float3 get_shadow(float3 p) {
    float4 p1, p2;
    float4 world_pos = mul(InverseView(view), float4(p, 1.0));

    float3 cascadeCoord0 = mul(shadow.shadowMat, world_pos).xyz;
    float3 cascadeCoord1 = cascadeCoord0 * shadow.cascadeScales[0].xyz + shadow.cascadeOffsets[0].xyz;
    float3 cascadeCoord2 = cascadeCoord0 * shadow.cascadeScales[1].xyz + shadow.cascadeOffsets[1].xyz;
    float3 cascadeCoord3 = cascadeCoord0 * shadow.cascadeScales[2].xyz + shadow.cascadeOffsets[2].xyz;

    float u1 = (p.z - shadow.a[1]) / (shadow.b[0] - shadow.a[1]);
    float u2 = (p.z - shadow.a[2]) / (shadow.b[1] - shadow.a[2]);
    float u3 = (p.z - shadow.a[3]) / (shadow.b[2] - shadow.a[3]);

    bool beyondCascade2 = u2 >= 0.0;
    bool beyondCascade3 = u3 >= 0.0;
    p1.z = (float)beyondCascade2 * 2.0;
    p2.z = (float)beyondCascade3 * 2.0 + 1.0;

    float2 shadowCoord1 = beyondCascade2 ? cascadeCoord2.xy : cascadeCoord0.xy;
    float2 shadowCoord2 = beyondCascade3 ? cascadeCoord3.xy : cascadeCoord1.xy;

    p1.w = beyondCascade2 ? cascadeCoord2.z : cascadeCoord0.z;
    p2.w = beyondCascade3 ? clamp(cascadeCoord3.z, 0.0, 1.0) : cascadeCoord1.z;

    float3 blend  = clamp(float3(u1, u2, u3), 0.0, 1.0);
    float  weight = beyondCascade2 ? (blend.y - blend.z) : (1.0 - blend.x);

    p1.xy = shadowCoord1 + shadow.shadowOffsets[0].xy;
    float light1 = SampleShadowMap(p1);
    p1.xy = shadowCoord1 + shadow.shadowOffsets[0].zw;
    light1 += SampleShadowMap(p1);
    p1.xy = shadowCoord1 + shadow.shadowOffsets[1].xy;
    light1 += SampleShadowMap(p1);
    p1.xy = shadowCoord1 + shadow.shadowOffsets[1].zw;
    light1 += SampleShadowMap(p1);

    p2.xy = shadowCoord2 + shadow.shadowOffsets[0].xy;
    float light2 = SampleShadowMap(p2);
    p2.xy = shadowCoord2 + shadow.shadowOffsets[0].zw;
    light2 += SampleShadowMap(p2);
    p2.xy = shadowCoord2 + shadow.shadowOffsets[1].xy;
    light2 += SampleShadowMap(p2);
    p2.xy = shadowCoord2 + shadow.shadowOffsets[1].zw;
    light2 += SampleShadowMap(p2);

    float3 shadowColor = max(0.2, lerp(light2, light1, weight) * 0.25);

    if (pc.enablePCF == 0) {
        p1.xy = shadowCoord1;
        p2.xy = shadowCoord2;
        light1 = SampleShadowMap(p1);
        light2 = SampleShadowMap(p2);
        shadowColor = max(0.2, lerp(light2, light1, weight));
    }

    if (pc.colorCascades == 1) {
        switch (int(lerp(p2.z, p1.z, weight))) {
            case 0: shadowColor *= float3(1.0f,  0.25f, 0.25f); break;
            case 1: shadowColor *= float3(0.25f, 1.0f,  0.25f); break;
            case 2: shadowColor *= float3(0.25f, 0.25f, 1.0f ); break;
            case 3: shadowColor *= float3(1.0f,  0.25f, 0.25f); break;
        }
    }

    return shadowColor;
}

float4 shade_pixel(float2 inUV) {
    float3 p = reconstruct_pos_view_space(inUV);
    float3 n = normalize(samplerNormal.Sample(samplerNormalState, inUV).xyz * 2.0 - 1.0);
    float3 v = normalize(mul(view, position).xyz - p);

    float3 shade = shade_directional_light(dirLight, n, v, inUV);
    for (int i = 0; i < pointLightCount; i++) {
        shade += shade_point_light(pointLights[i], n, v, p, inUV);
    }

    return float4(shade * get_shadow(p), 1.0);
}

float4 main([[vk::location(0)]] float2 inUV : TEXCOORD0) : SV_Target0 {
    switch (pc.renderMode) {
        case 0: return shade_pixel(inUV);
        case 1: return samplerAlbedo.Sample(samplerAlbedoState, inUV);
        case 2: return samplerNormal.Sample(samplerNormalState, inUV);
        case 3: {
            int channel = inUV.x < 0.5 ? 2 : 1;
            float4 result = float4(0.0, 0.0, 0.0, 1.0);
            result[channel] = samplerMetallicRoughness.Sample(samplerMetallicRoughnessState, inUV)[channel];
            return result;
        }
        case 4: return float4(samplerDepth.Sample(samplerDepthState, inUV).r, 0.0, 0.0, 1.0);
        default: return float4(0, 0, 0, 1);
    }
}
