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

layout (set = 1, binding = 0) uniform sampler2D samplerAlbedo;
layout (set = 1, binding = 1) uniform sampler2D samplerNormal;
layout (set = 1, binding = 2) uniform sampler2D samplerMetallicRoughness;
layout (set = 1, binding = 3) uniform sampler2D samplerDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

vec3 reconstruct_pos_view_space() {
	float z = texture(samplerDepth, inUV).r;

	vec4 clipSpacePosition = vec4(inUV * 2.0 - 1.0, z, 1.0);
	vec4 viewSpacePosition = ubo.invProj * clipSpacePosition;

	return viewSpacePosition.xyz / viewSpacePosition.w;
}

vec3 shade_directional_light(DirLight light, vec3 n, vec3 v) {
	const float alpha = 200.0;

	vec3 l = -normalize(mat3(ubo.view) * light.direction);

	vec3 directColor = light.diffuse * clamp(dot(n, l), 0.0, 1.0);
	vec3 diffuse = (light.ambient + directColor) * texture(samplerAlbedo, inUV).rgb;

	vec3 h = normalize(l + v);
	float highlight = pow(clamp(dot(n, h), 0.0, 1.0), alpha) * float(dot(n, l) > 0.0);
	vec3 specular = light.diffuse * light.specular * highlight;

	return diffuse + specular;
}

// These three are from fged
// x = r0^2 / (rmax^2 - r0^2), y = rmax^2
float inverse_square_attenuation(float r, vec2 attenuationConstants) {
	float r2 = r * r;
	return clamp(attenuationConstants.x * (attenuationConstants.y / r2 - 1.0), 0.0, 1.0);
}

// x = -k^2/rmax^2, y = 1 / (1 - e^(-k^2)), z = e^(-k^2) / (1-e^(-k^2)
float exponential_attenuation(float r, vec3 attenuationConstants) {
	float r2 = r * r;
	return clamp(exp(r2 * attenuationConstants.x) * attenuationConstants.y - attenuationConstants.z, 0.0, 1.0);
}

// x = 1 / rmax^2, y = 2 / rmax
float smooth_attenuation(float r, vec2 attenuationConstants) {
	r = clamp(r, 0.0, 2.0 / attenuationConstants.y);

	float r2 = r * r;
	float attenuation = r2 * attenuationConstants.x * (sqrt(r2) * attenuationConstants.y - 3.0) + 1.0;
	return clamp(attenuation, 0.0, 1.0);
}

// From https://www.youtube.com/watch?v=wzIcjzKQ2BE
// r = radius of point light (r0)
// d = distance
float yuksel_attenuation(float d, float r) {
	float r2 = r * r;
	return 2.0 / r2 * (1.0 - d / sqrt(d*d + r2));
}

vec3 shade_point_light(PointLight light, vec3 n, vec3 v, vec3 p) {
	const float alpha = 200.0;

	vec3 lpos = vec3(ubo.view * vec4(light.position, 1.0));
	vec3 l = normalize(lpos - p);

	vec3 directColor = light.diffuse * clamp(dot(n, l), 0.0, 1.0);
	vec3 diffuse = (light.ambient + directColor) * texture(samplerAlbedo, inUV).rgb;

	vec3 h = normalize(l + v);
	float highlight = pow(clamp(dot(n, h), 0.0, 1.0), alpha) * float(dot(n, l) > 0.0);
	vec3 specular = light.diffuse * light.specular * highlight;

	float d = length(lpos - p);
	float r0 = 0.05;	float r02 = r0 * r0;
	float rmax = 4.0;	float rmax2 = rmax * rmax;
	float k = 2.0;		float k2 = k * k;

	//return (diffuse + specular) * 500.0 * inverse_square_attenuation(d, vec2(r02 / (rmax2 - r02), rmax2));
	//return (diffuse + specular) * exponential_attenuation(d, vec3(-k2 / rmax2, 1.0 / (1.0 - exp(-k2)), exp(-k2) / (1.0 - exp(-k2))));
	return (diffuse + specular) * smooth_attenuation(d, vec2(1.0 / rmax2, 2.0 / rmax));
	//return (diffuse + specular) * yuksel_attenuation(d, r0);
}

vec4 shade_pixel() {
	vec3 p = reconstruct_pos_view_space();
	vec3 n = normalize(texture(samplerNormal, inUV).xyz * 2.0 - 1.0);
	vec3 v = normalize((ubo.view * ubo.position).xyz - p);

	vec3 shade = shade_directional_light(ubo.dirLight, n, v);

	for (int i = 0; i < ubo.pointLightCount; i++) {
		shade += shade_point_light(ubo.pointLights[i], n, v, p);
	}

	return vec4(shade, 1.0);
}

void main() {
	switch(renderMode) {
	case 0:
		outFragcolor = shade_pixel(); break;
	case 1:
		outFragcolor = texture(samplerAlbedo, inUV); break;
	case 2:
		outFragcolor = texture(samplerNormal, inUV); break;
	case 3:
		int channel = inUV.x < 0.5 ? 2 : 1;
		outFragcolor = vec4(0.0, 0.0, 0.0, 1.0);
		outFragcolor[channel] = texture(samplerMetallicRoughness, inUV)[channel];
		break;
	case 4:
		outFragcolor = vec4(texture(samplerDepth, inUV).r, 0.0, 0.0, 1.0); break;
	}
}