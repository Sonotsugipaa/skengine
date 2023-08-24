#version 450



layout(set = 1, binding = 0) uniform sampler2D tex_dfsSampler;
layout(set = 1, binding = 1) uniform sampler2D tex_spcSampler;
layout(set = 1, binding = 2) uniform sampler2D tex_nrmSampler;

layout(location = 0) in vec4 frg_col;
layout(location = 1) in vec2 frg_tex;

layout(location = 0) out vec4 out_col;



void main() {
	out_col =
		vec4(
			texture(tex_dfsSampler, frg_tex).rgb * frg_col.rgb,
			frg_col.a );
}

