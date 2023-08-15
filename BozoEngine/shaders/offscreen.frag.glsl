#version 450

layout(set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 1) uniform sampler2D samplerNormal;
layout(set = 1, binding = 2) uniform sampler2D samplerMetallicRoughness;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 camPos;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint parallaxMode;
    uint parallaxSteps;
    float parallaxScale;
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
    return normalize((ubo.view * primitive.model * vec4(object_space_normal, 0.0)).xyz);
}

vec2 parallax(vec2 uv, vec3 vdir) {
    float height = 1.0 - texture(samplerNormal, uv).a;
    vec2 parallax = vdir.xy * height * primitive.parallaxScale;
    return uv - parallax;
}

// This is probably wrong idk
vec2 fged_parallax(vec2 uv, vec3 vdir) {
    const int k = int(primitive.parallaxSteps);

    vec2 scale = vec2(primitive.parallaxScale * 5000) / (textureSize(samplerNormal, 0) * 2.0 * k);
    vec2 pdir = vdir.xy * scale;

    for (int i = 0; i < k; i++) {
        float parallax = (texture(samplerNormal, uv).z * 2.0 - 1.0) * (1.0 - texture(samplerNormal, uv).a);
        uv -= pdir * parallax;
    }

    return uv;
}

vec2 steep_parallax(vec2 uv, vec3 vdir) {
    // Small optimization?: Take less samples when looking straight at surface. Prob only worth it when using lots of samples
    const float minLayers = min(8, primitive.parallaxSteps);
    const float maxLayers = primitive.parallaxSteps;
    float numLayers = mix(maxLayers, minLayers, max(dot(vec3(0.0, 0.0, 1.0), vdir), 0.0));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    vec2 p = vdir.xy * primitive.parallaxScale;
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
    float numLayers = primitive.parallaxSteps;
    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    vec2 p = vdir.xy * primitive.parallaxScale;
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
    if (primitive.parallaxMode > 0) {
        switch(primitive.parallaxMode) {
            case 1: uv = parallax(inUV, tangentViewDir); break;
            case 2: uv = fged_parallax(inUV, tangentViewDir); break;
            case 3: uv = steep_parallax(inUV, tangentViewDir); break;
            case 4: uv = parallax_occlusion(inUV, tangentViewDir); break;
            default: break;
        }

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		    discard;
	    }
    }

    outAlbedo = vec4(texture(samplerAlbedo, uv).rgb, 1.0);
    outNormal = vec4(get_view_space_normal(uv) * 0.5 + 0.5, 1.0);
    outMetallicRoughness = vec4(texture(samplerMetallicRoughness, uv).xyz, 1.0);
}
