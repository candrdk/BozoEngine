#version 450

layout(location = 0) in vec3 inPos;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 viewProj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} primitive;

void main() {
    gl_Position = ubo.viewProj * primitive.model * vec4(inPos, 1.0);
}