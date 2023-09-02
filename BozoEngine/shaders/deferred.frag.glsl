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

struct ShadowData {
	vec4 a;
	vec4 b;
	mat4 shadowMat;
	vec3 cascadeScales[3];
	vec3 cascadeOffsets[3];
};

#define MAX_POINT_LIGHTS 4

layout(set = 0, binding = 0) uniform UniformBufferObject {
	mat4 view;
    mat4 invProj;
    vec4 position;

	ShadowData shadow;

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

layout (set = 2, binding = 0) uniform sampler2DArray samplerShadowMap;

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

	vec3 p1, p2;
	vec4 world_pos = inverse(ubo.view) * vec4(p, 1.0);

	// Apply scales and offsets to get the texcoords in all four cascade
	vec3 cascadeCoord0 = (ubo.shadow.shadowMat * world_pos).xyz;
	vec3 cascadeCoord1 = cascadeCoord0 * ubo.shadow.cascadeScales[0] + ubo.shadow.cascadeOffsets[0];
	vec3 cascadeCoord2 = cascadeCoord0 * ubo.shadow.cascadeScales[1] + ubo.shadow.cascadeOffsets[1];
	vec3 cascadeCoord3 = cascadeCoord0 * ubo.shadow.cascadeScales[2] + ubo.shadow.cascadeOffsets[2];

	float u1 = (p.z - ubo.shadow.a[1]) / (ubo.shadow.b[0] - ubo.shadow.a[1]);
	float u2 = (p.z - ubo.shadow.a[2]) / (ubo.shadow.b[1] - ubo.shadow.a[2]);
	float u3 = (p.z - ubo.shadow.a[3]) / (ubo.shadow.b[2] - ubo.shadow.a[3]);

	// Calculate the layer/cascade indices
	bool beyondCascade2 = u2 >= 0.0;
	bool beyondCascade3 = u3 >= 0.0;
	p1.z = float(beyondCascade2) * 2.0;
	p2.z = float(beyondCascade3) * 2.0 + 1.0;

	// Select the texture coordinates
	vec2 shadowCoord1 = beyondCascade2 ? cascadeCoord2.xy : cascadeCoord0.xy;
	vec2 shadowCoord2 = beyondCascade3 ? cascadeCoord3.xy : cascadeCoord1.xy;

	// Select the depth to compare against
	float depth1 = beyondCascade2 ? cascadeCoord2.z : cascadeCoord0.z;
	float depth2 = beyondCascade3 ? clamp(cascadeCoord3.z, 0.0, 1.0) : cascadeCoord1.z;

	depth1 += 0.0005;
	depth2 += 0.0005;

	vec3 blend = clamp(vec3(u1, u2, u3), 0.0, 1.0);
	float weight = beyondCascade2 ? (blend.y - blend.z) : (1.0 - blend.x);

	float delta = 0.0005;

	p1.xy = shadowCoord1 + vec2(delta, delta);
	float l0 = texture(samplerShadowMap, p1).x;
	float light1 = l0 > depth1 ? 0.0 : 1.0;

	p1.xy = shadowCoord1 + vec2(delta, -delta);
	float l11 = texture(samplerShadowMap, p1).x;
	light1 += l11 > depth1 ? 0.0 : 1.0;
	
	p1.xy = shadowCoord1 + vec2(-delta, delta);
	float l12 = texture(samplerShadowMap, p1).x;
	light1 += l12 > depth1 ? 0.0 : 1.0;
	
	p1.xy = shadowCoord1 + vec2(-delta, -delta);
	float l13 = texture(samplerShadowMap, p1).x;
	light1 += l13 > depth1 ? 0.0 : 1.0;


	p2.xy = shadowCoord2 + vec2(delta, delta);
	float l20 = texture(samplerShadowMap, p2).x;
	float light2 = l20 > depth2 ? 0.0 : 1.0;
	
	p2.xy = shadowCoord2 + vec2(delta, -delta);
	float l21 = texture(samplerShadowMap, p2).x;
	light2 += l21 > depth2 ? 0.0 : 1.0;
	
	p2.xy = shadowCoord2 + vec2(-delta, delta);
	float l22 = texture(samplerShadowMap, p2).x;
	light2 += l22 > depth2 ? 0.0 : 1.0;
	
	p2.xy = shadowCoord2 + vec2(-delta, -delta);
	float l23 = texture(samplerShadowMap, p2).x;
	light2 += l23 > depth2 ? 0.0 : 1.0;

	float shadow = mix(light2, light1, weight) * 0.25;

	shadow = 1.0 - (1.0 - shadow) * 0.95;

	vec3 cascades[3] = {
		vec3(1.0, 0.0, 0.0),
		vec3(0.0, 1.0, 0.0),
		vec3(0.0, 0.0, 1.0)
	};

	vec3 cascadeDebug = (weight > 0.0 && weight < 0.05) ? cascades[int(min(p1.z, p2.z))] : vec3(0.0);
	cascadeDebug += (weight > 0.95 && weight < 1.0) ? cascades[int(min(p1.z, p2.z))] : vec3(0.0);

	return vec4(shade * shadow + cascadeDebug, 1.0);
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