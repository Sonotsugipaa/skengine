#include "_render_process.inl.hpp"

#include <cassert>
#include <unordered_set>

#include <vulkan/vulkan_format_traits.hpp>

#include <timer.tpp>



namespace SKENGINE_NAME_NS {

	namespace {

		template <typename T>
		concept ImageContainer = requires(T t) {
			requires std::ranges::input_range<T>;
			requires std::is_assignable_v<vkutil::ManagedImage, decltype(t.begin()->first)>;
		};

		template <typename T>
		concept NonconstImageContainer = requires(T t) {
			requires ImageContainer<T>;
			requires ! std::is_const_v<std::remove_reference_t<decltype(t.begin()->first)>>;
		};


		template <typename T = size_t>
		constexpr auto imageByteSize(const VkExtent3D& ext, VkFormat format) {
			return T(ext.width) * T(ext.height) * T(ext.depth) * T(vk::blockSize(vk::Format(format)));
		}


		#define RP_STEP_ID_ RenderProcess::StepId
		auto detectGraphLoop(
			const std::map<RP_STEP_ID_, std::set<RP_STEP_ID_>>& fwdMap,
			const std::unordered_map<RP_STEP_ID_, std::set<RP_STEP_ID_>>& origins
		) {
			using StepId = RP_STEP_ID_;
			std::set<StepId> visited;
			std::vector<StepId> r; r.reserve(3);

			#define find_assert_(MAP_, KEY_) [&]() { auto r = MAP_.find(KEY_); assert(r != MAP_.end()); return r->second; } ()

			std::deque<StepId> q;
			std::unordered_map<StepId, StepId> bwdVisitTree;
			for(auto origin : origins) {
				q.clear();
				bwdVisitTree.clear();
				auto pop = [&]() { auto r = q.front(); q.pop_front(); return r; };
				q.push_back(origin.first);
				while(! q.empty()) {
					auto depender = pop();
					auto deps = find_assert_(fwdMap, depender);
					visited.insert(depender);
					for(auto& dependee : deps) {
						if(dependee == depender) [[unlikely /* and stupid */]] { r = { depender }; return r; }
						if(dependee != origin.first) {
							if(! visited.contains(dependee)) {
								q.push_back(dependee);
								bwdVisitTree[dependee] = depender;
							}
						} else {
							r.reserve(bwdVisitTree.size() + 1);
							do {
								r.push_back(depender);
								depender = find_assert_(bwdVisitTree, depender);
							} while(depender != origin.first);
							r.push_back(origin.first);
							return r;
						}
					}
				}
			}

			assert(false);
			std::unreachable();
			return r;
		}
		#undef RP_STEP_ID_


		// Don't forget to account for the number of gframes, which this function doesn't
		size_t computeRequiredDepthImageCount(
			spdlog::logger* logger,
			const std::vector<std::pair<RenderProcess::StepId, RenderProcess::Step>>& steps
		) {
			using seq_idx_e = RenderProcess::seq_idx_e;
			if(steps.empty()) return 0;
			size_t curCount = 0, maxCount = 0;
			auto lastIndex = steps.front().second.seqIndex;
			auto logStepIntvl = [&]() {
				bool one = curCount == 1;
				logger->trace("render_process: step {} require {} depth image{}", seq_idx_e(lastIndex), curCount, one?"":"s");
			};
			for(auto& step : steps) {
				curCount += step.second.depthImageCount;
				if(lastIndex != step.second.seqIndex) {
					logStepIntvl();
					maxCount = std::max(maxCount, curCount);
					curCount = 0;
					lastIndex = step.second.seqIndex;
				}
			}
			logStepIntvl();
			return std::max(curCount, maxCount);
		};


