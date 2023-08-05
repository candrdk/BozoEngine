#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec3 inColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} uboScene;

layout(push_constant) uniform PushConstants {
    mat4 model;
} primitive;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec4 outTangent;
layout(location = 2) out vec3 outColor;
layout(location = 3) out vec2 outUV;

void main() {
    outNormal = inNormal;
    outTangent = inTangent;

    outColor = inColor;
    outUV = inUV;
    gl_Position = uboScene.proj * uboScene.view * primitive.model * vec4(inPos, 1.0);
}
