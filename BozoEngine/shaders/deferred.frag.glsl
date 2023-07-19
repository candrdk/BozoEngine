#version 450

layout (set = 1, binding = 0) uniform sampler2DMS samplerAlbedo;
layout (set = 1, binding = 1) uniform sampler2DMS samplerNormal;
layout (set = 1, binding = 2) uniform sampler2DMS samplerOccMetRough;
layout (set = 1, binding = 3) uniform sampler2DMS samplerDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

#define NUM_SAMPLES 8

// Manual resolve for MSAA samples 
vec4 resolve(sampler2DMS tex, ivec2 uv) {
	vec4 result = vec4(0.0);	   
	for (int i = 0; i < NUM_SAMPLES; i++) {
		vec4 val = texelFetch(tex, uv, i); 
		result += val;
	}    
	// Average resolved samples
	return result / float(NUM_SAMPLES);
}

void main() {
	ivec2 uv = ivec2(inUV * textureSize(samplerAlbedo));

	if (inUV.y < 0.25) {
		outFragcolor = resolve(samplerAlbedo, uv);
	}
	else if (inUV.y < 0.5) {
		outFragcolor = resolve(samplerNormal, uv);
	}
	else if (inUV.y < 0.75) {
		outFragcolor = resolve(samplerOccMetRough, uv);
	} 
	else {
		outFragcolor = resolve(samplerDepth, uv);
	}
}