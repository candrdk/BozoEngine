#version 450

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;

void main() {
    outAlbedo = vec4(texture(texSampler, inUV).rgb, 1.0);
    outNormal = vec4(inNormal * 0.5 + 0.5, 1.0);
}
