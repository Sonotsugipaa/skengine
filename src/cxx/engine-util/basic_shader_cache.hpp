#pragma once

#include <skengine_fwd.hpp>

#include <engine/shader_cache.hpp>
#include <engine/types.hpp>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <vulkan/vulkan.h>



namespace SKENGINE_NAME_NS {

	/// \brief Basic implementation of a ShaderCacheInterface.
	///
	/// A BasicShaderCache attempts to read shaders from files that follow
	/// the pattern "[type]-[name]-[stage].spv", or fallback to
	/// "[type]-default-[stage].spv" if the requested shader isn't available.
	///
	/// All files are looked for in the current working directory.
	///
	/// For example, if the engine requests a world shader for a material
	/// called "MATERIAL_0", the BasicShaderCache will attempt to read
	/// "world-MATERIAL_0-vtx.spv" and "world-MATERIAL_0-frg.spv".
	///
	class BasicShaderCache : public ShaderCacheInterface {
	public:
		BasicShaderCache(std::string path_prefix, Logger logger);
		~BasicShaderCache();

		ShaderModuleSet shader_cache_requestModuleSet(VkDevice, const ShaderRequirement&) override;
		void shader_cache_releaseModuleSet  (VkDevice, ShaderModuleSet&) override;
		void shader_cache_releaseAllModules (VkDevice) override;

	private:
		using SetCache       = std::unordered_map<ShaderRequirement, ShaderModuleSet, ShaderRequirementHash, ShaderRequirementCompare>;
		using SetCacheLookup = std::unordered_map<ShaderModuleSet, ShaderRequirement, ShaderModuleSetHash, ShaderModuleSetCompare>;
		using Counters       = std::unordered_map<ShaderRequirement, size_t, ShaderRequirementHash, ShaderRequirementCompare>;
		Logger         mLogger;
		std::string    mPrefix;
		SetCache       mSetCache;
		SetCacheLookup mSetLookup;
		Counters       mModuleCounters;
	};

}
