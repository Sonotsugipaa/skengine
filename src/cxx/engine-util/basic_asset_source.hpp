#pragma once

#include <engine/world_renderer.hpp>

#include <unordered_map>

#include <posixfio.hpp>



namespace SKENGINE_NAME_NS {

	class AssetSourceError : public posixfio::FileError {
	public:
		template <typename... Args>
		AssetSourceError(Args... args): posixfio::FileError::FileError(args...) { }
	};

	#define DEFINE_ERROR_(NAME_, BASE_) class NAME_ : public BASE_ { public: template <typename... Args> NAME_(Args... args): BASE_::BASE_(args...) { } }

		DEFINE_ERROR_(ModelLoadError,    AssetSourceError);
		DEFINE_ERROR_(MaterialLoadError, AssetSourceError);

	#undef DEFINE_ERROR_


	class BasicAssetSource : public AssetSourceInterface {
	public:
		BasicAssetSource(std::string_view filenamePrefix, Logger);

		ModelSource asi_requestModelData(std::string_view locator) override;
		MaterialSource asi_requestMaterialData(std::string_view locator) override;
		void asi_releaseModelData(std::string_view locator) noexcept override;
		void asi_releaseMaterialData(std::string_view locator) noexcept override;

		Logger& logger(this auto& self) { return self.bas_logger; }

	private:
		struct ModelRef {
			posixfio::MemMapping mmap;
			ModelSource src;
			unsigned refCount;
		};

		struct MaterialRef {
			posixfio::MemMapping mmap;
			MaterialSource src;
			unsigned refCount;
		};

		std::string bas_filenamePrefix;
		Logger bas_logger;
		std::unordered_map<std::string, ModelRef>    bas_mdlMmaps;
		std::unordered_map<std::string, MaterialRef> bas_mtlMmaps;
	};

}
