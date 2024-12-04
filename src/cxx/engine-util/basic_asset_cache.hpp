#pragma once

#include "world_renderer.hpp"

#include <unordered_map>
#include <utility>

#include <idgen.hpp>

#include <posixfio.hpp>



namespace SKENGINE_NAME_NS {

	class AssetFileError : public posixfio::Errcode {
	public:
		template <typename... Args>
		AssetFileError(Args&&... args): posixfio::Errcode::Errcode(std::forward<Args>(args)...) { }
	};

	#define DEFINE_ERROR_(NAME_, BASE_) class NAME_ : public BASE_ { public: template <typename... Args> NAME_(Args&&... args): BASE_::BASE_(std::forward<Args>(args)...) { } }
		DEFINE_ERROR_(ModelLoadError,    AssetFileError);
		DEFINE_ERROR_(MaterialLoadError, AssetFileError);
	#undef DEFINE_ERROR_


	class AssetCacheError {
	public:
		union AssetId { ModelId model; MaterialId material; };

		AssetCacheError(ModelId    mdl): ace_assetId { .model    = mdl } { }
		AssetCacheError(MaterialId mtl): ace_assetId { .material = mtl } { }

		constexpr AssetId id() const noexcept { return ace_assetId; }

	protected:
		AssetId ace_assetId;
	};

	class UnregisteredModelError : public AssetCacheError {
	public:
		UnregisteredModelError(ModelId mdl): AssetCacheError(mdl) { }
		constexpr ModelId id() const noexcept { return AssetCacheError::id().model; }
	};

	class UnregisteredMaterialError : public AssetCacheError {
	public:
		UnregisteredMaterialError(MaterialId mtl): AssetCacheError(mtl) { }
		constexpr MaterialId id() const noexcept { return AssetCacheError::id().material; }
	};


	struct BasicAssetCacheData {
		union {
			posixfio::MemMapping*         mmapData;
			std::pair<std::byte*, size_t> localData;
		};
		bool isLocal;

		BasicAssetCacheData(nullptr_t = nullptr): localData(nullptr, 0), isLocal(true) { }
		BasicAssetCacheData(posixfio::MemMapping mmap): mmapData(new posixfio::MemMapping(std::move(mmap))), isLocal(false) { }
		static BasicAssetCacheData allocate(size_t size) { BasicAssetCacheData r; r.localData = { new std::byte[size], size }; r.isLocal = true; return r; }

		BasicAssetCacheData(BasicAssetCacheData&& mv) { isLocal = mv.isLocal; if(isLocal) mmapData = mv.mmapData; else localData = mv.localData; new (&mv) BasicAssetCacheData(nullptr); }
		auto& operator=(BasicAssetCacheData&& mv) noexcept { this->~BasicAssetCacheData(); return * new (this) BasicAssetCacheData(std::move(mv)); }

		~BasicAssetCacheData() {
			if(isLocal) { if(localData.first != nullptr) delete[] localData.first; }
			else        { delete mmapData; }
			new (this) BasicAssetCacheData(nullptr);
		}

		template <typename T> constexpr auto* get(this auto& self) noexcept { return self.isLocal? self.localData.first : self.mmapData->template get<T>(); }
		constexpr auto size() const noexcept { return isLocal? localData.second : mmapData->size(); }

		constexpr bool isValid() const noexcept { return ! (isLocal && (localData.first == nullptr)); }
	};


	class BasicAssetCache : public AssetCacheInterface {
	public:
		struct GenericStrHash {
			using is_transparent = void;
			constexpr size_t operator()(const std::string& v) const noexcept { return std::hash<std::string>{}(v); }
			constexpr size_t operator()(std::string_view   v) const noexcept { return std::hash<std::string_view>{}(v); }
			constexpr size_t operator()(const char*        v) const noexcept { return std::hash<std::string_view>{}(v); }
		};

		struct GenericStrEq {
			using is_transparent = void;
			template <typename L, typename R>
			constexpr bool operator()(L&& l, R&& r) const noexcept { return std::equal_to<std::string_view>()(std::string_view(std::forward<L>(l)), std::string_view(std::forward<R>(r))); }
		};

		BasicAssetCache(std::string_view filenamePrefix, Logger);

		ModelDescription aci_requestModelData(ModelId) override;
		MaterialDescription aci_requestMaterialData(MaterialId) override;
		void aci_releaseModelData(ModelId) noexcept override;
		void aci_releaseMaterialData(MaterialId) noexcept override;
		MaterialId aci_materialIdFromName(std::string_view) override;

		ModelId setModelFromFile(std::string_view filename);
		void unsetModel(ModelId);

		MaterialId setMaterialFromFile(std::string_view filename, std::string name);
		void unsetMaterial(MaterialId);

		Logger& logger(this auto& self) { return self.bac_logger; }

	private:
		struct Source {
			std::string filename;
		};

		struct ModelRef {
			Source src;
			BasicAssetCacheData data;
			ModelDescription desc;
			unsigned refCount;
		};

		struct MaterialRef {
			Source src;
			BasicAssetCacheData data;
			MaterialDescription desc;
			unsigned refCount;
		};

		std::string bac_filenamePrefix;
		Logger bac_logger;
		idgen::IdGenerator<ModelId>    bac_mdlIdGen;
		idgen::IdGenerator<MaterialId> bac_mtlIdGen;
		std::unordered_map<ModelId,    ModelRef>    bac_mdlMmaps;
		std::unordered_map<MaterialId, MaterialRef> bac_mtlMmaps;
		std::unordered_map<std::string, MaterialId, GenericStrHash, GenericStrEq> bac_mtlNameMap; // Maps a mesh's declared material name to a material ref
	};

}