		template <ImageContainer DepthImages>
		bool areDepthImagesCorrectlySized(const RenderTargetStorage& rtargetStorage, const DepthImages& depthImages) {
			VkExtent3D maxExt = { 0, 0, 0 };
			for(auto& desc : rtargetStorage.getDescriptionsRange()) {
				maxExt.width  = std::max(maxExt.width , desc.extent.width );
				maxExt.height = std::max(maxExt.height, desc.extent.height);
				maxExt.depth  = std::max(maxExt.depth , desc.extent.depth );
			}
			for(auto& depthImg : depthImages) {
				bool tooSmall =
					(depthImg.first.info().extent.width  < maxExt.width ) ||
					(depthImg.first.info().extent.height < maxExt.height) ||
					(depthImg.first.info().extent.depth  < maxExt.depth );
				if(tooSmall) [[unlikely]] return false;
			}
			return true;
		}


		template <NonconstImageContainer DepthImages>
		void createDepthImages(
			VmaAllocator vma,
			VkDevice dev,
			DepthImages& dst,
			const VkExtent3D& depthImageExtent,
			VkFormat depthImageFormat,
			size_t firstIndex = 0,
			size_t lastIndex = SIZE_MAX
		) {
			assert((! dst.empty()) && "depth image container must have the desired size");
			assert((dst.front().second == nullptr) && "depth image container elements must not contain existing images");
			firstIndex = std::min(firstIndex, dst.size());
			lastIndex  = std::min(lastIndex,  dst.size());
			if(firstIndex == lastIndex) [[unlikely]] return;
			vkutil::ImageCreateInfo depthImgIcInfo = { }; { auto& i = depthImgIcInfo;
				i.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				i.extent = depthImageExtent;
				i.format = depthImageFormat;
				i.type = VK_IMAGE_TYPE_2D;
				i.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				i.samples = VK_SAMPLE_COUNT_1_BIT;
				i.tiling = VK_IMAGE_TILING_OPTIMAL;
				i.arrayLayers = 1;
				i.mipLevels = 1;
			}
			vkutil::AllocationCreateInfo depthImgAcInfo = { }; { auto& i = depthImgAcInfo;
				i.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			}
			VkImageViewCreateInfo depthImgIvcInfo = { }; { auto& i = depthImgIvcInfo;
				#define SWID_ VK_COMPONENT_SWIZZLE_IDENTITY
				i.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				i.image = { };
				i.viewType = VK_IMAGE_VIEW_TYPE_2D;
				i.format = depthImageFormat;
				i.components = { SWID_, SWID_, SWID_, SWID_ };
				i.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				i.subresourceRange.levelCount = 1;
				i.subresourceRange.layerCount = 1;
				#undef SWID_
			}
			for(size_t depthImgIdx = firstIndex; depthImgIdx < lastIndex; ++ depthImgIdx) {
				auto& dstImage = dst[depthImgIdx];
				dstImage.first = vkutil::ManagedImage::create(vma, depthImgIcInfo, depthImgAcInfo);
				depthImgIvcInfo.image = dstImage.first;
				vkCreateImageView(dev, &depthImgIvcInfo, nullptr, &dstImage.second);
			}
		}


		template <NonconstImageContainer DepthImages>
		void destroyDepthImages(
			VmaAllocator vma,
			VkDevice dev,
			DepthImages& dst,
			size_t firstIndex = 0,
			size_t lastIndex = SIZE_MAX
		) {
			firstIndex = std::min(firstIndex, dst.size());
			lastIndex  = std::min(lastIndex,  dst.size());
			for(size_t i = firstIndex; i < lastIndex; ++i) {
				auto& img = dst[i];
				vkDestroyImageView(dev, img.second, nullptr);
				vkutil::ManagedImage::destroy(vma, img.first);
				#ifndef NDEBUG
					img = { };
				#endif
			}
		}

	}



