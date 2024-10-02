#include "basic_asset_source.hpp"

#include <posixfio_tl.hpp>



#define BAS_ BasicAssetSource
#define STRV_CAT_(STR_, STRV0_, STRV1_) { STR_.reserve(STRV0_.size() + STRV1_.size()); STR_.append(STRV0_); STR_.append(STRV1_); }



namespace SKENGINE_NAME_NS {

	BasicAssetSource::BasicAssetSource(std::string_view filenamePrefix, Logger logger):
			bas_filenamePrefix(filenamePrefix),
			bas_logger(std::move(logger))
	{ }


	AssetSourceInterface::ModelSource BAS_::asi_requestModelData(std::string_view locator) {
		using posixfio::MemProtFlags;
		using posixfio::MemMapFlags;

		std::string locator_s;
		STRV_CAT_(locator_s, bas_filenamePrefix, locator)

		// Seek an existing mapping
		auto found = bas_mdlMmaps.find(locator_s);
		if(found != bas_mdlMmaps.end()) {
			++ found->second.refCount;
			return { found->second.src };
		}

		// Insert a new mapping
		posixfio::File file;
		try {
			file = posixfio::File::open(locator_s.c_str(), posixfio::OpenFlags::eRdonly);
		} catch(posixfio::FileError& ex) {
			throw ModelLoadError(ex);
		}

		auto len  = file.lseek(0, posixfio::Whence::eEnd);
		auto mmap = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
		auto h    = fmamdl::HeaderView { mmap.get<std::byte>(), mmap.size() };
		return
			bas_mdlMmaps.insert(decltype(bas_mdlMmaps)::value_type(std::move(locator_s), ModelRef {
				std::move(mmap),
				ModelSource { h },
				1 }))
			.first->second.src;
	}


	AssetSourceInterface::MaterialSource BAS_::asi_requestMaterialData(std::string_view locator) {
		using posixfio::MemProtFlags;
		using posixfio::MemMapFlags;

		std::string locator_s;
		STRV_CAT_(locator_s, bas_filenamePrefix, locator);

		// Seek an existing mapping
		auto found = bas_mtlMmaps.find(locator_s);
		if(found != bas_mtlMmaps.end()) {
			++ found->second.refCount;
			return { found->second.src };
		}

		posixfio::File file;
		try {
			file = posixfio::File::open(locator_s.c_str(), posixfio::OpenFlags::eRdonly);
		} catch(posixfio::FileError& ex) {
			bas_logger.error("Material file not found: {}", locator_s);
			std::rethrow_exception(std::current_exception());
		}

		auto len  = file.lseek(0, posixfio::Whence::eEnd);
		auto mmap = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
		auto h    = fmamdl::MaterialView { mmap.get<std::byte>(), mmap.size() };
		auto ins  = bas_mtlMmaps.insert(decltype(bas_mtlMmaps)::value_type(std::move(locator_s), MaterialRef {
			std::move(mmap),
			MaterialSource { h, bas_filenamePrefix },
			1 }));
		return ins.first->second.src;
	}


	void BAS_::asi_releaseModelData(std::string_view locator) noexcept {
		std::string locator_s;
		STRV_CAT_(locator_s, bas_filenamePrefix, locator)

		auto found = bas_mdlMmaps.find(locator_s);
		assert(found != bas_mdlMmaps.end());
		assert(found->second.refCount > 0);
		-- found->second.refCount;
		if(found->second.refCount == 0) {
			bas_mdlMmaps.erase(found);
		}
	}


	void BAS_::asi_releaseMaterialData(std::string_view locator) noexcept {
		std::string locator_s;
		STRV_CAT_(locator_s, bas_filenamePrefix, locator)

		auto found = bas_mtlMmaps.find(locator_s);
		assert(found != bas_mtlMmaps.end());
		assert(found->second.refCount > 0);
		-- found->second.refCount;
		if(found->second.refCount == 0) {
			bas_mtlMmaps.erase(found);
		}
	}

}



#undef STRV_CAT_
#undef BAS_
