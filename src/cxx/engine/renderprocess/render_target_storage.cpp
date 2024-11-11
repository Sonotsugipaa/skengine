#include "_render_process.inl.hpp"

#include <cassert>

#include <vk-util/error.hpp>

#include <vulkan/vulkan_format_traits.hpp>



namespace SKENGINE_NAME_NS {

	namespace {

		void initRtarget(
			RenderTarget& dstRtarget,
			VmaAllocator vma,
			VkDevice dev,
			const RenderTargetDescription& desc
		) {
			vkutil::ImageCreateInfo icInfo = { }; {
				icInfo.type = VK_IMAGE_TYPE_2D;
				icInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				icInfo.samples = VK_SAMPLE_COUNT_1_BIT;
				icInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
				icInfo.arrayLayers = 1;
				icInfo.mipLevels = 1;
			}
			vkutil::AllocationCreateInfo acInfo = { }; {
				acInfo.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
				acInfo.vmaFlags = 0;
			}
			VkImageViewCreateInfo ivcInfo = { }; {
				#define SWID_ VK_COMPONENT_SWIZZLE_IDENTITY
				ivcInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				ivcInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				ivcInfo.components = { SWID_, SWID_, SWID_, SWID_ };
				ivcInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ivcInfo.subresourceRange.levelCount = 1;
				ivcInfo.subresourceRange.layerCount = 1;
				#undef SWID_
			}
			vkutil::BufferCreateInfo bcInfo;
			bool hostAccess = desc.hostReadable || desc.hostWriteable;
			icInfo.usage = desc.usage;
			icInfo.extent = { desc.extent.width, desc.extent.height, 1 };
			icInfo.format = desc.format;
			acInfo.preferredMemFlags = (! hostAccess)? 0 : VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
			ivcInfo.format = icInfo.format;
			bcInfo.size =
				icInfo.extent.width *
				icInfo.extent.height *
				icInfo.extent.depth *
				vk::blockSize(vk::Format(icInfo.format));
			if(hostAccess) acInfo.vmaFlags = desc.hostAccessSequential?
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT :
				VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			bcInfo.usage =
				(desc.hostWriteable * VK_BUFFER_USAGE_TRANSFER_SRC_BIT) |
				(desc.hostReadable  * VK_BUFFER_USAGE_TRANSFER_DST_BIT);
			dstRtarget.devImage = vkutil::ManagedImage::create(vma, icInfo, acInfo);
			bool devImageIsHostVisible = 0 != (dstRtarget.devImage.info().memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			try {
				if(hostAccess && ! devImageIsHostVisible) {
					dstRtarget.hostBuffer = vkutil::ManagedBuffer::createStagingBuffer(vma, bcInfo);
				}
				ivcInfo.image = dstRtarget.devImage;
				if(desc.requiresImageView) {
					try {
						VK_CHECK(vkCreateImageView, dev, &ivcInfo, nullptr, &dstRtarget.devImageView);
					} catch(...) {
						if(hostAccess && ! devImageIsHostVisible) vkutil::ManagedBuffer::destroy(vma, dstRtarget.hostBuffer);
						std::rethrow_exception(std::current_exception());
					}
				} else {
					dstRtarget.devImageView = nullptr;
				}
			} catch(...) {
				vkutil::ManagedImage::destroy(vma, dstRtarget.devImage);
				std::rethrow_exception(std::current_exception());
			}
		}


		void destroyRtarget(
			RenderTarget& rtarget,
			VmaAllocator vma,
			VkDevice dev,
			const RenderTargetDescription& desc
		) {
			auto memVisibleBit = desc.hostReadable || desc.hostWriteable;
			assert((! memVisibleBit) || (rtarget.hostBuffer.value != nullptr && rtarget.hostBuffer.alloc != nullptr));
			assert(rtarget.devImageView != nullptr);
			assert(rtarget.devImage.value != nullptr && rtarget.devImage.alloc != nullptr);
			if(memVisibleBit) vkutil::ManagedBuffer::destroy(vma, rtarget.hostBuffer);
			if(desc.requiresImageView) vkDestroyImageView(dev, rtarget.devImageView, nullptr);
			else assert(rtarget.devImageView == nullptr);
			vkutil::ManagedImage::destroy(vma, rtarget.devImage);
		}

	}