	std::strong_ordering RenderProcess::WaveIterator::operator<=>(const WaveIterator&r) const noexcept {
		auto& l = *this;
		if(l.wi_rp == nullptr) {
			if(r.wi_rp == nullptr) return std::strong_ordering::equal;
			else return std::strong_ordering::greater;
		} else {
			assert(l.wi_validity == l.wi_rp->rp_waveIterValidity);
			if(r.wi_rp == nullptr) return std::strong_ordering::less;
			else if(l.wi_seqIdx  < r.wi_seqIdx) return std::strong_ordering::greater;
			else if(l.wi_seqIdx  > r.wi_seqIdx) return std::strong_ordering::less;
			else return std::strong_ordering::equal;
		}
	}


	RenderProcess::WaveIterator& RenderProcess::WaveIterator::operator++() {
		auto curStep = step_id_e(wi_firstStep);
		auto nxtStep = curStep + wi_stepCount;
		if constexpr (std::is_signed_v<step_id_e>) assert(std::make_signed_t<step_id_e>(curStep) >= 0); // make_signed is redundant, but makes GCC not nag without cause
		assert(wi_rp != nullptr);
		assert(wi_validity == wi_rp->rp_waveIterValidity);
		if(size_t(nxtStep) >= wi_rp->rp_steps.size()) {
			*this = WaveIterator();
		} else {
			wi_seqIdx = wi_rp->rp_steps[nxtStep].second.seqIndex;
			wi_firstStep = StepId(nxtStep);
			wi_stepCount = 1;
			for(step_id_e i = nxtStep + 1; size_t(i) < wi_rp->rp_steps.size(); ++i) {
				if(wi_rp->rp_steps[i].second.seqIndex != wi_seqIdx) break;
				++ wi_stepCount;
			}
		}
		return *this;
	}


	std::span<std::pair<RenderProcess::StepId, RenderProcess::Step>> RenderProcess::WaveIterator::operator*() {
		assert(wi_rp != nullptr);
		assert(wi_validity == wi_rp->rp_waveIterValidity);
		auto beg = wi_rp->rp_steps.begin() + size_t(wi_firstStep);
		auto end = beg + wi_stepCount;
		return std::span<std::pair<StepId, Step>>(beg, end);
	}


	#ifndef NDEBUG
		RenderProcess::~RenderProcess() {
			assert(! rp_initialized);
		}
	#endif


	void RenderProcess::setup(
		VmaAllocator vma,
		std::shared_ptr<spdlog::logger> logger,
		VkFormat depthImageFormat,
		unsigned gframeCount,
		const DependencyGraph& depGraph
	) {
		auto seqDesc = depGraph.assembleSequence();
		setup(vma, std::move(logger), depthImageFormat, gframeCount, seqDesc);
	}


	void RenderProcess::setup(
		VmaAllocator vma,
		std::shared_ptr<spdlog::logger> logger,
		VkFormat depthImageFormat,
		unsigned gframeCount,
		DependencyGraph&& depGraph
	) {
		setup(vma, std::move(logger), depthImageFormat, gframeCount, static_cast<DependencyGraph&>(depGraph));
	}


