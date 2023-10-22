#pragma once

#include <string_view>



namespace SKENGINE_NAME_NS {
inline namespace geom_shaders {

	constexpr const std::string_view polyLineVtxSrc =
	"#version 450\n"
	""
	"layout(location = 0) in vec3 in_pos;"
	"layout(location = 1) in vec4 in_col;"
	"layout(location = 2) in vec3 in_transform_x;"
	"layout(location = 3) in vec3 in_transform_y;"
	"layout(location = 4) in vec3 in_transform_z;"
	"layout(location = 0) out vec4 out_col;"
	""
	"void main() {"
		"mat3 transform = mat3(1.0)/*mat3(in_transform_x, in_transform_y, in_transform_z)*/;"
		"gl_Position    = vec4(transform * in_pos, 1.0);"
		"out_col        = in_col;"
	"}";


	constexpr const std::string_view polyLineFrgSrc =
	"#version 450\n"
	""
	"layout(location = 0) in vec4 in_col;"
	"layout(location = 0) out vec4 out_col;"
	""
	"void main() {"
		"out_col = in_col;"
	"}";

}}
