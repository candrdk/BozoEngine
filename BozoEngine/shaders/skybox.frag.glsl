#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 camPos;
} ubo;

layout(set = 1, binding = 0) uniform samplerCube samplerSkybox;

layout(location = 0) in vec3 texCoords;

layout(location = 0) out vec4 outAlbedo;

void main() {
    outAlbedo = texture(samplerSkybox, texCoords);
}