#include "basic_asset_cache.hpp"

#include <cassert>

#include <posixfio_tl.hpp>



#define BAC_ BasicAssetCache
#define STRV_CAT_(STR_, STRV0_, STRV1_) { STR_.reserve(STRV0_.size() + STRV1_.size()); STR_.append(STRV0_); STR_.append(STRV1_); }
#define ASSERT_IF_(COND) assert(COND); if(COND) [[likely]]



namespace SKENGINE_NAME_NS {

	BAC_::BAC_(std::string_view filenamePrefix, Logger logger):
			bac_filenamePrefix(filenamePrefix),
			bac_logger(std::move(logger))
	{ }


	AssetCacheInterface::ModelDescription BAC_::aci_requestModelData(ModelId id) {
		using posixfio::MemProtFlags;
		using posixfio::MemMapFlags;

		// Seek an existing ref
		auto found = bac_mdlMmaps.find(id);
		if(found == bac_mdlMmaps.end()) throw UnregisteredModelError(id);
		++ found->second.refCount;

		// If the ref is not already cached, load it
		if(! found->second.data.isValid()) {
			posixfio::File file;
			try {
				file = posixfio::File::open(found->second.src.filename.c_str(), posixfio::OpenFlags::eRdonly);
			} catch(posixfio::Errcode& ex) {
				throw ModelLoadError(ex);
			}

			auto len = file.lseek(0, posixfio::Whence::eEnd);
			found->second.data = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
			found->second.desc = { .fmaHeader = { found->second.data.get<std::byte>(), found->second.data.size() } };

			{ // Map all of the model's materials to its ID
				auto& h = found->second.desc.fmaHeader;
				for(auto& mtl : found->second.desc.fmaHeader.materials()) {
					auto mtlName = h.getStringView(mtl.name);
					std::string mtlFilename;
					mtlFilename.reserve(bac_filenamePrefix.size() + mtlName.size());
					mtlFilename.append(bac_filenamePrefix);
					mtlFilename.append(mtlName);
					MaterialRef newMtl = {
						.src      = { .filename = std::move(mtlFilename) },
						.data     = nullptr,
						.desc     = { },
						.refCount = 0 };
					if(! bac_mtlNameMap.contains(mtlName)) {
						auto newId = bac_mtlIdGen.generate();
						bac_mtlMmaps.insert({ newId, std::move(newMtl) });
						bac_mtlNameMap.insert({ std::string(mtlName), newId });
					}
				}
			}

			assert(found->second.data.isValid());
		}

		return found->second.desc;
	}


	AssetCacheInterface::MaterialDescription BAC_::aci_requestMaterialData(MaterialId id) {
		using posixfio::MemProtFlags;
		using posixfio::MemMapFlags;

		// Seek an existing ref
		auto found = bac_mtlMmaps.find(id);
		if(found == bac_mtlMmaps.end()) throw UnregisteredMaterialError(id);
		++ found->second.refCount;

		// If the ref is not already cached, load it
		if(! found->second.data.isValid()) {
			posixfio::File file;
			try {
				file = posixfio::File::open(found->second.src.filename.c_str(), posixfio::OpenFlags::eRdonly);
			} catch(posixfio::Errcode& ex) {
				throw MaterialLoadError(ex);
			}

			auto len = file.lseek(0, posixfio::Whence::eEnd);
			found->second.data = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
			found->second.desc = {
				.fmaHeader = { found->second.data.get<std::byte>(), found->second.data.size() },
				.texturePathPrefix = bac_filenamePrefix };

			assert(found->second.data.isValid());
		}

		return found->second.desc;
	}


	void BAC_::aci_releaseModelData(ModelId id) noexcept {
		auto found = bac_mdlMmaps.find(id);
		bool isFound = found != bac_mdlMmaps.end();
		ASSERT_IF_(isFound) {
			bool refCountGt0 = found->second.refCount > 0;
			ASSERT_IF_(refCountGt0) {
				-- found->second.refCount;
				if(found->second.refCount == 0) [[unlikely]] {
					found->second.data = nullptr;
				}
			}
		}
	}