	void RenderProcess::setup(
		VmaAllocator vma,
		std::shared_ptr<spdlog::logger> mvLogger,
		VkFormat depthImageFormat,
		unsigned gframeCount,
		const SequenceDescription& seqDesc
	) {
		util::SteadyTimer<std::chrono::microseconds> timer;

		rp_gframeCount = gframeCount;
		rp_logger = std::move(mvLogger);
		rp_vkState = { vma, rp_vkState.depthImageFormat };
		auto vkDev = rp_vkState.device();
		auto& logger = *rp_logger;
		auto& rtsFac = * seqDesc.rtsFactory;

		rp_steps.resize(seqDesc.steps.size());
		rp_rpasses.resize(seqDesc.rpasses.size());
		rp_renderers.resize(seqDesc.renderers.size());

		rp_depthImageFormat = depthImageFormat;

		for(size_t i = 0; i < rp_steps.size(); ++i) { rp_steps[i] = { idFromIndex<StepId>(i), seqDesc.steps[i] }; }

		rp_rtargetStorage = rtsFac.finalize(vma);

		rp_depthImageExtent = { 0, 0, 0 };
		{ // Compute required depth image sizes
			// Memory optimization tip:
			// We can find out for any step if the images requirements are met by the
			// currently "allocated" images, and eventually "allocate" more; then sort
			// the "allocations" by size and make render steps pick the smallest fit.
			auto reqImageCount = computeRequiredDepthImageCount(&logger, rp_steps);
			rp_depthImages.resize(reqImageCount * rp_gframeCount, { });
			for(auto& stepPair : rp_steps) {
				auto& step = stepPair.second;
				rp_depthImageExtent.width  = std::max(rp_depthImageExtent.width , step.depthImageExtent.width );
				rp_depthImageExtent.height = std::max(rp_depthImageExtent.height, step.depthImageExtent.height);
				rp_depthImageExtent.depth  = std::max(rp_depthImageExtent.depth , step.depthImageExtent.depth );
			}
		}

		// Create depth images
		if(! rp_depthImages.empty()) {
			logger.trace("render_process: creating {} depth image{}, each {}x{}x{}", rp_depthImages.size(), (rp_depthImages.size() == 1)? "":"s", rp_depthImageExtent.width, rp_depthImageExtent.height, rp_depthImageExtent.depth);
			createDepthImages(vma, vkDev, rp_depthImages, rp_depthImageExtent, rp_depthImageFormat);
		}

		{ // Create rpasses
			size_t maxSubpassCount = 0;
			for(auto& desc : seqDesc.rpasses) maxSubpassCount = std::max(maxSubpassCount, desc.subpasses.size());
			auto rprocRpcInfo = RprocRpassCreateInfo { logger, vkDev, gframeCount, rp_rtargetStorage, rp_depthImageFormat, rp_depthImages };
			auto vectors = RprocRpassCreateVectorCache(maxSubpassCount, gframeCount);
			for(size_t rpassIdx = 0; rpassIdx < rp_rpasses.size(); ++ rpassIdx) {
				createRprocRpass(rp_rpasses.data() + rpassIdx, rpassIdx, &seqDesc.rpasses[rpassIdx], rprocRpcInfo, vectors);
			}
		}

		for(size_t i = 0; i < rp_renderers.size(); ++i) rp_renderers[i] = seqDesc.renderers[i].lock();

		++ rp_waveIterValidity;
		rp_initialized = true;

		logger.debug("render_process: setup took {}ms", timer.count<float>() / 1000.0f);
	}


	void RenderProcess::destroy() {
		auto vkDev = rp_vkState.device();
		assert(rp_initialized);

		for(size_t i = 0; i < rp_rpasses.size(); ++i) {
			auto& dst = rp_rpasses[i];
			if(dst) {
				for(auto& fb : dst.framebuffers) vkDestroyFramebuffer(vkDev, fb, nullptr);
				vkDestroyRenderPass(vkDev, dst.handle, nullptr);
			}
		}
		rp_rpasses.clear();

		destroyDepthImages(rp_vkState.vma, vkDev, rp_depthImages);
		rp_depthImages = { };

		rp_rtargetStorage = { };

		rp_gframeCount = 0;
		rp_initialized = false;
	}


