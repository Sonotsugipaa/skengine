#version 450



layout(set = 1, binding = 0) uniform sampler2D tex_dfsSampler;
layout(set = 1, binding = 1) uniform sampler2D tex_nrmSampler;
layout(set = 1, binding = 2) uniform sampler2D tex_spcSampler;
layout(set = 1, binding = 3) uniform sampler2D tex_emiSampler;

layout(location = 0) in vec4 frg_col;
layout(location = 1) in vec2 frg_tex;

layout(location = 0) out vec4 out_col;



void main() {
	vec4 tex_dfs = texture(tex_dfsSampler, frg_tex);
	vec4 tex_nrm = texture(tex_nrmSampler, frg_tex);
	vec4 tex_spc = texture(tex_spcSampler, frg_tex);
	vec4 tex_emi = texture(tex_emiSampler, frg_tex);

	float lighting = 1.0;

	out_col.rgb = tex_dfs.rgb * frg_col.rgb * lighting;
	out_col.a   = frg_col.a;

	// Overlap normal texture
	out_col.rgb = (
		(out_col.rgb                * 3 / 4) +
		(tex_nrm.rgb * frg_col.rgb) * 1 / 4);
}

