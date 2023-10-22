#pragma once

#include <skengine_fwd.hpp>

#include <shaderc/shaderc.hpp>

#include <fmt/format.h>

#include <vulkan/vulkan.h>

#include <vk-util/error.hpp>

#include <string_view>



namespace SKENGINE_NAME_NS {
inline namespace shcmp {

	class ShaderCompiler {
	public:
		static shaderc::SpvCompilationResult fileGlslToSpv(
			const char*         filename,
			shaderc_shader_kind kind );

		static VkShaderModule fileGlslToModule(
			VkDevice,
			const char*         filename,
			shaderc_shader_kind kind );

		static VkShaderModule glslSourceToModule(
			VkDevice,
			const char*         name,
			std::string_view    source,
			shaderc_shader_kind kind );

	private:
		static shaderc::Compiler       sc_compiler;
		static shaderc::CompileOptions sc_opt;
	};

}}
