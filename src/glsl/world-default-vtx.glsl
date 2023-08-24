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
layout(location =  5) in mat4  in_transf;
layout(location =  9) in vec4  in_col;
layout(location = 10) in float in_rnd;

layout(location = 0) out vec4 frg_col;
layout(location = 1) out vec2 frg_tex;



void main() {
	gl_Position = frame_ubo.projview_transf * in_transf * vec4(in_pos, 1.0);
	frg_col     = in_col;
	frg_tex     = in_tex;
}
