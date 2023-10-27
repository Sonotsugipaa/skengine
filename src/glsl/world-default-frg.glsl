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
	uint flags;
} frame_ubo;


struct RayLight {
	vec4  direction;
	vec4  color;
	float unused0;
	float unused1;
	float unused2;
	float unused3;
};

struct PointLight {
	vec4  position;
	vec4  color;
	float falloff_exp;
	float unused0;
	float unused1;
	float unused2;
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
layout(location = 3) in vec2 frg_viewport_pos;
layout(location = 4) in vec3 frg_nrm;
layout(location = 5) in vec3 frg_viewspace_tanu;
layout(location = 6) in vec3 frg_viewspace_tanv;
layout(location = 7) in vec3 frg_viewspace_tanw;
layout(location = 8) in mat3 frg_view3;

layout(location = 0) out vec4 out_col;

const float normal_backface_bias = -0.05;
const float pi                   = 3.14159265358;
const uint  flag_hdr_enabled     = 1;



struct LuminanceInfo {
	vec4 dfs;
	vec4 spc;
};


vec3 unorm_correct(vec3 v) {
	// v = v * 255;
	// if(v >= 127 && v <= 128) v = 127.5;
	// v = v / 255;
	v = v * 255.0;
	v.x = v.x + (v.x < 126.9? 0.0 : 0.5) - (v.x < 127.9? 0.0 : 0.5);
	v.y = v.y + (v.y < 126.9? 0.0 : 0.5) - (v.y < 127.9? 0.0 : 0.5);
	v.z = v.z + (v.z < 126.9? 0.0 : 0.5) - (v.z < 127.9? 0.0 : 0.5);
	return v / 255.0;
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


LuminanceInfo sum_ray_lighting(vec3 tex_nrm_viewspace, vec3 view_dir) {
	LuminanceInfo luminance;
	luminance.dfs = vec4(0.0, 0.0, 0.0, 0.0);
	luminance.spc = vec4(0.0, 0.0, 0.0, 0.0);

	// Sum luminances
	for(uint i = 0; i < frame_ubo.ray_light_count; ++i) {
		vec3 light_dir =
			frg_view3
			* ray_light_buffer.lights[i].direction.xyz;

		float aot = dot(frg_nrm, light_dir);

		luminance.dfs.a += (
			ray_light_buffer.lights[i].color.a
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aot) );

		luminance.spc.a += (
			ray_light_buffer.lights[i].color.a
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_dir, aot) );
	}

	// Sum colors
	for(uint i = 0; i < frame_ubo.ray_light_count; ++i) {
		luminance.dfs.rgb += ray_light_buffer.lights[i].color.rgb * luminance.dfs.a;
		luminance.spc.rgb += ray_light_buffer.lights[i].color.rgb * luminance.dfs.a;
	}

	return luminance;
}


LuminanceInfo sum_point_lighting(vec3 tex_nrm_viewspace, vec3 view_dir) {
	LuminanceInfo luminance;
	luminance.dfs = vec4(0.0, 0.0, 0.0, 0.0);
	luminance.spc = vec4(0.0, 0.0, 0.0, 0.0);
	uint light_count = frame_ubo.ray_light_count + frame_ubo.point_light_count;

	// Sum luminances
	for(uint i = frame_ubo.ray_light_count; i < light_count; ++i) {
		vec3 light_dir =
			point_light_buffer.lights[i].position.xyz
			- frg_pos.xyz;

		light_dir = normalize(frg_view3 * light_dir);

		float aot = dot(frg_nrm, light_dir);

		float intensity         = point_light_buffer.lights[i].color.a;
		float fragm_distance    = distance(frg_pos.xyz, point_light_buffer.lights[i].position.xyz);
		float intensity_falloff = intensity / pow(fragm_distance, point_light_buffer.lights[i].falloff_exp);

		luminance.dfs.a += (
			intensity_falloff
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aot) );

		luminance.spc.a += (
			intensity_falloff
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_dir, aot) );
	}

	// Sum colors
	for(uint i = frame_ubo.ray_light_count; i < light_count; ++i) {
		luminance.dfs.rgb += point_light_buffer.lights[i].color.rgb * luminance.dfs.a;
		luminance.spc.rgb += point_light_buffer.lights[i].color.rgb * luminance.dfs.a;
	}

	return luminance;
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
	mat3 tbn = transpose(inverse(mat3(
		normalize(frg_viewspace_tanu),
		normalize(frg_viewspace_tanv),
		normalize(frg_viewspace_tanw) )));

	vec4 tex_dfs = texture(tex_dfsSampler, frg_tex);
	vec3 tex_nrm = texture(tex_nrmSampler, frg_tex).rgb;
	vec4 tex_spc = texture(tex_spcSampler, frg_tex);
	vec4 tex_emi = texture(tex_emiSampler, frg_tex);

	tex_nrm = unorm_correct(tex_nrm);
	tex_nrm = normalize((tex_nrm * 2.0) - 1.0);

	vec3 tex_nrm_viewspace = normalize(tbn * tex_nrm);
	vec3 view_dir          = normalize(frg_view3 * ((frg_pos.xyz) - (frame_ubo.view_pos.xyz)));

	LuminanceInfo luminance;
	LuminanceInfo ray_luminance = sum_ray_lighting(tex_nrm_viewspace, view_dir);
	LuminanceInfo pt_luminance  = sum_point_lighting(tex_nrm_viewspace, view_dir);
	luminance.dfs = ray_luminance.dfs + pt_luminance.dfs;
	luminance.spc = ray_luminance.spc + pt_luminance.spc;

	if(frame_ubo.shade_step_count > 0) {
		luminance.dfs.a = multistep(luminance.dfs.a);
		luminance.spc.a = multistep(luminance.spc.a);
	}

	out_col.rgb =
		(frg_col.rgb * (
			(tex_dfs.rgb * luminance.dfs.rgb * luminance.dfs.a) +
			(tex_spc.rgb * luminance.spc.rgb * luminance.spc.a)
		))
		+ tex_emi.rgb;
	out_col.a = frg_col.a;

	// Handle colors > 1 in a LDR-friendly way
	if((frame_ubo.flags & flag_hdr_enabled) != 0) {
		out_col.rgb = color_excess_filter(out_col.rgb);
	}
}
