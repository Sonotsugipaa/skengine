#version 450



/*layout(location = 0) in vec2 frg_tex;
layout(location = 1) in vec3 frg_nrmTan;
layout(location = 2) in vec4 frg_col;
layout(location = 3) in mat3 frg_tbnInverse;*/

layout(location = 0) in vec4 frg_col;

layout(location = 0) out vec4 out_col;



void main() {
	out_col = frg_col;
	out_col = vec4(1, 0, 0, 1);
}

