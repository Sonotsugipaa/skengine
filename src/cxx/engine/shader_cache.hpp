#pragma once

#include <skengine_fwd.hpp>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <stdexcept>

#include <vulkan/vulkan.h>



namespace SKENGINE_NAME_NS {

	enum class PipelineLayoutId { eImage, eGeometry, e3d };


	class ShaderModuleReadError : public std::runtime_error {
	public:
		template <typename... Args>
		ShaderModuleReadError(Args... args): runtime_error::runtime_error(args...) { }
	};


	struct ShaderModuleSet {
		VkShaderModule vertex;
		VkShaderModule fragment;
	};


	template <typename T>
	concept ShaderCodeContainer = requires(T t) {
		typename T::value_type;
		std::span<const uint32_t>(reinterpret_cast<const uint32_t*>(t.data()), t.size());
		{ t.size() } -> std::convertible_to<uint32_t>;
		{ t.data() } -> std::convertible_to<const void*>;
		requires 4 == sizeof(t.data()[0]);
	};


	struct ShaderRequirement {
		std::string_view name;
		PipelineLayoutId pipelineLayout;
	};


	struct ShaderRequirementHash {
		std::size_t operator()(const ShaderRequirement& req) const noexcept { return std::hash<std::string_view>()(req.name); }
	};

	struct ShaderRequirementCompare {
		bool operator()(const ShaderRequirement& l, const ShaderRequirement& r) const noexcept { return l.name == r.name; }
	};

	struct ShaderModuleSetHash {
		std::size_t operator()(const ShaderModuleSet&) const noexcept;
	};

	struct ShaderModuleSetCompare {
		bool operator()(const ShaderModuleSet&, const ShaderModuleSet&) const noexcept;
	};


	/// \brief Allows on-demand access to shader modules
	///        as desired by the user.
	///
	class ShaderCacheInterface {
	public:
		virtual ShaderModuleSet shader_cache_requestModuleSet(VkDevice, const ShaderRequirement&) = 0;
		virtual void shader_cache_releaseModuleSet  (VkDevice, ShaderModuleSet&) = 0;
		virtual void shader_cache_releaseAllModules (VkDevice) = 0;
	};


	VkShaderModule createShaderModuleFromMemory(VkDevice dev, std::span<const uint32_t> code);


	VkShaderModule createShaderModuleFromFile(VkDevice dev, const std::string& file_path);


	void destroyShaderModule(VkDevice dev, VkShaderModule);

}
