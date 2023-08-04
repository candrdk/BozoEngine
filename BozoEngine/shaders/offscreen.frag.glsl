#version 450

layout(set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout(set = 1, binding = 1) uniform sampler2D samplerNormal;
layout(set = 1, binding = 2) uniform sampler2D samplerOccMetRough;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outOccMetRough;

void main() {
    outAlbedo = vec4(texture(samplerAlbedo, inUV).rgb, 1.0);
    outNormal = vec4(normalize(inNormal) * 0.5 + 0.5, 1.0);
    outOccMetRough = vec4(texture(samplerOccMetRough, inUV).xyz, 1.0);
}
