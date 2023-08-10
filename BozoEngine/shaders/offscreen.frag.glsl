#version 450

layout(set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 1) uniform sampler2D samplerNormal;
layout(set = 1, binding = 2) uniform sampler2D samplerMetallicRoughness;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 camPos;
    uint parallaxMode;
    uint parallaxSteps;
    float parallaxScale;
} uboScene;

layout(push_constant) uniform PushConstants {
    mat4 model;
} primitive;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inTangent;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 4) in vec3 inTangentViewPos;
layout(location = 5) in vec3 inTangentFragPos;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMetallicRoughness;

vec3 get_view_space_normal(vec2 uv) {
    vec3 n = normalize(inNormal);
    vec3 t = normalize(inTangent.xyz - n * dot(inTangent.xyz, n));
    vec3 b = cross(n, t) * inTangent.w;
    mat3 tbn = mat3(t, b, n);

    vec3 object_space_normal = tbn * (texture(samplerNormal, uv).xyz * 2.0 - 1.0);
    return normalize((uboScene.view * primitive.model * vec4(object_space_normal, 0.0)).xyz);
}

// TODO: The vdir.z division causes weird artifacts at shallow angles.
// Look into pros/cons of including it...
vec2 parallax(vec2 uv, vec3 vdir) {
    float height = 1.0 - texture(samplerNormal, uv).a;
    vec2 p = vdir.xy * height * uboScene.parallaxScale / vdir.z;
    return uv - p;
}

vec2 steep_parallax(vec2 uv, vec3 vdir) {
    // Small optimization?: Take less samples when looking straight at surface.
    const float minLayers = min(8, uboScene.parallaxSteps);
    const float maxLayers = uboScene.parallaxSteps;
    float numLayers = mix(maxLayers, minLayers, max(dot(vec3(0.0, 0.0, 1.0), vdir), 0.0));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    vec2 p = vdir.xy * uboScene.parallaxScale;
    vec2 deltaUV = p / numLayers;

    float currentDepth = 1.0 - texture(samplerNormal, uv).a;
    while (currentLayerDepth < currentDepth) {
        uv -= deltaUV;
        currentDepth = 1.0 - texture(samplerNormal, uv).a;
        currentLayerDepth += layerDepth;
    }

    return uv;
}

vec2 parallax_occlusion(vec2 uv, vec3 vdir) {
    // Small optimization?: Take less samples when looking straight at surface.
    const float minLayers = min(8, uboScene.parallaxSteps);
    const float maxLayers = uboScene.parallaxSteps;
    float numLayers = mix(maxLayers, minLayers, max(dot(vec3(0.0, 0.0, 1.0), vdir), 0.0));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    vec2 p = vdir.xy * uboScene.parallaxScale;
    vec2 deltaUV = p / numLayers;

    vec2 currentUV = uv;
    float currentDepth = 1.0 - texture(samplerNormal, currentUV).a;

    while (currentLayerDepth < currentDepth) {
        currentUV -= deltaUV;
        currentDepth = 1.0 - texture(samplerNormal, currentUV).a;
        currentLayerDepth += layerDepth;
    }

    vec2 prevUV = currentUV + deltaUV;

    float nextDepth = currentDepth - currentLayerDepth;
    float prevDepth = 1.0 - texture(samplerNormal, prevUV).a - currentLayerDepth + layerDepth;

    float weight = nextDepth / (nextDepth - prevDepth);
    return mix(currentUV, prevUV, weight);
}

void main() {
    vec3 tangentViewDir = normalize(inTangentViewPos - inTangentFragPos);
    tangentViewDir.y *= -1.0;

    vec2 uv = inUV;

    switch(uboScene.parallaxMode) {
        case 1: uv = parallax(inUV, tangentViewDir); break;
        case 2: uv = steep_parallax(inUV, tangentViewDir); break;
        case 3: uv = parallax_occlusion(inUV, tangentViewDir); break;
        default: break;
    }

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		discard;
	}

    outAlbedo = vec4(texture(samplerAlbedo, uv).rgb, 1.0);
    outNormal = vec4(get_view_space_normal(uv) * 0.5 + 0.5, 1.0);
    outMetallicRoughness = vec4(texture(samplerMetallicRoughness, uv).xyz, 1.0);
}
