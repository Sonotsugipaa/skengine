#include "shader_cache.hpp"

#include <posixfio_tl.hpp>

#include <memory>
#include <cstdint>

#include <vk-util/error.hpp>



namespace SKENGINE_NAME_NS {

	std::size_t ShaderModuleSetHash::operator()(const ShaderModuleSet& req) const noexcept {
		using namespace SKENGINE_NAME_NS_SHORT;
		std::size_t hv = std::hash<VkShaderModule>()(req.vertex);
		std::size_t hf = std::hash<VkShaderModule>()(req.fragment);
		return hv ^ std::rotr(hf, 7);
	}


	bool ShaderModuleSetCompare::operator()(const ShaderModuleSet& l, const ShaderModuleSet& r) const noexcept {
		return l.vertex == r.vertex && l.fragment == r.fragment;
	}


	VkShaderModule createShaderModuleFromMemory(VkDevice dev, std::span<const uint32_t> code) {
		VkShaderModuleCreateInfo sm_info = { };
		sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		sm_info.pCode    = code.data();
		sm_info.codeSize = code.size_bytes();
		VkShaderModule r;
		VK_CHECK(vkCreateShaderModule, dev, &sm_info, nullptr, &r);
		return r;
	}


	VkShaderModule createShaderModuleFromFile(VkDevice dev, const std::string& file_path) {
		using namespace posixfio;
		static_assert(4 == sizeof(uint32_t));

		VkShaderModuleCreateInfo    sm_info = { };
		std::unique_ptr<uint32_t[]> buffer;
		try {
			auto file    = posixfio::File::open(file_path.c_str(), OpenFlags::eRdonly);
			size_t lsize = file.lseek(0, Whence::eEnd);
			if(lsize > UINT32_MAX) throw ShaderModuleReadError("Shader file is too long");
			if(lsize % 4 != 0)     throw ShaderModuleReadError("Misaligned shader file size");
			file.lseek(0, Whence::eSet);
			buffer    = std::make_unique_for_overwrite<uint32_t[]>(lsize / 4);
			size_t rd = posixfio::readAll(file, buffer.get(), lsize);
			if(rd != lsize) throw ShaderModuleReadError("Shader file partially read");
			sm_info.codeSize = uint32_t(lsize);
		} catch(posixfio::Errcode& e) {
			switch(e.errcode) {
				using namespace std::string_literals;
				case ENOENT: throw ShaderModuleReadError("Shader file not found: \""s      + file_path + "\""s); break;
				case EACCES: throw ShaderModuleReadError("Shader file not accessible: \""s + file_path + "\""s); break;
				default: throw e;
			}
		}
		sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		sm_info.pCode = buffer.get();
		VkShaderModule r;
		VK_CHECK(vkCreateShaderModule, dev, &sm_info, nullptr, &r);
		return r;
	}


	void destroyShaderModule(VkDevice dev, VkShaderModule module) {
		vkDestroyShaderModule(dev, module, nullptr);
	}

}
