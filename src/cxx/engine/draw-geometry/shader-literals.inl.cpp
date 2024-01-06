#pragma once

#include <string_view>



namespace SKENGINE_NAME_NS {
inline namespace geom_shaders {

	constexpr const std::string_view polyVtxSrc =
	"#version 450\n"
	""
	"layout(location = 0) in vec3 in_pos;"
	"layout(location = 1) in vec4 in_col;"
	"layout(location = 2) in mat4 in_transform;"
	"layout(location = 0) out vec4 frg_col;"
	""
	"void main() {"
		"gl_Position   = in_transform * vec4(in_pos, 1.0);"
		"frg_col       = in_col;"
	"}";


	constexpr const std::string_view polyFrgSrc =
	"#version 450\n"
	""
	"layout(location = 0) in vec4 frg_col;"
	"layout(location = 0) out vec4 out_col;"
	""
	"void main() {"
		"out_col = frg_col;"
	"}";


	constexpr const std::string_view textVtxSrc =
	"#version 450\n"
	""
	"layout(location = 0) in vec3 in_pos;"
	"layout(location = 1) in vec2 in_tex;"
	"layout(location = 2) in vec4 in_col;"
	"layout(location = 3) in mat4 in_transform;"
	"layout(location = 0) out vec4 frg_col;"
	"layout(location = 1) out vec2 frg_tex;"
	""
	"layout(push_constant) uniform constants {"
		"float xOffset;"
		"float yOffset;"
		"float zOffset;"
		"float xScale;"
		"float yScale;"
		"float zScale;"
	"} transform;"
	""
	"void main() {"
		"gl_Position   = in_transform * vec4(in_pos, 1.0);"
		"gl_Position  *= vec4(transform.xScale,  transform.yScale,  transform.zScale,  1);"
		"gl_Position  += vec4(transform.xOffset, transform.yOffset, transform.zOffset, 0);"
		"frg_col       = in_col;"
		"frg_tex       = in_tex;"
	"}";


	constexpr const std::string_view textFrgSrc =
	"#version 450\n"
	""
	"layout(location = 0) in vec4 frg_col;"
	"layout(location = 1) in vec2 frg_tex;"
	"layout(location = 0) out vec4 out_col;"
	"layout(set = 0, binding = 0) uniform sampler2D tex_text;"
	""
	"void main() {"
		"out_col = frg_col * texture(tex_text, frg_tex);"
	"}";

}}
