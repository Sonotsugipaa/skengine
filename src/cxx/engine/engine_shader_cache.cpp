#include "engine.hpp"



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
		ShaderModuleSet r = { .vertex = nullptr, .fragment = nullptr };

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

		constexpr auto combineStr = [](const std::string& pfx, PipelineLayoutId pl, std::string_view nm, std::string_view sfx) {
			constexpr std::string_view plStr[2] = { "unkn-", "3d-" };
			std::string r;
			size_t plIndex;
			switch(pl) {
				default: plIndex = 0; break;
				case PipelineLayoutId::e3d: plIndex = 1; break;
			}
			r.reserve(pfx.size() + plStr[plIndex].size() + nm.size() + sfx.size());
			r.append(pfx);
			r.append(plStr[plIndex]);
			r.append(nm);
			r.append(sfx);
			return r;
		};

		try {
			r = {
				try_get_shader(combineStr(mPrefix, sr.pipelineLayout, sr.name, "-vtx.spv"), combineStr("", sr.pipelineLayout, "default", "-vtx.spv")),
				try_get_shader(combineStr(mPrefix, sr.pipelineLayout, sr.name, "-frg.spv"), combineStr("", sr.pipelineLayout, "default", "-frg.spv")) };
			mSetCache.insert(SetCache::value_type { sr, r });
			mSetLookup.insert(SetCacheLookup::value_type { r, sr });
			mModuleCounters[sr] = 1;
		} catch(...) {
			if(r.vertex)   e.destroyShaderModule(r.vertex);
			if(r.fragment) e.destroyShaderModule(r.fragment);
			std::rethrow_exception(std::current_exception());
		}

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
