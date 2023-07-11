#version 450

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outFragColor;

void main() {
    outFragColor = vec4(texture(texSampler, inUV).rgb, 1.0);
}