	void RenderProcess::reset(unsigned newGframeCount, util::TransientPtrRange<RtargetResizeInfo> resizes) {
		util::SteadyTimer<std::chrono::microseconds> timer;
		VkExtent3D maxResize = { 0, 0, 0 };
		auto dev = rp_vkState.device();
		auto& logger = *rp_logger;

		bool doResize = resizes.size() > 0;
		bool doChangeGframeCount = newGframeCount == rp_gframeCount;
		bool doRecreateRpasses = doResize || doChangeGframeCount;
		std::vector<RenderPassDescription> rpassDescs;

		auto destroyRpasses = [&](std::vector<RenderPassDescription>* rpassDescs, VkDevice dev) {
			rpassDescs->reserve(rp_rpasses.size());
			for(size_t i = 0; i < rp_rpasses.size(); i++) {
				rpassDescs->emplace_back(std::move(rp_rpasses[i].description));
				destroyRprocRpass(rp_rpasses.data() + i, dev);
			}
		};

		auto recreateRpasses = [&](const std::vector<RenderPassDescription>* rpassDescs, VkDevice dev) {
			size_t maxSubpassCount = 0;
			for(auto& desc : *rpassDescs) maxSubpassCount = std::max(maxSubpassCount, desc.subpasses.size());
			auto rprocRpcInfo = RprocRpassCreateInfo { logger, dev, newGframeCount, rp_rtargetStorage, rp_depthImageFormat, rp_depthImages };
			auto vectors = RprocRpassCreateVectorCache(maxSubpassCount, newGframeCount);
			for(size_t rpassIdx = 0; rpassIdx < rp_rpasses.size(); ++ rpassIdx) {
				createRprocRpass(rp_rpasses.data() + rpassIdx, rpassIdx, &(*rpassDescs)[rpassIdx], rprocRpcInfo, vectors);
			}
		};

		if(doRecreateRpasses) rp_rtargetStorage.updateRtargetReferences();

		for(auto& resize : resizes) {
			maxResize.width  = std::max(maxResize.width , resize.newExtent.width );
			maxResize.height = std::max(maxResize.height, resize.newExtent.height);
			maxResize.depth  = std::max(maxResize.depth , resize.newExtent.depth );
			rp_rtargetStorage.setRtargetExtent(resize.rtarget, resize.newExtent);
		}

		bool doResizeDepthImages = doChangeGframeCount;
		if(doResize && ! doResizeDepthImages)
		for(auto& depthImg : rp_depthImages) {
			doResizeDepthImages =
				(depthImg.first.info().extent.width  < maxResize.width ) &&
				(depthImg.first.info().extent.height < maxResize.height) &&
				(depthImg.first.info().extent.depth  < maxResize.depth );
			if(doResizeDepthImages) [[unlikely]] break;
		}

		if(doResizeDepthImages) [[likely]] {
			rp_depthImageExtent = maxResize;
			logger.trace("render_process: resizing depth images to {}x{}x{}", rp_depthImageExtent.width, rp_depthImageExtent.height, rp_depthImageExtent.depth);
			destroyDepthImages(rp_vkState.vma, dev, rp_depthImages);
			createDepthImages(rp_vkState.vma, dev, rp_depthImages, rp_depthImageExtent, rp_depthImageFormat);
		}

		if(doRecreateRpasses) destroyRpasses(&rpassDescs, dev);

		if(doChangeGframeCount) {
			rp_rtargetStorage.setGframeCount(newGframeCount);

			auto oldRequiredDepthImageCount = rp_depthImages.size();
			auto newRequiredDepthImageCount = computeRequiredDepthImageCount(rp_logger.get(), rp_steps) * newGframeCount;
			if(newRequiredDepthImageCount != oldRequiredDepthImageCount) {
				auto dev = rp_vkState.device();
				if(newRequiredDepthImageCount < oldRequiredDepthImageCount) {
					destroyDepthImages(rp_vkState.vma, dev, rp_depthImages, newRequiredDepthImageCount, SIZE_MAX);
					rp_depthImages.resize(newRequiredDepthImageCount);
				}
				else if(newRequiredDepthImageCount > oldRequiredDepthImageCount) {
					auto& ext = rp_depthImageExtent;
					auto& fmt = rp_depthImageFormat;
					rp_depthImages.resize(newRequiredDepthImageCount);
					createDepthImages(rp_vkState.vma, dev, rp_depthImages, ext, fmt, oldRequiredDepthImageCount, SIZE_MAX);
				}
			}

			assert((rp_gframeCount != newGframeCount) && "`rp_gframeCount` must only be updated at the very end");
			rp_gframeCount = newGframeCount;
		}

		if(doRecreateRpasses) recreateRpasses(&rpassDescs, dev);

		logger.debug("render_process: reset operation took {}ms", timer.count<float>() / 1000.0f);
	}


