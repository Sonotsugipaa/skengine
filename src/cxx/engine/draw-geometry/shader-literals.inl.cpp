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
	"layout(location = 0) out vec4 out_col;"
	""
	"void main() {"
		"gl_Position   = in_transform * vec4(in_pos, 1.0);"
		//"gl_Position   = vec4(in_pos, 1.0);"
		"gl_Position.z = 0.0;"
		"out_col       = in_col;"
	"}";


	constexpr const std::string_view polyFrgSrc =
	"#version 450\n"
	""
	"layout(location = 0) in vec4 in_col;"
	"layout(location = 0) out vec4 out_col;"
	""
	"void main() {"
		"out_col = in_col;"
	"}";

}}
