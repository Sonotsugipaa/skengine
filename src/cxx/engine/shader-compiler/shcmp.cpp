#include "shcmp.hpp"

#include <posixfio.hpp>



#define FAILED_TO_COMPILE_FMT "Failed to compile \"{}\":\n{}"



namespace SKENGINE_NAME_NS {
inline namespace shcmp {

	shaderc::Compiler ShaderCompiler::sc_compiler;

	shaderc::CompileOptions ShaderCompiler::sc_opt = []() {
		shaderc::CompileOptions r;
		#ifndef NDEBUG
			r.SetGenerateDebugInfo();
		#endif
		r.SetSourceLanguage(shaderc_source_language_glsl);
		r.SetTargetSpirv(shaderc_spirv_version_1_6);
		r.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
		return r;
	} ();


	shaderc::SpvCompilationResult ShaderCompiler::fileGlslToSpv(const char* filename, shaderc_shader_kind kind) {
		using namespace posixfio;
		auto file    = File::open(filename, OpenFlags::eRdonly);
		auto len     = file.lseek(0, Whence::eEnd); file.lseek(0, Whence::eSet);
		auto map     = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
		auto res     = sc_compiler.CompileGlslToSpv(map.get<char>(), len, kind, filename, sc_opt);
		auto cstatus = res.GetCompilationStatus();
		if(cstatus != shaderc_compilation_status_success) {
			throw std::runtime_error(fmt::format("Failed to compile \"{}\":\n{}", filename, res.GetErrorMessage()));
		}
		return res;
	}


	VkShaderModule ShaderCompiler::fileGlslToModule(
		VkDevice            dev,
		const char*         filename,
		shaderc_shader_kind kind
	) {
		VkShaderModule r;
		auto   res = fileGlslToSpv(filename, kind);
		size_t len = (res.end() - res.begin()) * (sizeof *res.begin());

		VkShaderModuleCreateInfo smcInfo = { };
		smcInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		smcInfo.codeSize = len;
		smcInfo.pCode    = res.begin();
		VK_CHECK(vkCreateShaderModule, dev, &smcInfo, nullptr, &r);
		return r;
	}


	VkShaderModule ShaderCompiler::glslSourceToModule(
		VkDevice            dev,
		const char*         name,
		std::string_view    source,
		shaderc_shader_kind kind
	) {
		VkShaderModule r;
		auto res     = sc_compiler.CompileGlslToSpv(source.data(), source.size(), kind, name, sc_opt);
		auto cstatus = res.GetCompilationStatus();
		if(cstatus != shaderc_compilation_status_success) {
			throw std::runtime_error(fmt::format("Failed to compile \"{}\":\n{}", name, res.GetErrorMessage()));
		}

		VkShaderModuleCreateInfo smcInfo = { };
		size_t len = (res.end() - res.begin()) * (sizeof *res.begin());
		smcInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		smcInfo.codeSize = len;
		smcInfo.pCode    = res.begin();
		VK_CHECK(vkCreateShaderModule, dev, &smcInfo, nullptr, &r);
		return r;
	}

}}



#undef FAILED_TO_COMPILE_FMT
