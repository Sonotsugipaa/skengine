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


struct RayLight {
	vec4  direction;
	vec4  color;
	float aoa_threshold;
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

layout(location = 0) out vec4 out_col;



void main() {
	out_col = vec4(0, 0, 0, 1);
}
