#version 450

layout(binding = 0) uniform sampler2D fontSampler;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// Rec. 709 OETF and inverse OETF: https://en.wikipedia.org/wiki/Rec._709
float LinearToGamma(float l) {
	return (l < 0.018) ? (4.500 * l) : (1.099 * pow(l, 0.45) - 0.099);
}

float GammaToLinear(float v) { 
	return (v < 0.081) ? (v / 4.5) : (pow((v + 0.099) / 1.099, 1.0/0.45));
}

// TODO: Check if including these vec3 versions is justifiable 
//		 - how does the codegen compare to the single float version?
//		 - prob no difference, but good excercise to learn reading shader dissassembly
vec3 LinearToGamma(vec3 l) {
	mat2x3 v = mat2x3(4.500 * l, 1.099 * pow(l, vec3(0.45)) - 0.099);
	return vec3(
		v[l[0] < 0.018 ? 0 : 1][0],
		v[l[1] < 0.018 ? 0 : 1][1],
		v[l[2] < 0.018 ? 0 : 1][2] );
}

vec3 GammaToLinear(vec3 v) {
	mat2x3 l = mat2x3((v / 4.5), pow((v + 0.0999)/1.099, vec3(2.22222)) );
	return vec3(
		l[v[0] < 0.081 ? 0 : 1][0],
		l[v[1] < 0.081 ? 0 : 1][1],
		l[v[2] < 0.081 ? 0 : 1][2] );
}

void main() {
	outColor = inColor * texture(fontSampler, inUV);

	// Dear ImGUI color spaces are a mess, so we have to do some funky gamma correction here.
	// Note that this shader expects that the pipeline uses alpha-premultiplied blending.
	// See: https://github.com/ocornut/imgui/issues/578#issuecomment-1585432977
	outColor.rgb = GammaToLinear(outColor.rgb * outColor.a);
	outColor.a = 1.0 - GammaToLinear(1.0 - outColor.a);
}