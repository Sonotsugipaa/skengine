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



void main() {
	mat4 objview_transf = frame_ubo.view_transf4 * in_obj_transf;
	vec4 viewspace_pos = objview_transf * vec4(in_pos, 1.0);
	vec4 viewspace_nrm = objview_transf * vec4(in_nrm, 1.0);

	//viewspace_pos.xyz -= normalize(viewspace_nrm.xyz) * 0.1;

	gl_Position = vec4(0, 0, 0, 1);
	//gl_Position.xy += normalize(viewspace_nrm.xy) * 0.1;
	//gl_Position.z *= 1.1;
	//gl_Position.w *= 1.1;
	gl_Position.z = -1.1;
}
