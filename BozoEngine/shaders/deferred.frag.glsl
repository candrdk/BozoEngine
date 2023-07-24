#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} uboScene;

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

// Resolve to the lowest possible depth; the normal resolve messes up edges. 
// TODO: Look into if this is the corret way to do this
float resolve_depth(ivec2 uv) {
	float minDepth = 0.0;

	for (int i = 0; i < NUM_SAMPLES; i++) {
		float depth = texelFetch(samplerDepth, uv, i).r;
		minDepth = max(depth, minDepth);
	}

	return minDepth;
}

vec3 reconstruct_viewpos(ivec2 uv) {
	float z = resolve_depth(uv);

	vec4 clipSpacePosition = vec4(inUV * 2.0 - 1.0, z, 1.0);
	vec4 viewSpacePosition = inverse(uboScene.proj) * clipSpacePosition;	// TODO: inverse proj should be given by ubo

	return viewSpacePosition.xyz / viewSpacePosition.w;
}

vec4 shade_pixel(ivec2 uv) {
	vec3 diffuseColor = resolve(samplerAlbedo, uv).rgb;
	vec3 ambientColor = vec3(0.25, 0.25, 0.25);
	vec3 lightColor = vec3(1.0, 1.0, 1.0);

	vec3 pixelpos = reconstruct_viewpos(uv);
	vec3 lightpos = (uboScene.view * vec4(0.5, 0.0, 0.5, 1.0)).xyz;

	vec3 n = normalize((uboScene.view * vec4(resolve(samplerNormal, uv).xyz, 0.0)).xyz);
	vec3 l = normalize(lightpos - pixelpos);

	vec3 directColor = lightColor * clamp(dot(n, l), 0.0, 1.0);
	return vec4((ambientColor + directColor) * diffuseColor, 1.0);
}

void main() {
	ivec2 uv = ivec2(gl_FragCoord.xy);

	switch(RENDER_MODE) {
	case 0:
		outFragcolor = shade_pixel(uv); break;
	case 1:
		outFragcolor = resolve(samplerAlbedo, uv); break;
	case 2:
		outFragcolor = resolve(samplerNormal, uv); break;
	case 3:
		outFragcolor = resolve(samplerOccMetRough, uv); break;
	case 4:
		outFragcolor = vec4(resolve_depth(uv), 0.0, 0.0, 1.0); break;
	}
}