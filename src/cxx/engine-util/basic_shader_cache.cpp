#include "basic_shader_cache.hpp"

#include <cassert>



namespace SKENGINE_NAME_NS {

	BasicShaderCache::BasicShaderCache(std::string p, Logger logger):
		mLogger(std::move(logger)),
		mPrefix(std::move(p)),
		mSetCache(16),
		mSetLookup(16),
		mModuleCounters(16)
	{ }


	BasicShaderCache::~BasicShaderCache() {
		assert(mModuleCounters.empty());
	}


	ShaderModuleSet BasicShaderCache::shader_cache_requestModuleSet(VkDevice dev, const ShaderRequirement& sr) {
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
				return createShaderModuleFromFile(dev, name);
			} catch(ShaderModuleReadError& err) {
				mLogger.error("{}", err.what());
				return createShaderModuleFromFile(dev, fallback);
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
				try_get_shader(combineStr(mPrefix, sr.pipelineLayout, sr.name, "-vtx.spv"), combineStr(mPrefix, sr.pipelineLayout, "default", "-vtx.spv")),
				try_get_shader(combineStr(mPrefix, sr.pipelineLayout, sr.name, "-frg.spv"), combineStr(mPrefix, sr.pipelineLayout, "default", "-frg.spv")) };
			mSetCache.insert(SetCache::value_type { sr, r });
			mSetLookup.insert(SetCacheLookup::value_type { r, sr });
			mModuleCounters[sr] = 1;
		} catch(...) {
			if(r.vertex)   destroyShaderModule(dev, r.vertex);
			if(r.fragment) destroyShaderModule(dev, r.fragment);
			std::rethrow_exception(std::current_exception());
		}

		return r;
	}


	void BasicShaderCache::shader_cache_releaseModuleSet(VkDevice dev, ShaderModuleSet& ms) {
		auto set_req = mSetLookup.find(ms);
		assert(set_req != mSetLookup.end());
		auto counter = mModuleCounters.find(set_req->second);
		assert(counter != mModuleCounters.end());
		assert(counter->second > 0);
		-- counter->second;
		if(counter->second < 1) {
			destroyShaderModule(dev, ms.vertex);
			destroyShaderModule(dev, ms.fragment);
			mSetCache.erase(set_req->second);
			mSetLookup.erase(ms);
			mModuleCounters.erase(counter);
		}
	}


	void BasicShaderCache::shader_cache_releaseAllModules(VkDevice dev) {
		for(auto& m : mSetCache) {
			destroyShaderModule(dev, m.second.vertex);
			destroyShaderModule(dev, m.second.fragment);
		}
		mSetCache.clear();
		mSetLookup.clear();
		mModuleCounters.clear();
	}

}
