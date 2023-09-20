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
	vec3  direction;
	float direction_padding;
	float intensity;
	float unused0;
	float unused1;
	float unused2;
};

struct PointLight {
	vec3  position;
	float position_padding;
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
const float pi                   = 3.14159265358;



vec3 unorm_correct(vec3 v) {
	// v = v * 255.0;
	// v = min(v, 254.0);
	// v = v / 254.0;
	return min(v * 255.0, 254.0) / 254.0;
}

float shinify(float x) {
	return (sin((x - 0.5) * pi) + 1.0) / 2.0;
}

float shinify_exp(float x, float exp) {
	x = clamp(x, 0.0, 1.0);
	return pow(shinify(pow(x, exp)), exp);
}


float compute_flat_reflection(vec3 tex_nrm_viewspace, vec3 light_dir_viewspace, vec3 view_dir, float angle_of_attack) {
	float lighting = dot(
		view_dir,
		reflect(light_dir_viewspace, tex_nrm_viewspace) );
	float light_exp = material_ubo.shininess;
	if(normal_backface_bias > angle_of_attack) lighting = 0.0;
	return shinify_exp(lighting, light_exp);
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


vec2 sum_ray_lighting(vec3 tex_nrm_viewspace, vec3 view_dir) {
	float lighting_dfs = 0.0;
	float lighting_spc = 0.0;
	for(uint i = 0; i < frame_ubo.ray_light_count; ++i) {
		vec3 light_dir =
			frg_view3
			* ray_light_buffer.lights[i].direction;

		float aot = dot(frg_nrm, light_dir);

		lighting_dfs +=
			ray_light_buffer.lights[i].intensity
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aot);

		lighting_spc +=
			ray_light_buffer.lights[i].intensity
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_dir, aot);
	}
	return vec2(lighting_dfs, lighting_spc);
}


vec2 sum_point_lighting(vec3 tex_nrm_viewspace, vec3 view_dir) {
	float lighting_dfs = 0.0;
	float lighting_spc = 0.0;
	uint  light_count  = frame_ubo.ray_light_count + frame_ubo.point_light_count;
	for(uint i = frame_ubo.ray_light_count; i < light_count; ++i) {
		vec3 light_dir =
			point_light_buffer.lights[i].position
			- frg_pos.xyz;

		light_dir = normalize(frg_view3 * light_dir);

		float aot = dot(frg_nrm, light_dir);

		float intensity         = point_light_buffer.lights[i].intensity;
		float fragm_distance    = distance(frg_pos.xyz, point_light_buffer.lights[i].position);
		float intensity_falloff = intensity / pow(fragm_distance, point_light_buffer.lights[i].falloff_exp);

		lighting_dfs +=
			intensity_falloff
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aot);

		lighting_spc +=
			intensity_falloff
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_dir, aot);
	}
	return vec2(lighting_dfs, lighting_spc);
}


vec3 color_excess_filter(vec3 col) {
	float len = length(col);
	if(len > 1.0) {
		float excess_len = len - 1.0;
		return (col / len) + (excess_len);
	}
	return col;
}



void main() {
	vec4 tex_dfs = texture(tex_dfsSampler, frg_tex);
	vec3 tex_nrm = texture(tex_nrmSampler, frg_tex).rgb;
	vec4 tex_spc = texture(tex_spcSampler, frg_tex);
	vec4 tex_emi = texture(tex_emiSampler, frg_tex);

	tex_nrm = unorm_correct(tex_nrm);
	tex_nrm = normalize((tex_nrm * 2.0) - 1.0);

	vec3 tex_nrm_viewspace = normalize(frg_tbn * tex_nrm);
	vec3 view_dir          = normalize(frg_view3 * ((frg_pos.xyz) - (frame_ubo.view_pos.xyz)));

	vec2 lighting = vec2(0, 0);

	lighting += sum_ray_lighting(tex_nrm_viewspace, view_dir);
	lighting += sum_point_lighting(tex_nrm_viewspace, view_dir);

	if(frame_ubo.shade_step_count > 0) {
		lighting.x = multistep(lighting.x);
		lighting.y = multistep(lighting.y);
	}

	out_col.rgb =
		(frg_col.rgb * (
			(tex_dfs.rgb * lighting.x) +
			(tex_spc.rgb * lighting.y)
		))
		+ tex_emi.rgb;
	out_col.rgb = color_excess_filter(out_col.rgb);
	out_col.a = frg_col.a;
}