	RenderProcess::WaveRange RenderProcess::waveRange() & {
		assert(rp_rtargetStorage.gframeCount() == rp_gframeCount);
		#ifndef NDEBUG
			assert(areDepthImagesCorrectlySized(rp_rtargetStorage, rp_depthImages));
		#endif
		WaveRange r = WaveRange {
			.beginIter = WaveIterator(),
			.endIter = WaveIterator() };
		r.beginIter.wi_rp = this;
		r.beginIter.wi_validity = rp_waveIterValidity;
		++ r.beginIter;
		return r;
	}


	#define RP_DG_ RenderProcess::DependencyGraph


	RP_DG_::Subgraph& RP_DG_::Subgraph::before(const Subgraph& sg) {
		sg_graph->dg_dependencies_fwd.find(sg_step)->second.insert(sg.sg_step);
		sg_graph->dg_dependencies_bwd.find(sg.sg_step)->second.insert(sg_step);
		return *this;
	}

	RP_DG_::Subgraph& RP_DG_::Subgraph::after(const Subgraph& sg) {
		sg_graph->dg_dependencies_fwd.find(sg.sg_step)->second.insert(sg_step);
		sg_graph->dg_dependencies_bwd.find(sg_step)->second.insert(sg.sg_step);
		return *this;
	}


	RenderTargetId RP_DG_::addRtarget(RenderTargetDescription rtDesc) {
		return dg_rtsFactory->setRenderTarget(std::move(rtDesc));
	}

	RenderPassId RP_DG_::addRpass(RenderPassDescription rpDesc) {
		dg_rpasses.push_back(std::move(rpDesc));
		return idFromIndex<RenderPassId>(dg_rpasses.size() - 1);
	}

	RendererId RP_DG_::addRenderer(std::weak_ptr<Renderer> renderer) {
		dg_renderers.push_back(renderer.lock());
		return idFromIndex<RendererId>(dg_renderers.size() - 1);
	}


	RP_DG_::Subgraph RP_DG_::addDummyStep() {
		Subgraph r;
		r.sg_graph = this;
		r.sg_step = idFromIndex<StepId>(dg_steps.size());
		dg_steps.push_back({ idgen::invalidId<RenderPassId>(), idgen::invalidId<RendererId>() });
		dg_dependencies_fwd.insert(DependencyMap::value_type { r.sg_step, { } });
		dg_dependencies_bwd.insert(DependencyMap::value_type { r.sg_step, { } });
		return r;
	}

	RP_DG_::Subgraph RP_DG_::addStep(RenderPassId rpass, RendererId renderer) {
		Subgraph r;
		r.sg_graph = this;
		r.sg_step = idFromIndex<StepId>(dg_steps.size());
		dg_steps.push_back({ rpass, renderer });
		dg_dependencies_fwd.insert(DependencyMap::value_type { r.sg_step, { } });
		dg_dependencies_bwd.insert(DependencyMap::value_type { r.sg_step, { } });
		return r;
	}