	RenderTargetStorage::RenderTargetStorage(const RenderTargetStorage& cp):
		rts_logger(cp.rts_logger),
		rts_vma(cp.rts_vma),
		rts_descs(cp.rts_descs),
		rts_entries(cp.rts_entries),
		rts_map(cp.rts_map),
		rts_gframeCount(cp.rts_gframeCount)
	{
		#ifndef NDEBUG
		for(auto& mapping : rts_map) {
			auto isManagedImageInitialized = [&](size_t i) { return (! mapping.second.isExternal) && rts_entries[mapping.second.offset + i].managed; };
			for(size_t i = 0; i < rts_gframeCount; ++i) {
				assert((! isManagedImageInitialized(i)) && "initialized managed images in this copy-ctor WILL cause memory leaks");
			}
		}
		#endif
	}


	RenderTargetStorage::RenderTargetStorage(RenderTargetStorage&& mv):
		rts_logger(std::move(mv.rts_logger)),
		rts_vma(std::move(mv.rts_vma)),
		rts_descs(std::move(mv.rts_descs)),
		rts_entries(std::move(mv.rts_entries)),
		rts_map(std::move(mv.rts_map)),
		rts_gframeCount(std::move(mv.rts_gframeCount))
	{
		mv.rts_map.clear();
	}


	RenderTargetStorage::~RenderTargetStorage() {
		if(rts_vma == nullptr) return;

		constexpr auto getVkDev = [](VmaAllocator vma) { VmaAllocatorInfo r; vmaGetAllocatorInfo(vma, &r); return r.device; };
		auto dev = getVkDev(rts_vma);
		for(auto& set : rts_map) {
			if(! set.second.isExternal) {
				size_t offset0 = set.second.offset;
				assert(offset0 % rts_gframeCount == 0);
				for(size_t offset1 = 0; offset1 < rts_gframeCount; ++ offset1) {
					auto offset = offset0 + offset1;
					auto& entry = rts_entries[offset].managed;
					auto& desc = rts_descs[idToIndex(set.first)];
					destroyRtarget(entry, rts_vma, dev, desc);
					#ifndef NDEBUG
						memset(rts_entries.data() + offset, 0, sizeof(Entry));
					#endif
				}
			}
		}
	}


	void RenderTargetStorage::setRtargetExtent(RenderTargetId id, const VkExtent3D& newExtent) {
		assert(rts_gframeCount > 0 /* should always be true, but it's an important assumption here */);
		auto  entrySetIter = rts_map.find(id);
		assert(entrySetIter != rts_map.end());
		#ifdef NDEBUG
			// The logic error is anything but unrecoverable; still, we can't resize an externally managed rtarget
			if(entrySetIter == rts_map.end()) {
				rts_logger.error("Attempting to resize externally managed rtarget {}", render_target_id_e(id));
				return;
			}
		#endif
		auto& entrySet = * entrySetIter;
		assert(! entrySet.second.isExternal);
		auto* baseEntry = rts_entries.data() + entrySet.second.offset;
		auto& baseExtent = baseEntry->managed.devImage.info().extent;
		auto& desc = rts_descs[entrySet.second.offset / rts_gframeCount];  assert(entrySet.second.offset % rts_gframeCount == 0);
		if(
			(newExtent.width  != baseExtent.width ) ||
			(newExtent.height != baseExtent.height) ||
			(newExtent.depth  != baseExtent.depth )
		) {
			auto& log = rts_logger;
			auto* dev = [](auto vma) { VmaAllocatorInfo r; vmaGetAllocatorInfo(vma, &r); return r.device; } (rts_vma);
			log.trace("Resizing managed entries [{}, {}) of rtarget ID {}: {}x{}x{} -> {}x{}x{}",
				auto(entrySet.second.offset),
				entrySet.second.offset + rts_gframeCount,
				render_target_id_e(id),
				baseExtent.width, baseExtent.height, baseExtent.depth,
				newExtent.width, newExtent.height, newExtent.depth );
			desc.extent = newExtent;
			for(size_t i = 0; i < rts_gframeCount; ++i) {
				volatile size_t baseEntryIdx = std::distance(rts_entries.data(), &baseEntry[i]); (void) baseEntryIdx;
				auto& rtarget = baseEntry[i].managed;
				#define ARG_LIST_ rtarget, rts_vma, dev, desc
				destroyRtarget(ARG_LIST_);
				initRtarget(ARG_LIST_);
				#undef ARG_LIST_
			}
		}
	}


