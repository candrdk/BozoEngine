#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 camPos;
} ubo;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 texCoords;

void main() {
    texCoords = inPosition.xyz;
    gl_Position = ubo.proj * ubo.view * vec4(inPosition, 0.0);
}