#version 450

layout(set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 1) uniform sampler2D samplerNormal;
layout(set = 1, binding = 2) uniform sampler2D samplerMetallicRoughness;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} uboScene;

layout(push_constant) uniform PushConstants {
    mat4 model;
} primitive;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec4 inTangent;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMetallicRoughness;

vec3 get_view_space_normal() {
    vec3 n = normalize(inNormal);
    vec3 t = normalize(inTangent.xyz - n * dot(inTangent.xyz, n));
    vec3 b = cross(n, t) * inTangent.w;
    mat3 tbn = mat3(t, b, n);

    vec3 object_space_normal = tbn * (texture(samplerNormal, inUV).xyz * 2.0 - 1.0);
    return normalize((uboScene.view * primitive.model * vec4(object_space_normal, 0.0)).xyz);
}

void main() {
    outAlbedo = vec4(texture(samplerAlbedo, inUV).rgb, 1.0);
    outNormal = vec4(get_view_space_normal() * 0.5 + 0.5, 1.0);
    outMetallicRoughness = vec4(texture(samplerMetallicRoughness, inUV).xyz, 1.0);
}
