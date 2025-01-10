#version 450



layout(set = 0, binding = 0) uniform FrameUbo {
	mat4 projview_transf4;
	mat4 proj_transf4;
	mat4 view_transf4;
	vec4 view_pos;
	vec4 ambient_lighting;
	uint ray_light_count;
	uint point_light_count;
	uint shade_step_count;
	float shade_step_smooth;
	float shade_step_exp;
	float dithering_steps;
	float rnd;
	float time_delta;
	float p_light_dist_threshold;
	uint flags;
} frame_ubo;



layout(location =  0) in vec3  in_pos;
layout(location =  1) in vec2  in_tex;
layout(location =  2) in vec3  in_nrm;
layout(location =  3) in vec3  in_tanu;
layout(location =  4) in vec3  in_tanv;
layout(location =  5) in mat4  in_obj_transf;
layout(location =  9) in vec4  in_col;
layout(location = 10) in float in_rnd;

layout(location = 0) out vec4 frg_pos;
layout(location = 1) out vec4 frg_col;
layout(location = 2) out vec2 frg_tex;
layout(location = 3) out vec2 frg_viewport_pos;
layout(location = 4) out vec3 frg_nrm;
layout(location = 5) out vec3 frg_viewspace_tanu;
layout(location = 6) out vec3 frg_viewspace_tanv;
layout(location = 7) out vec3 frg_viewspace_tanw;
layout(location = 8) out mat3 frg_view3;



void main() {
	vec4 worldspace_pos = in_obj_transf * vec4(in_pos, 1.0);
	vec4 viewspace_pos  = frame_ubo.view_transf4 * worldspace_pos;

	gl_Position = frame_ubo.proj_transf4 * viewspace_pos;
	frg_pos     = worldspace_pos;
	frg_col     = in_col;
	frg_tex     = in_tex;
	frg_viewport_pos = gl_Position.xy;

	mat3 iview3      = inverse(mat3(frame_ubo.view_transf4));
	mat3 view3       = transpose(iview3);
	mat3 obj_transf3 = mat3(in_obj_transf);
	frg_view3        = view3;

	vec3 worldspace_tanu = normalize(obj_transf3 * -in_tanu);
	vec3 worldspace_tanv = normalize(obj_transf3 * -in_tanv);
	vec3 worldspace_tanw = normalize(obj_transf3 * +in_nrm);

	{ // Gram-Schmidt process
		vec3 viewspace_u   = view3 * worldspace_tanu;
		vec3 viewspace_v   = view3 * worldspace_tanv;
		vec3 viewspace_w   = view3 * worldspace_tanw;
		frg_viewspace_tanu = viewspace_u - (viewspace_w * dot(viewspace_w, viewspace_u));
		frg_viewspace_tanv = viewspace_v - (viewspace_w * dot(viewspace_w, viewspace_v));
		frg_viewspace_tanw = viewspace_w;
	}

	frg_nrm = normalize(view3 * obj_transf3 * in_nrm);
}
