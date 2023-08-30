#version 450



layout(set = 0, binding = 0) uniform FrameUbo {
	mat4 projview_transf;
	mat4 proj_transf;
	mat4 view_transf;
	vec4 view_pos;
	uint ray_light_count;
	uint point_light_count;
	uint shade_step_count;
	float shade_step_smooth;
	float shade_step_exp;
	float rnd;
	float time_delta;
} frame_ubo;


struct RayLight {
	vec4  direction;
	float intensity;
	float unused0;
	float unused1;
	float unused2;
};

struct PointLight {
	vec4  position;
	float intensity;
	float falloff_exp;
	float unused0;
	float unused1;
};

layout(std430, set = 0, binding = 1) readonly buffer RayLightBuffer {
	RayLight lights[];
} ray_light_buffer;

layout(std430, set = 0, binding = 1) readonly buffer PointLightBuffer {
	PointLight lights[];
} point_light_buffer;



layout(set = 1, binding = 0) uniform sampler2D tex_dfsSampler;
layout(set = 1, binding = 1) uniform sampler2D tex_nrmSampler;
layout(set = 1, binding = 2) uniform sampler2D tex_spcSampler;
layout(set = 1, binding = 3) uniform sampler2D tex_emiSampler;

layout(location = 0) in vec4 frg_pos;
layout(location = 1) in vec4 frg_col;
layout(location = 2) in vec2 frg_tex;
layout(location = 3) in vec3 frg_nrm;
layout(location = 4) in mat3 frg_tbn;
layout(location = 7) in mat3 frg_view3;

layout(location = 0) out vec4 out_col;



float compute_rough_reflection(vec3 tex_nrm_viewspace, vec3 light_dir_viewspace) {
	float lighting = dot(tex_nrm_viewspace, light_dir_viewspace);
	if(0.001 > dot(frg_nrm, light_dir_viewspace)) lighting = 0.0;
	return clamp(lighting, 0, 1);
}


float lighting_multistep(float v) {
	float r = v;
	float fcount = frame_ubo.shade_step_count;
	r = pow(r, 1.0 / frame_ubo.shade_step_exp);
	r = r * fcount;
	r = round(r);
	r = r / fcount;
	r = pow(r, frame_ubo.shade_step_exp);
	r += (v * frame_ubo.shade_step_smooth);
	r /= (1.0 + frame_ubo.shade_step_smooth);
	return r;
}


float sum_ray_lighting(vec3 tex_nrm_viewspace) {
	float lighting = 0.0;
	for(uint i = 0; i < frame_ubo.ray_light_count; ++i) {
		vec3 light_dir =
			frg_view3
			* ray_light_buffer.lights[i].direction.xyz;
		lighting +=
			ray_light_buffer.lights[i].intensity
			* compute_rough_reflection(tex_nrm_viewspace, light_dir);
	}
	return lighting;
}


float sum_point_lighting(vec3 tex_nrm_viewspace) {
	float lighting    = 0.0;
	uint  light_count = frame_ubo.ray_light_count + frame_ubo.point_light_count;
	for(uint i = frame_ubo.ray_light_count; i < light_count; ++i) {
		vec3 light_dir =
			point_light_buffer.lights[i].position.xyz
			- frg_pos.xyz;
		light_dir = normalize(frg_view3 * light_dir);
		float add = compute_rough_reflection(tex_nrm_viewspace, light_dir);
		lighting +=
			point_light_buffer.lights[i].intensity
			* compute_rough_reflection(tex_nrm_viewspace, light_dir)
			/ pow(
				distance(frg_pos.xyz, point_light_buffer.lights[i].position.xyz),
				point_light_buffer.lights[i].falloff_exp
			);
	}
	return lighting;
}



void main() {
	vec4 tex_dfs = texture(tex_dfsSampler, frg_tex);
	vec3 tex_nrm = texture(tex_nrmSampler, frg_tex).rgb;
	vec4 tex_spc = texture(tex_spcSampler, frg_tex);
	vec4 tex_emi = texture(tex_emiSampler, frg_tex);

	tex_nrm = normalize((tex_nrm * 2.0) - 1.0);

	vec3 tex_nrm_viewspace = frg_tbn * tex_nrm;

	float lighting = 0.0;

	lighting += sum_ray_lighting(tex_nrm_viewspace);
	lighting += sum_point_lighting(tex_nrm_viewspace);

	if(frame_ubo.shade_step_count > 0) {
		lighting = lighting_multistep(lighting);
	}

	lighting = max(lighting, 0.0);

	out_col.rgb =
		(tex_dfs.rgb * frg_col.rgb * lighting) +
		(tex_emi.rgb * (1 - lighting));
	out_col.rgb = min(out_col.rgb, 1.0);
	out_col.a = frg_col.a;
}
