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



layout(location =  0) in vec3  in_pos;
layout(location =  1) in vec2  in_tex;
layout(location =  2) in vec3  in_nrm;
layout(location =  3) in vec3  in_tanu;
layout(location =  4) in vec3  in_tanv;
layout(location =  5) in mat4  in_obj_transf;
layout(location =  9) in vec4  in_col;
layout(location = 10) in float in_rnd;

layout(location = 0) out vec4 frg_col;
layout(location = 1) out vec2 frg_tex;
layout(location = 2) out mat3 frg_tbn;
layout(location = 5) out vec3 frg_viewspace_lightdir;
layout(location = 6) out vec3 frg_nrm;



void main() {
	vec4 worldspace_pos = in_obj_transf * vec4(in_pos, 1.0);
	vec4 viewspace_pos  = frame_ubo.view_transf * worldspace_pos;

	gl_Position = frame_ubo.proj_transf * viewspace_pos;
	frg_col     = in_col;
	frg_tex     = in_tex;

	mat3 view3       = inverse(transpose(mat3(frame_ubo.view_transf)));
	mat3 obj_transf3 = inverse(transpose(mat3(in_obj_transf)));

	vec3 worldspace_tanu = obj_transf3 * in_tanu;
	vec3 worldspace_tanv = obj_transf3 * in_tanv;
	vec3 worldspace_tanw = obj_transf3 * in_nrm;
	vec3 viewspace_tanu  = normalize(view3 * worldspace_tanu);
	vec3 viewspace_tanv  = normalize(view3 * worldspace_tanv);
	vec3 viewspace_tanw  = normalize(view3 * worldspace_tanw);

	frg_tbn = transpose(inverse(mat3(
		viewspace_tanu,
		viewspace_tanv,
		viewspace_tanw )));

	frg_nrm = normalize(view3 * obj_transf3 * in_nrm);

	vec3 worldspace_lightpos = vec3(2, 1, 5);
	vec3 viewspace_lightpos  = view3 * worldspace_lightpos;
	frg_viewspace_lightdir   = normalize(viewspace_lightpos);
}
