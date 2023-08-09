#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec3 inColor;

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

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec4 outTangent;
layout(location = 2) out vec3 outColor;
layout(location = 3) out vec2 outUV;

layout(location = 4) out vec3 outTangentViewPos;
layout(location = 5) out vec3 outTangentFragPos;

void main() {
    gl_Position = uboScene.proj * uboScene.view * primitive.model * vec4(inPos, 1.0);

    outNormal = inNormal;
    outTangent = inTangent;
    outColor = inColor;
    outUV = inUV;

    vec3 N = normalize(mat3(primitive.model) * inNormal);
    vec3 T = normalize(mat3(primitive.model) * inTangent.xyz);
    vec3 B = normalize(cross(N, T) * inTangent.w);
    mat3 TBN = transpose(mat3(T, B, N));

    outTangentViewPos = TBN * uboScene.camPos;
    outTangentFragPos = TBN * vec3(primitive.model * vec4(inPos, 1.0));
}
