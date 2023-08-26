#include "engine.hpp"



namespace SKENGINE_NAME_NS {

	std::size_t ShaderRequirementHash::operator()(const ShaderRequirement& req) const noexcept {
		using namespace SKENGINE_NAME_NS_SHORT;
		using ReqType = ShaderRequirement::Type;
		std::size_t r;
		switch(req.type) {
			default:
				std::unreachable();
				abort();
			case ReqType::eWorld:
				r = std::hash<std::string_view>()(req.world.materialName);
				break;
			#warning "Temporarily bad hash for UI shader requirements"
			case ReqType::eUi:
				r = 0xaaaaaaaaaaaaaaaa;
				break;
		}
		return r;
	}


	bool ShaderRequirementCompare::operator()(const ShaderRequirement& l, const ShaderRequirement& r) const noexcept {
		using namespace SKENGINE_NAME_NS_SHORT;
		using ReqType = ShaderRequirement::Type;
		if(l.type != r.type) return false;
		if(l.type == ReqType::eWorld) return l.world.materialName == r.world.materialName;
		if(l.type == ReqType::eUi)    abort();
		std::unreachable();
	}


	std::size_t ShaderModuleSetHash::operator()(const ShaderModuleSet& req) const noexcept {
		using namespace SKENGINE_NAME_NS_SHORT;
		std::size_t hv = std::hash<VkShaderModule>()(req.vertex);
		std::size_t hf = std::hash<VkShaderModule>()(req.fragment);
		return hv ^ std::rotr(hf, 7);
	}


	bool ShaderModuleSetCompare::operator()(const ShaderModuleSet& l, const ShaderModuleSet& r) const noexcept {
		return l.vertex == r.vertex && l.fragment == r.fragment;
	}


	BasicShaderCache::BasicShaderCache(std::string p):
		mPrefix(std::move(p)),
		mSetCache(16),
		mSetLookup(16),
		mModuleCounters(16)
	{ }


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

		{
			auto found = mSetCache.find(sr);
			if(found != mSetCache.end()) {
				++ mModuleCounters[sr];
				return found->second;
			}
		}

		const auto try_get_shader = [&](const std::string& name, const std::string& fallback) {
			try {
				return e.createShaderModuleFromFile(name);
			} catch(ShaderModuleReadError& err) {
				e.logger().error("Recoverable error: {}", err.what());
				return e.createShaderModuleFromFile(fallback);
			}
		};

		switch(sr.type) {
			case ShaderRequirement::Type::eWorld:
				str = std::string(sr.world.materialName);
				r = {
					try_get_shader(mPrefix + "world-" + str + "-vtx.spv", "world-default-vtx.spv"),
					try_get_shader(mPrefix + "world-" + str + "-frg.spv", "world-default-frg.spv") };
				break;
			case ShaderRequirement::Type::eUi:
				throw std::runtime_error("TBW");
				break;
			default: abort();
		}

		mSetCache.insert(SetCache::value_type { sr, r });
		mSetLookup.insert(SetCacheLookup::value_type { r, sr });
		mModuleCounters[sr] = 1;

		return r;
	}


	void BasicShaderCache::shader_cache_releaseModuleSet(
			Engine&          e,
			ShaderModuleSet& ms
	) {
		auto set_req = mSetLookup.find(ms);
		assert(set_req != mSetLookup.end());
		auto counter = mModuleCounters.find(set_req->second);
		assert(counter != mModuleCounters.end());
		assert(counter->second > 0);
		-- counter->second;
		if(counter->second < 1) {
			e.destroyShaderModule(ms.vertex);
			e.destroyShaderModule(ms.fragment);
			mSetCache.erase(set_req->second);
			mSetLookup.erase(ms);
			mModuleCounters.erase(counter);
		}
	}


	void BasicShaderCache::shader_cache_releaseAllModules(Engine& e) {
		for(auto& m : mSetCache) {
			e.destroyShaderModule(m.second.vertex);
			e.destroyShaderModule(m.second.fragment);
		}
		mSetCache.clear();
		mSetLookup.clear();
		mModuleCounters.clear();
	}

}