	void RenderTargetStorage::setGframeCount(size_t newGframeCount) {
		if(rts_gframeCount == newGframeCount) return;
		auto& log = rts_logger;
		auto* dev = [](auto vma) { VmaAllocatorInfo r; vmaGetAllocatorInfo(vma, &r); return r.device; } (rts_vma);
		auto swapOut = decltype(rts_entries)(rts_descs.size() * newGframeCount, Entry { });
		size_t copyCount = std::min(rts_gframeCount, newGframeCount);
		size_t destroyCount = rts_gframeCount - copyCount;
		size_t createCount = newGframeCount - copyCount;
		assert((destroyCount > 0) != (createCount > 0));
		for(size_t descIdx = 0; descIdx < rts_descs.size(); ++ descIdx) {
			auto&  entrySet  = rts_map.at(idFromIndex<RenderTargetId>(descIdx));
			auto&  desc      = rts_descs[descIdx];
			size_t oldOffset = descIdx * rts_gframeCount;
			size_t newOffset = descIdx * newGframeCount;
			assert(oldOffset == entrySet.offset);
			assert((entrySet.isExternal == 1) == bool(desc.externalImages));
			#define ARG_LIST_ rts_vma, dev, desc
			auto copyEntry = [&](size_t gframe) {
				size_t srcIdx = oldOffset + gframe, dstIdx = newOffset + gframe;
				log.trace("Copying rtarget entry {} -> {}", srcIdx, dstIdx);
				swapOut[dstIdx] = rts_entries[srcIdx];
			};
			auto destroyEntry = [&](size_t gframe) {
				size_t idx = oldOffset + copyCount + gframe;
				assert(idx < rts_entries.size());
				log.trace("Destroying rtarget entry {}", idx);
				destroyRtarget(rts_entries[idx].managed, ARG_LIST_);
			};
			auto initEntry = [&](size_t gframe) {
				size_t idx = newOffset + copyCount + gframe;
				assert(idx < swapOut.size());
				log.trace("Creating rtarget entry {}", idx);
				initRtarget(swapOut[idx].managed, ARG_LIST_);
			};
			for(size_t i = 0; i < copyCount; ++i) copyEntry(i);
			if(entrySet.isExternal) {
				if(destroyCount > 0) log.trace("Not destroying"" external rtarget entries [{}, {})", oldOffset + copyCount, oldOffset + copyCount + destroyCount);
				else                 log.trace("Not initializing external rtarget entries [{}, {})", newOffset + copyCount, newOffset + copyCount + createCount);
			} else {
				for(size_t i = 0; i < destroyCount; ++i) destroyEntry(i);
				for(size_t i = 0; i < createCount ; ++i) initEntry(i);
			}
			#undef ARG_LIST_
			entrySet.offset = newOffset;
		}

		rts_entries = std::move(swapOut);
		rts_gframeCount = newGframeCount;
	}


	void RenderTargetStorage::updateRtargetReferences() {
		auto& log = rts_logger;

		{ // Create rtarget images
			for(auto& mapping : rts_map) {
				auto& entrySet = mapping.second;
				auto  descIdx  = entrySet.offset / rts_gframeCount;
				if(entrySet.isExternal) {
					log.trace("Updating external entries [{}, {}) matching ID {}", auto(entrySet.offset), entrySet.offset + rts_gframeCount, descIdx);
					auto& desc = rts_descs[descIdx];
					auto& extImages = * desc.externalImages;
					assert(extImages.size() >= rts_gframeCount);
					for(size_t i = 0; i < rts_gframeCount; ++i) {
						rts_entries[entrySet.offset + i].external = {
							.image = extImages[i].image,
							.imageView = extImages[i].imageView,
							.extent = desc.extent,
							.format = desc.format };
					}
				}
			}
		}
	}


	RenderTargetStorage::EntryRange RenderTargetStorage::getEntrySet(RenderTargetId id) const & {
		auto set = rts_map.at(id);
		auto begin = rts_entries.data() + set.offset;
		assert(set.offset % rts_gframeCount == 0);
		return EntryRange {
			.entries = decltype(EntryRange::entries)(begin, begin + rts_gframeCount),
			.isExternal = set.isExternal == 1 };
	}

