#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <vulkan/vulkan.h>



namespace SKENGINE_NAME_NS {

	class Engine; // Defined in `engine.hpp`


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


	struct WorldShaderRequirement {
		std::string_view materialName;
	};

	struct UiShaderRequirement {
		// TBW
	};

	struct ShaderRequirement {
		enum class Type {
			eWorld, eUi
		};

		union {
			WorldShaderRequirement world;
			UiShaderRequirement    ui;
		};

		Type type;
	};


	struct ShaderRequirementHash {
		std::size_t operator()(const ShaderRequirement&) const noexcept;
	};

	struct ShaderRequirementCompare {
		bool operator()(const ShaderRequirement&, const ShaderRequirement&) const noexcept;
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
	/// When a ShaderCacheInterface virtual function is called
	/// by the Engine, the latter is guaranteed to be able to load
	/// modules via `Engine::createShaderModuleFromFile` and
	/// `Engine::createShaderModuleFromMemory`, and said Engine
	/// is given as the first argument; although it is not necessarily
	/// fully constructed, and only those two functions are guaranteed
	/// to be accessible.
	///
	/// Before the ShaderCacheInterface is destroyed,
	/// `shader_cache_releaseAllModules` is always called, and no other
	/// function is called afterwards.
	///
	/// Furthermore, it may be assumed that the Engine will never
	/// try to release a module that was never given by the
	/// ShaderCacheInterface, and no module is released more
	/// (nor fewer) times than it is acquired.
	///
	class ShaderCacheInterface {
	public:
		virtual ShaderModuleSet shader_cache_requestModuleSet(Engine&, const ShaderRequirement&) = 0;
		virtual void shader_cache_releaseModuleSet  (Engine&, ShaderModuleSet&) = 0;
		virtual void shader_cache_releaseAllModules (Engine&) = 0;
	};


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
		BasicShaderCache(std::string path_prefix);

		#ifndef NDEBUG
			// The non-default default constructor isn't necessary,
			// and only used for runtime assertions
			~BasicShaderCache();
		#endif

		ShaderModuleSet shader_cache_requestModuleSet(Engine&, const ShaderRequirement&) override;
		void shader_cache_releaseModuleSet  (Engine&, ShaderModuleSet&) override;
		void shader_cache_releaseAllModules (Engine&) override;

	private:
		using SetCache       = std::unordered_map<ShaderRequirement, ShaderModuleSet, ShaderRequirementHash, ShaderRequirementCompare>;
		using SetCacheLookup = std::unordered_map<ShaderModuleSet, ShaderRequirement, ShaderModuleSetHash, ShaderModuleSetCompare>;
		using Counters       = std::unordered_map<ShaderRequirement, size_t, ShaderRequirementHash, ShaderRequirementCompare>;
		std::string    mPrefix;
		SetCache       mSetCache;
		SetCacheLookup mSetLookup;
		Counters       mModuleCounters;
	};

}
