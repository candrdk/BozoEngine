#version 450

layout(set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 1) uniform sampler2D samplerNormal;
layout(set = 1, binding = 2) uniform sampler2D samplerMetallicRoughness;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 camPos;
    uint parallaxMode;
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

vec2 parallax(vec2 uv, vec3 vdir) {
    float height = 1.0 - texture(samplerNormal, uv).a;
    //height = uv.x > 0.5 ? 1.0 : 0.0;
    vec2 p = vdir.xy * (height * uboScene.parallaxScale) / vdir.z;
    return uv - p;
}

void main() {
    vec3 tangentViewDir = normalize(inTangentViewPos - inTangentFragPos);
    tangentViewDir.y *= -1.0;
    
    vec2 uv = inUV;
    if (uboScene.parallaxMode == 1) {
        uv = parallax(inUV, tangentViewDir);
    }

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		discard;
	}

    outAlbedo = vec4(texture(samplerAlbedo, uv).rgb, 1.0);
    outNormal = vec4(get_view_space_normal(uv) * 0.5 + 0.5, 1.0);
    outMetallicRoughness = vec4(texture(samplerMetallicRoughness, uv).xyz, 1.0);
}
