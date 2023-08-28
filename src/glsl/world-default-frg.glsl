#version 450



layout(set = 0, binding = 0) uniform FrameUbo {
	mat4 projview_transf;
	mat4 proj_transf;
	mat4 view_transf;
	uint ray_light_count;
	uint point_light_count;
	float rnd;
	float time_delta;
} frame_ubo;



layout(set = 1, binding = 0) uniform sampler2D tex_dfsSampler;
layout(set = 1, binding = 1) uniform sampler2D tex_nrmSampler;
layout(set = 1, binding = 2) uniform sampler2D tex_spcSampler;
layout(set = 1, binding = 3) uniform sampler2D tex_emiSampler;

layout(location = 0) in vec4 frg_col;
layout(location = 1) in vec2 frg_tex;
layout(location = 2) in mat3 frg_tbn;
layout(location = 5) in vec3 frg_viewspace_lightdir;

layout(location = 0) out vec4 out_col;



void main() {
	vec4 tex_dfs = texture(tex_dfsSampler, frg_tex);
	vec3 tex_nrm = texture(tex_nrmSampler, frg_tex).rgb;
	vec4 tex_spc = texture(tex_spcSampler, frg_tex);
	vec4 tex_emi = texture(tex_emiSampler, frg_tex);

	tex_nrm = normalize((tex_nrm * 2) - 1);

	vec3 tex_nrm_viewspace = frg_tbn * tex_nrm;

	float lighting = dot(tex_nrm_viewspace, frg_viewspace_lightdir);
	//lighting = round(lighting * 12) / 12;

	out_col.rgb = tex_dfs.rgb * frg_col.rgb * lighting;
	out_col.a   = frg_col.a;
}