	RenderProcess::SequenceDescription RP_DG_::assembleSequence() const {
		seq_idx_e seq = 0;
		SequenceDescription r;
		auto mkStepSet = [this]() { std::unordered_set<StepId> r; r.max_load_factor(1.2); r.reserve(dg_steps.size()); return r; };
		std::unordered_set<StepId> resolvedSteps;

		std::unordered_map<StepId, std::set<StepId>> unresolvedSteps;
		unresolvedSteps.max_load_factor(1.2);
		unresolvedSteps.reserve(dg_steps.size());
		for(auto& dep : dg_dependencies_bwd) unresolvedSteps.insert(dep);

		r.steps.reserve(dg_steps.size());
		r.rtsFactory = dg_rtsFactory;
		r.rpasses    = dg_rpasses;
		r.renderers  = dg_renderers;

		auto getTargetSizes = [&](RenderTargetId id) {
			struct R {
				uint_fast32_t bytes;
				decltype(VkExtent3D::width ) width ;
				decltype(VkExtent3D::height) height;
				decltype(VkExtent3D::depth ) depth ;
			};
			auto& target = r.rtsFactory->dst.getDescription(id);
			return R { imageByteSize<uint_fast32_t>(target.extent, target.format), target.extent.width, target.extent.height, target.extent.depth };
		};

		auto subpassDepthSizes = [&](
			VkExtent3D& dstExt,
			uint_fast32_t& dstSize,
			uint_fast32_t& dstCount,
			const std::vector<RenderPassDescription::Subpass>& subpasses
		) {
			dstSize = 0;
			dstExt = { 0, 0, 0 };
			for(auto& subpass : subpasses) {
				if(subpass.requiresDepthAttachments) {
					for(auto& atch : subpass.colorAttachments) {
						auto sizes = getTargetSizes(atch.rtarget);
						dstSize = std::max(dstSize, sizes.bytes);
						dstExt.width  = std::max(dstExt.width , sizes.width );
						dstExt.height = std::max(dstExt.height, sizes.height);
						dstExt.depth  = std::max(dstExt.depth , sizes.depth );
					}
					++ dstCount;
				}
			}
		};

		while(resolvedSteps.size() < dg_steps.size()) {
			auto localResolvedSteps = mkStepSet();
			auto localUnresolvedSteps = unresolvedSteps;
			uint_fast32_t depthImageSize = 0;
			uint_fast32_t depthImageCount = 0;
			VkExtent3D    depthImageExtent = { 0, 0, 0 };
			size_t resolved = 0;
			for(auto& stepDeps : unresolvedSteps) {
				assert(! resolvedSteps.contains(stepDeps.first));
				bool skipDep = [&]() {
					for(StepId dep : stepDeps.second) { if(! resolvedSteps.contains(dep)) return true; }
					return false;
				} ();
				if(! skipDep) {
					auto  stepIdx = idToIndex<StepId>(stepDeps.first);
					auto& step    = dg_steps[stepIdx];
					if(step.rpass != idgen::invalidId<RenderPassId>()) { // Scan required depth images
						auto& rpass = dg_rpasses[idToIndex<RenderPassId>(step.rpass)];
						subpassDepthSizes(depthImageExtent, depthImageSize, depthImageCount, rpass.subpasses);
					}
					r.steps.push_back(Step {
						.seqIndex         = SequenceIndex(seq),
						.rpass            = step.rpass,
						.renderer         = step.renderer,
						.depthImageCount  = uint32_t(depthImageCount),
						.depthImageExtent = depthImageExtent,
						.depthImageSize   = uint32_t(depthImageSize) });
					localResolvedSteps.insert(stepDeps.first);
					localUnresolvedSteps.erase(stepDeps.first);
					++ resolved;
				}
			}
			if(resolved == 0) {
				auto loop = detectGraphLoop(dg_dependencies_fwd, unresolvedSteps);
				throw RenderProcess::UnsatisfiableDependencyError(std::move(loop));
			}
			unresolvedSteps = std::move(localUnresolvedSteps);
			resolvedSteps.merge(localResolvedSteps);
			++ seq;
		}

		return r;
	}


	RenderProcess::UnsatisfiableDependencyError::UnsatisfiableDependencyError(std::vector<RenderProcess::StepId> dependencyChain):
		std::runtime_error("cyclic render step dependency"),
		dep_err_depChain(std::move(dependencyChain))
	{ }


	#undef RP_DG_

}
