#version 450

struct DirLight {
	vec3 direction;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
};

struct PointLight {
	vec3 position;
	vec3 ambient;
	vec3 diffuse;
	vec3 specular;
};

#define MAX_POINT_LIGHTS 4

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 view;
    mat4 invProj;
    vec4 position;

	int pointLightCount;
	DirLight dirLight;
	PointLight pointLights[MAX_POINT_LIGHTS];
} ubo;

layout(push_constant) uniform PushConstants {
    uint renderMode;
};

layout (set = 1, binding = 0) uniform sampler2DMS samplerAlbedo;
layout (set = 1, binding = 1) uniform sampler2DMS samplerNormal;
layout (set = 1, binding = 2) uniform sampler2DMS samplerMetallicRoughness;
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
	vec4 viewSpacePosition = ubo.invProj * clipSpacePosition;

	return viewSpacePosition.xyz / viewSpacePosition.w;
}

vec3 shade_directional_light(DirLight light, vec3 n, vec3 v) {
	const float alpha = 200.0;

	vec3 l = -normalize(mat3(ubo.view) * light.direction);

	vec3 directColor = light.diffuse * clamp(dot(n, l), 0.0, 1.0);
	vec3 diffuse = (light.ambient + directColor) * resolve(samplerAlbedo, ivec2(gl_FragCoord)).rgb;

	vec3 h = normalize(l + v);
	float highlight = pow(clamp(dot(n, h), 0.0, 1.0), alpha) * float(dot(n, l) > 0.0);
	vec3 specular = light.diffuse * light.specular * highlight;

	return diffuse + specular;
}

//TODO: fix up attentuation function + variables
vec3 shade_point_light(PointLight light, vec3 n, vec3 v, vec3 p) {
	const float alpha = 200.0;

	vec3 lpos = vec3(ubo.view * vec4(light.position, 1.0));
	vec3 l = normalize(lpos - p);

	vec3 directColor = light.diffuse * clamp(dot(n, l), 0.0, 1.0);
	vec3 diffuse = (light.ambient + directColor) * resolve(samplerAlbedo, ivec2(gl_FragCoord)).rgb;

	vec3 h = normalize(l + v);
	float highlight = pow(clamp(dot(n, h), 0.0, 1.0), alpha) * float(dot(n, l) > 0.0);
	vec3 specular = light.diffuse * light.specular * highlight;

	float d = length(lpos - p);
	float attenuation = 1.0 / (1.0 + 0.7 * d + 1.8 * d * d);  

	return (diffuse + specular) * attenuation;
}

vec4 shade_pixel(ivec2 uv) {
	vec3 p = reconstruct_pos_view_space(uv);
	vec3 n = normalize(resolve(samplerNormal, uv).xyz * 2.0 - 1.0);
	vec3 v = normalize((ubo.view * ubo.position).xyz - p);

	vec3 shade = shade_directional_light(ubo.dirLight, n, v);

	for (int i = 0; i < ubo.pointLightCount; i++) {
		shade += shade_point_light(ubo.pointLights[i], n, v, p);
	}

	return vec4(shade, 1.0);
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
		int channel = inUV.x < 0.5 ? 2 : 1;
		outFragcolor = vec4(0.0, 0.0, 0.0, 1.0);
		outFragcolor[channel] = resolve(samplerMetallicRoughness, uv)[channel];
		break;
	case 4:
		outFragcolor = vec4(resolve_depth(uv), 0.0, 0.0, 1.0); break;
	}
}