#version 450



/*layout(set = 0, binding = 0) uniform StaticUbo {
	mat4 proj;
} staticUbo;

layout(set = 1, binding = 0) uniform ModelUbo {
	uint _unused;
} modelUbo;

layout(set = 2, binding = 0) uniform FrameUbo {
	uint _unused;
} frameUbo;*/



/*layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_nrm;
layout(location = 2) in vec4 in_tanu;
layout(location = 3) in vec4 in_tanv;
layout(location = 4) in vec2 in_tex;

layout(location = 5)  in mat4  in_modelMat;
layout(location = 9)  in vec4  in_col;
layout(location = 10) in float in_rnd;

layout(location = 0) out vec2 frg_tex;
layout(location = 1) out vec3 frg_nrmTan;
layout(location = 2) out vec4 frg_col;
layout(location = 3) out mat3 frg_tbnInverse;*/

layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_col;
layout(location = 0) out vec4 frg_col;



void main() {
	gl_Position = vec4(in_pos.xyz, 1.0);
	//gl_Position = staticUbo.proj * modelViewMat * vec4(in_pos.xyz, 1.0);
	frg_col     = in_col;
}
