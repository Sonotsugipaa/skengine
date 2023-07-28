#include "engine.hpp"

#include <spdlog/spdlog.h>



namespace SKENGINE_NAME_NS {

	#ifndef NDEBUG
		BasicShaderCache::~BasicShaderCache() {
			assert(mModuleCounters.empty());
		}
	#endif


	ShaderModuleSet BasicShaderCache::shader_cache_requestModuleSet(
			Engine&                  e,
			const ShaderRequirement& sr
	) {
		ShaderModuleSet r;
		std::string str;

		const auto try_get_shader = [&](const std::string& name, const std::string& fallback) {
			try {
				return e.createShaderModuleFromFile(name);
			} catch(ShaderModuleReadError& err) {
				spdlog::error("Recoverable error: {}", err.what());
				return e.createShaderModuleFromFile(fallback);
			}
		};

		switch(sr.type) {
			case ShaderRequirement::Type::eWorld:
				str = std::string(sr.world.materialName);
				r = {
					try_get_shader(str + "-vtx.spv", "world-default-vtx.spv"),
					try_get_shader(str + "-frg.spv", "world-default-frg.spv") };
				break;
			case ShaderRequirement::Type::eUi:
				throw std::runtime_error("TBW");
				break;
			default: abort();
		}

		++ mModuleCounters[r.vertex];
		++ mModuleCounters[r.fragment];

		return r;
	}


	void BasicShaderCache::shader_cache_releaseModuleSet(
			Engine&          e,
			ShaderModuleSet& ms
	) {
		const auto rm = [&](VkShaderModule m) {
			auto found = mModuleCounters.find(m);
			assert(found != mModuleCounters.end());
			assert(found->second > 0);
			-- found->second;
			if(found->second < 1) {
				e.destroyShaderModule(m);
				mModuleCounters.erase(found);
			}
		};

		rm(ms.vertex);
		rm(ms.fragment);
	}


	void BasicShaderCache::shader_cache_releaseAllModules(Engine& e) {
		for(auto& m : mModuleCounters) {
			e.destroyShaderModule(m.first);
		}
		mModuleCounters.clear();
	}

}
