#version 450

layout (set = 1, binding = 0) uniform sampler2DMS samplerAlbedo;
layout (set = 1, binding = 1) uniform sampler2DMS samplerNormal;
layout (set = 1, binding = 2) uniform sampler2DMS samplerOccMetRough;
layout (set = 1, binding = 3) uniform sampler2DMS samplerDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (constant_id = 0) const int RENDER_MODE = 0;
layout (constant_id = 1) const int NUM_SAMPLES = 1;

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

	switch(RENDER_MODE) {
	case 0:
	case 1:
		outFragcolor = resolve(samplerAlbedo, uv); break;
	case 2:
		outFragcolor = resolve(samplerNormal, uv); break;
	case 3:
		outFragcolor = resolve(samplerOccMetRough, uv); break;
	case 4:
		outFragcolor = resolve(samplerDepth, uv); break;
	}
}