	const RenderTargetDescription& RenderTargetStorage::getDescription(RenderTargetId id) const & {
		auto descIdx = idToIndex(id);
		#ifdef NDEBUG
			return rts_descs.at(descIdx);
		#else
			assert(descIdx < std::make_signed_t<decltype(rts_descs.size())>(rts_descs.size()));
			return rts_descs[descIdx];
		#endif
	}


	RenderTargetStorage::Factory::Factory(Logger mvLogger, size_t gframeCount):
		dst()
	{
		dst.rts_logger = std::move(mvLogger);
		dst.rts_gframeCount = gframeCount;
	}


	RenderTargetId RenderTargetStorage::Factory::setRenderTarget(RenderTargetDescription&& desc) {
		assert(dst.rts_entries.size() == dst.rts_descs.size() * dst.rts_gframeCount);
		assert(dst.rts_descs.size() == dst.rts_map.size());
		bool isExternal = bool(desc.externalImages);
		auto id = idFromIndex<RenderTargetId>(dst.rts_map.size());
		auto baseIndex = dst.rts_entries.size();
		dst.rts_entries.resize(dst.rts_entries.size() + dst.rts_gframeCount);
		auto base = dst.rts_entries.begin() + baseIndex;
		if(isExternal) {
			assert(desc.externalImages->size() >= dst.rts_gframeCount);
			auto& extImages = *desc.externalImages;
			for(size_t i = 0; i < dst.rts_gframeCount; ++i) {
				base[i].external = {
					.image = extImages[i].image,
					.imageView = extImages[i].imageView,
					.extent = desc.extent,
					.format = desc.format };
			}
		} else {
			for(size_t i = 0; i < dst.rts_gframeCount; ++i) {
				base[i].managed = { }; // zero-init so that destroying an incomplete RTS doesn't cause UB nor a fatal error
			}
		}
		dst.rts_descs.emplace_back(std::move(desc));
		dst.rts_map.insert(std::pair(
			id,
			EntrySet {
				.offset = baseIndex,
				.isExternal = decltype(EntrySet::isExternal)(isExternal? 1 : 0) } ));
		return id;
	}


	RenderTargetStorage RenderTargetStorage::Factory::finalize(VmaAllocator vma) && {
		auto& log = dst.rts_logger;
		auto* dev = [](auto vma) { VmaAllocatorInfo r; vmaGetAllocatorInfo(vma, &r); return r.device; } (vma);

		{ // Create rtarget images
			log.trace("render_target_storage: populating (<= {}) set{} of {} rtarget image{} each", dst.rts_descs.size(), (dst.rts_descs.size() == 1)? "":"s", dst.rts_gframeCount, (dst.rts_gframeCount == 1)? "":"s");
			for(auto& mapping : dst.rts_map) {
				auto& entrySet = mapping.second;
				auto  descIdx  = entrySet.offset / dst.rts_gframeCount;
				if(entrySet.isExternal) {
					log.trace("Not populating external entries [{}, {}) matching ID {}", auto(entrySet.offset), entrySet.offset + dst.rts_gframeCount, descIdx);
				} else {
					log.trace("render_target_storage: populating managed entries [{}, {}) matching ID {}", auto(entrySet.offset), entrySet.offset + dst.rts_gframeCount, descIdx);
					for(size_t rtargetIdx = 0; rtargetIdx < dst.rts_gframeCount; ++ rtargetIdx) {
						auto dstIdx = entrySet.offset + rtargetIdx;  assert(dstIdx < dst.rts_entries.size());
						#ifndef NDEBUG
							auto entryIsUninitialized = [&]() { constexpr auto z = RenderTarget { }; return 0 == memcmp(&z, &dst.rts_entries[dstIdx].managed, sizeof(z)); };
							assert(entryIsUninitialized());
						#endif
						initRtarget(dst.rts_entries[dstIdx].managed, vma, dev, dst.rts_descs[descIdx]);
					}
				}
			}
		}

		dst.rts_vma = vma;
		return std::move(dst);
	}

	RenderTargetStorage RenderTargetStorage::Factory::finalize(VmaAllocator vma) & {
		return auto(*this).finalize(vma);
	}

}