	void BAC_::aci_releaseMaterialData(MaterialId id) noexcept {
		auto found = bac_mtlMmaps.find(id);
		bool isFound = found != bac_mtlMmaps.end();
		ASSERT_IF_(isFound) {
			bool refCountGt0 = found->second.refCount > 0;
			ASSERT_IF_(refCountGt0) {
				-- found->second.refCount;
				if(found->second.refCount == 0) [[unlikely]] {
					found->second.data = nullptr;
				}
			}
		}
	}


	MaterialId BAC_::aci_materialIdFromName(std::string_view name) {
		auto found = bac_mtlNameMap.find(name);
		bool notFound = found == bac_mtlNameMap.end();
		assert(! notFound);
		if(notFound) [[unlikely]] throw std::runtime_error(fmt::format("Request for bad naterial name \"{}\"", name));
		return found->second;
	}


	ModelId BAC_::setModelFromFile(std::string_view filename) {
		std::string mdlFilename;
		mdlFilename.reserve(bac_filenamePrefix.size() + filename.size());
		mdlFilename.append(bac_filenamePrefix);
		mdlFilename.append(filename);
		ModelRef newMdl = {
			.src      = { .filename = std::move(mdlFilename) },
			.data     = nullptr,
			.desc     = { },
			.refCount = 0 };
		auto newId = bac_mdlIdGen.generate();
		bac_mdlMmaps.insert({ newId, std::move(newMdl) });
		bac_logger.info("Associated model {} with file \"{}{}\"", model_id_e(newId), bac_filenamePrefix, filename);
		return newId;
	}

	void BAC_::unsetModel(ModelId id) {
		auto found = bac_mdlMmaps.find(id);
		bool notFound = found == bac_mdlMmaps.end();
		assert(! notFound);
		if(notFound) [[unlikely]] throw UnregisteredModelError(id);
		bool refCountIsZero = found->second.refCount == 0;
		assert(refCountIsZero);
		if(! refCountIsZero) [[unlikely]] {
			bac_logger.warn("Trying to forget model {} with references in use", model_id_e(id));
			bac_logger.warn(" (This will probably cause a memory leak if the application");
			bac_logger.warn(" assumes that the model has been forgotten)");
		} else {
			bac_mdlMmaps.erase(id);
			bac_mdlIdGen.recycle(id);
		}
	}


	MaterialId BAC_::setMaterialFromFile(std::string_view filename, std::string name) {
		std::string mtlFilename;
		mtlFilename.reserve(bac_filenamePrefix.size() + filename.size());
		mtlFilename.append(bac_filenamePrefix);
		mtlFilename.append(filename);
		MaterialRef newMtl = {
			.src      = { .filename = std::move(mtlFilename) },
			.data     = nullptr,
			.desc     = { },
			.refCount = 0 };
		auto newId = bac_mtlIdGen.generate();
		bac_mtlMmaps.insert({ newId, std::move(newMtl) });
		bac_mtlNameMap.insert({ std::move(name), newId });
		bac_logger.info("Associated material {} with file \"{}{}\"", material_id_e(newId), bac_filenamePrefix, filename);
		return newId;
	}

	void BAC_::unsetMaterial(MaterialId id) {
		auto found = bac_mtlMmaps.find(id);
		bool notFound = found == bac_mtlMmaps.end();
		assert(! notFound);
		if(notFound) [[unlikely]] throw UnregisteredMaterialError(id);
		bool refCountIsZero = found->second.refCount == 0;
		assert(refCountIsZero);
		if(! refCountIsZero) [[unlikely]] {
			bac_logger.warn("Trying to forget material {} with references in use", material_id_e(id));
			bac_logger.warn(" (This will probably cause a memory leak if the application");
			bac_logger.warn(" assumes that the material has been forgotten)");
		} else {
			bac_mtlMmaps.erase(id);
			bac_mtlIdGen.recycle(id);
		}
	}

}



#undef ASSERT_IF_
#undef STRV_CAT_
#undef BAC_
