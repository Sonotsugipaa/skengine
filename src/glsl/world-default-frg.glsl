#version 450



layout(set = 0, binding = 0) uniform FrameUbo {
	mat4 projview_transf4;
	mat4 proj_transf4;
	mat4 view_transf4;
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

layout(set = 1, binding = 4) uniform MaterialUbo {
	float shininess;
} material_ubo;

layout(location = 0) in vec4 frg_pos;
layout(location = 1) in vec4 frg_col;
layout(location = 2) in vec2 frg_tex;
layout(location = 3) in vec3 frg_nrm;
layout(location = 4) in mat3 frg_tbn;
layout(location = 7) in mat3 frg_view3;

layout(location = 0) out vec4 out_col;

const float normal_backface_bias = 0.001;



float compute_flat_reflection(vec3 tex_nrm_viewspace, vec3 light_dir_viewspace, vec3 view_pos, float angle_of_attack) {
	float lighting = dot(
		view_pos,
		reflect(light_dir_viewspace, tex_nrm_viewspace) );
	if(normal_backface_bias > angle_of_attack) lighting = 0.0;
	return clamp(pow(lighting, material_ubo.shininess), 0.0, 1.0);
}

float compute_rough_reflection(vec3 tex_nrm_viewspace, vec3 light_dir_viewspace, float angle_of_attack) {
	float lighting = dot(tex_nrm_viewspace, light_dir_viewspace);
	if(normal_backface_bias > angle_of_attack) lighting = 0.0;
	return clamp(lighting, 0, 1);
}


float multistep(float v) {
	float r      = v;
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


vec2 sum_ray_lighting(vec3 tex_nrm_viewspace, vec3 view_pos) {
	float lighting_dfs = 0.0;
	float lighting_spc = 0.0;
	for(uint i = 0; i < frame_ubo.ray_light_count; ++i) {
		vec3 light_dir =
			frg_view3
			* ray_light_buffer.lights[i].direction.xyz;

		float aot = dot(frg_nrm, light_dir);

		lighting_dfs +=
			ray_light_buffer.lights[i].intensity
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aot);

		lighting_spc +=
			ray_light_buffer.lights[i].intensity
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_pos, aot);
	}





lighting_spc = 0;





	return vec2(lighting_dfs, lighting_spc);
}


vec2 sum_point_lighting(vec3 tex_nrm_viewspace, vec3 view_pos) {
	float lighting_dfs = 0.0;
	float lighting_spc = 0.0;
	uint  light_count  = frame_ubo.ray_light_count + frame_ubo.point_light_count;
	for(uint i = frame_ubo.ray_light_count; i < light_count; ++i) {
		vec3 light_dir =
			point_light_buffer.lights[i].position.xyz
			- frg_pos.xyz;

		light_dir = normalize(frg_view3 * light_dir);

		float aot = dot(frg_nrm, light_dir);

		float intensity = point_light_buffer.lights[i].intensity;
		float falloff = pow(
			distance(frg_pos.xyz, point_light_buffer.lights[i].position.xyz),
			point_light_buffer.lights[i].falloff_exp );

		lighting_dfs +=
			intensity
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aot)
			/ falloff;

		lighting_spc +=
			intensity
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_pos, aot);
	}
	return vec2(lighting_dfs, lighting_spc);
}



void main() {
	vec4 tex_dfs = texture(tex_dfsSampler, frg_tex);
	vec3 tex_nrm = texture(tex_nrmSampler, frg_tex).rgb;
	vec4 tex_spc = texture(tex_spcSampler, frg_tex);
	vec4 tex_emi = texture(tex_emiSampler, frg_tex);

	tex_nrm = normalize((tex_nrm * 2.0) - 1.0);

	vec3 tex_nrm_viewspace = frg_tbn * tex_nrm;
	vec3 view_pos          = normalize(frg_view3 * ((frg_pos.xyz) - (frame_ubo.view_pos.xyz)));

	vec2 lighting = vec2(0, 0);

	lighting += sum_ray_lighting(tex_nrm_viewspace, view_pos);
	lighting += sum_point_lighting(tex_nrm_viewspace, view_pos);

	float lighting_sum = lighting.x + lighting.y;

	if(frame_ubo.shade_step_count > 0) {
		lighting.x = multistep(lighting.x);
		lighting.y = multistep(lighting.y);
		lighting_sum = multistep(lighting_sum);
	}

	lighting_sum = clamp(lighting_sum, 0.0, 1.0);

	out_col.rgb =
		(frg_col.rgb * (
			(tex_dfs.rgb * lighting.x) +
			(tex_spc.rgb * lighting.y)
		))
		+ (tex_emi.rgb * (1 - lighting_sum));
	out_col.rgb = min(out_col.rgb, 1.0);
	out_col.a = frg_col.a;
}
