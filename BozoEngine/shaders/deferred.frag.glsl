#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 view;
    mat4 invProj;
    vec4 position;
	vec4 lightPos1;
	vec4 lightPos2;
} camera;

layout(push_constant) uniform PushConstants {
    uint renderMode;
};

layout (set = 1, binding = 0) uniform sampler2DMS samplerAlbedo;
layout (set = 1, binding = 1) uniform sampler2DMS samplerNormal;
layout (set = 1, binding = 2) uniform sampler2DMS samplerOccMetRough;
layout (set = 1, binding = 3) uniform sampler2DMS samplerDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

layout (constant_id = 0) const int MSAA_SAMPLES = 1;

// Manual resolve for MSAA samples 
vec4 resolve(sampler2DMS tex, ivec2 uv) {
	vec4 result = vec4(0.0);	   
	for (int i = 0; i < MSAA_SAMPLES; i++) {
		vec4 val = texelFetch(tex, uv, i); 
		result += val;
	}    
	// Average resolved samples
	return result / float(MSAA_SAMPLES);
}

// Resolve to the lowest possible depth; the normal resolve messes up edges. 
// TODO: Look into if this is the corret way to do this
float resolve_depth(ivec2 uv) {
	float minDepth = 0.0;

	for (int i = 0; i < MSAA_SAMPLES; i++) {
		float depth = texelFetch(samplerDepth, uv, i).r;
		minDepth = max(depth, minDepth);
	}

	return minDepth;
}

vec3 reconstruct_pos_view_space(ivec2 uv) {
	float z = resolve_depth(uv);

	vec4 clipSpacePosition = vec4(inUV * 2.0 - 1.0, z, 1.0);
	vec4 viewSpacePosition = camera.invProj * clipSpacePosition;

	return viewSpacePosition.xyz / viewSpacePosition.w;
}

vec3 blinn_phong(vec3 n, vec3 l, vec3 v, vec3 lightColor) {
	vec3 ambientColor = vec3(0.25, 0.25, 0.25);
	//vec3 lightColor = vec3(1.0, 1.0, 1.0);
	vec3 specularColor = vec3(0.25, 0.25, 0.25);
	float alpha = 200.0;
	
	vec3 directColor = lightColor * clamp(dot(n, l), 0.0, 1.0);
	vec3 diffuse = (ambientColor + directColor) * resolve(samplerAlbedo, ivec2(gl_FragCoord)).rgb;

	vec3 h = normalize(l + v);
	float highlight = pow(clamp(dot(n, h), 0.0, 1.0), alpha) * float(dot(n, l) > 0.0);
	vec3 specular = lightColor * specularColor * highlight;

	return specular + diffuse;
}

vec4 shade_pixel(ivec2 uv) {
	vec3 pos = reconstruct_pos_view_space(uv);
	vec3 lightpos1 = (camera.view * camera.lightPos1).xyz;
	vec3 lightpos2 = (camera.view * camera.lightPos2).xyz;

	vec3 n = normalize(resolve(samplerNormal, uv).xyz * 2.0 - 1.0);
	vec3 v = normalize((camera.view * camera.position).xyz - pos);

	vec3 l1 = normalize(lightpos1 - pos);
	vec3 l2 = normalize(lightpos2 - pos);

	return vec4(blinn_phong(n, l1, v, vec3(1.0, 0.0, 0.0)) + blinn_phong(n, l2, v, vec3(0.0, 0.0, 1.0)), 1.0);
}

void main() {
	ivec2 uv = ivec2(gl_FragCoord.xy);

	switch(renderMode) {
	case 0:
		outFragcolor = shade_pixel(uv); break;
	case 1:
		outFragcolor = resolve(samplerAlbedo, uv); break;
	case 2:
		outFragcolor = resolve(samplerNormal, uv); break;
	case 3:
		int channel = int(inUV.x < 0.5) + 2 * int(inUV.y < 0.5);
		outFragcolor = vec4(0.0, 0.0, 0.0, 1.0);
		outFragcolor[channel] = resolve(samplerOccMetRough, uv)[channel];
		break;
	case 4:
		outFragcolor = vec4(resolve_depth(uv), 0.0, 0.0, 1.0); break;
	}
}