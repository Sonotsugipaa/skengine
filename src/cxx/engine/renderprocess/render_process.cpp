#include "_render_process.inl.hpp"

#include <cassert>
#include <unordered_set>

#include <vk-util/error.hpp>

#include <vulkan/vulkan_format_traits.hpp>

#include <timer.tpp>



namespace SKENGINE_NAME_NS {

	namespace {

		template <typename T = size_t>
		constexpr auto imageByteSize(const VkExtent3D& ext, VkFormat format) {
			return T(ext.width) * T(ext.height) * T(ext.depth) * T(vk::blockSize(vk::Format(format)));
		}

		template <typename T = size_t>
		constexpr auto imageByteSize(const VkExtent2D& ext, VkFormat format) {
			return imageByteSize(VkExtent3D { ext.width, ext.height, 1 }, format);
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

		VkExtent3D depthImageExtent = { 0, 0, 1 };
		{ // Compute required depth image count and sizes
			// Memory optimization tip:
			// We can find out for any step if the images requirements are met by the
			// currently "allocated" images, and eventually "allocate" more; then sort
			// the "allocations" by size and make render steps pick the smallest fit.
			size_t maxCount = 0;
			for(auto& step : seqDesc.steps) {
				depthImageExtent.width  = std::max(depthImageExtent.width,  step.depthImageExtent.width);
				depthImageExtent.height = std::max(depthImageExtent.height, step.depthImageExtent.height);
				maxCount = std::max<size_t>(maxCount, step.depthImageCount);
			}
			rp_depthImages.resize(maxCount * rp_gframeCount);
		}

		for(size_t i = 0; i < rp_steps.size(); ++i) rp_steps[i] = { idFromIndex<StepId>(i), seqDesc.steps[i] };

		rp_rtargetStorage = rtsFac.finalize(vma);

		// Create depth images
		logger.trace("render_process: creating {} depth image{}, each {}x{}", rp_depthImages.size(), (rp_depthImages.size() == 1)? "":"s", depthImageExtent.width, depthImageExtent.height);
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
			i.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			i.subresourceRange.levelCount = 1;
			i.subresourceRange.layerCount = 1;
			#undef SWID_
		}
		for(size_t depthImgIdx = 0; depthImgIdx < rp_depthImages.size(); ++ depthImgIdx) {
			auto& dst = rp_depthImages[depthImgIdx];
			dst.first = vkutil::ManagedImage::create(rp_vkState.vma, depthImgIcInfo, depthImgAcInfo);
			vkCreateImageView(vkDev, &depthImgIvcInfo, nullptr, &dst.second);
		}

		// Create rpasses
		for(size_t i = 0; i < rp_rpasses.size(); ++i) {
			auto& dst  = rp_rpasses[i];
			auto& desc = seqDesc.rpasses[i];
			size_t usedDepthImages = 0;
			uint_fast32_t colorAtchCount = 0;
			dst = RenderPass { };
			dst.description = seqDesc.rpasses[i];
			logger.trace("render_process: creating rpass {}", i);
			auto atchHeuristic = (desc.subpasses.size() * size_t(3)) / size_t(2);
			using ImageViewVec3 = std::vector<std::vector<std::vector<VkImageView>>>;
			std::vector<VkAttachmentDescription>  atchDescs;        atchDescs       .reserve(atchHeuristic);
			std::vector<VkAttachmentReference>    atchRefs;         atchRefs        .reserve(atchHeuristic);
			std::vector<VkSubpassDescription>     subpassDescs;     subpassDescs    .reserve(atchHeuristic);
			std::vector<VkSubpassDependency>      subpassDeps;      subpassDeps     .reserve(atchHeuristic);
			std::vector<VkExtent3D>               subpassFbSizes;   subpassFbSizes  .reserve(atchHeuristic);
			ImageViewVec3                         subpassAtchViews; subpassAtchViews.reserve(atchHeuristic * rp_gframeCount);
			// Initial layout of subpassAtchViews:
			//    sp0 { a0 { gf0, gf1, gf2 } }, a1 { gf0, gf1, gf2 } },
			//    sp1 { a0 { gf0, gf1, gf2 } }, a1 { gf0, gf1, gf2 } } ...
			for(size_t spIdx = 0; spIdx < desc.subpasses.size(); ++ spIdx) {
				auto& rpSubpass = desc.subpasses[spIdx];
				logger.trace("render_process: appending subpass {}", spIdx);
				subpassFbSizes.push_back({ });
				subpassAtchViews.push_back({ });
				auto& subpassAtchViewSet = subpassAtchViews.back();
				auto appendAttachment = [&](const decltype(rpSubpass.inputAttachments[0])& rpAtchDescs) {
					auto rtarget = rp_rtargetStorage.getEntrySet(rpAtchDescs.rtarget); assert(rtarget.begin() != rtarget.end());
					auto atchIdx = atchDescs.size();
					logger.trace("render_process: attachment {} has rtarget ID {}", atchDescs.size(), render_target_id_e(rpAtchDescs.rtarget));
					subpassAtchViewSet.push_back({ });
					for(size_t i = 0; i < rp_gframeCount; ++i) { // Append one image view for each gframe
						subpassAtchViewSet.back().push_back(rtarget.getImageView(i));
					}
					atchDescs.push_back({ });
					atchRefs.push_back({ });
					auto& atchDesc = atchDescs.back();
					atchDesc.format = rtarget.getFormat(0);
					atchDesc.samples = VK_SAMPLE_COUNT_1_BIT;
					atchDesc.loadOp = rpAtchDescs.loadOp;
					atchDesc.storeOp = rpAtchDescs.storeOp;
					atchDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
					atchDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
					atchDesc.initialLayout = rpAtchDescs.initialLayout;
					atchDesc.finalLayout = rpAtchDescs.finalLayout;
					std::tie(atchDesc.loadOp,        atchDesc.storeOp)        = std::pair(rpAtchDescs.loadOp,              rpAtchDescs.storeOp);
					std::tie(atchDesc.stencilLoadOp, atchDesc.stencilStoreOp) = std::pair(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE);
					std::tie(atchDesc.initialLayout, atchDesc.finalLayout)    = std::pair(rpAtchDescs.initialLayout,       rpAtchDescs.finalLayout);
					auto& atchRef = atchRefs.back();
					atchRef.attachment = atchIdx;
					atchRef.layout = atchDesc.finalLayout;
					auto& fbSize = subpassFbSizes.back();
					auto& rtargetExtent = rtarget.getExtent(0);
					fbSize.width  = std::max(fbSize.width,  rtargetExtent.width);
					fbSize.height = std::max(fbSize.height, rtargetExtent.height);
					fbSize.depth  = std::max(fbSize.depth,  rtargetExtent.depth);
				};
				size_t firstInputAtch = atchDescs.size();
				logger.trace("render_process: appending {} input attachment{} at index {}", rpSubpass.inputAttachments.size(), (rpSubpass.inputAttachments.size()) == 1? "":"s", firstInputAtch);
				for(auto& atch : rpSubpass.inputAttachments) appendAttachment(atch);
				size_t firstColorAtch = atchDescs.size();
				logger.trace("render_process: appending {} color attachment{} at index {}", rpSubpass.colorAttachments.size(), (rpSubpass.colorAttachments.size()) == 1? "":"s", firstColorAtch);
				for(auto& atch : rpSubpass.colorAttachments) appendAttachment(atch);
				VkAttachmentDescription depthAtchDesc;
				VkAttachmentReference depthAtchRef;
				if(rpSubpass.requiresDepthAttachments) {
					logger.trace("render_process: appending depth attachment {}", usedDepthImages);
					atchDescs.push_back({ }); auto& atchDesc = atchDescs.back();
					atchRefs.push_back({ });  auto& atchRef  = atchRefs.back();
					subpassAtchViewSet.push_back({ });
					for(size_t i = 0; i < rp_gframeCount; ++i) subpassAtchViewSet.back().push_back(rp_depthImages[usedDepthImages + i].second);
					depthAtchDesc = { };
					atchDesc.format = depthImageFormat;
					atchDesc.samples = VK_SAMPLE_COUNT_1_BIT;
					std::tie(atchDesc.loadOp,        atchDesc.storeOp)        = std::pair(VK_ATTACHMENT_LOAD_OP_CLEAR,     VK_ATTACHMENT_STORE_OP_DONT_CARE);
					std::tie(atchDesc.stencilLoadOp, atchDesc.stencilStoreOp) = std::pair(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE);
					std::tie(atchDesc.initialLayout, atchDesc.finalLayout)    = std::pair(VK_IMAGE_LAYOUT_UNDEFINED,       VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
					atchRef.attachment = atchDescs.size();
					atchRef.layout = atchDesc.finalLayout;
					++ usedDepthImages;
				}
				subpassDescs.push_back({ });
				auto& subpassDesc = subpassDescs.back(); {
					subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
					subpassDesc.inputAttachmentCount = rpSubpass.inputAttachments.size();
					subpassDesc.pInputAttachments    = atchRefs.data() + firstInputAtch;
					subpassDesc.colorAttachmentCount = rpSubpass.colorAttachments.size();
					subpassDesc.pColorAttachments    = atchRefs.data() + firstColorAtch;
					subpassDesc.pDepthStencilAttachment = rpSubpass.requiresDepthAttachments? &depthAtchRef : nullptr;
				}
				for(auto& rpDep : rpSubpass.subpassDependencies) {
					subpassDeps.push_back({ });
					auto& dstDep = subpassDeps.back(); {
						dstDep.srcSubpass      = rpDep.srcSubpass;
						dstDep.srcStageMask    = rpDep.srcStageMask;
						dstDep.dstStageMask    = rpDep.dstStageMask;
						dstDep.srcAccessMask   = rpDep.srcAccessMask;
						dstDep.dstAccessMask   = rpDep.dstAccessMask;
						dstDep.dependencyFlags = rpDep.dependencyFlags;
					}
				}
				colorAtchCount += rpSubpass.colorAttachments.size();
			}
			// Reorder image view sets, so that gframe indices (instead of subpass indices) are contiguous
			if(desc.subpasses.size() * rp_gframeCount > 1) {
				// Example:     sp0 { a0  { gf0, gf1 }, a1  { gf0, gf1 } }, sp1 { a0  { gf0, gf1 }, a1  { gf0, gf1 } }
				// ... becomes  gf0 { sp0 { a0,  a1 },  sp1 { a0,  a1  } }, gf1 { sp0 { a0,  a1 },  sp1 { a0,  a1 } }
				// This is necessary because a rpass' framebuffer expects {sp0,sp1,sp2} image views
				// and each gframe has its own framebuffer..
				size_t subpassCount = desc.subpasses.size();
				auto swapOut = ImageViewVec3(rp_gframeCount);
				auto appendGframeViews = [&](size_t gframeOffset) {
					auto& dstSpSlot = swapOut[gframeOffset];
					dstSpSlot.reserve(dstSpSlot.size() + subpassCount);
					for(size_t spIdx = 0; spIdx < subpassCount; ++ spIdx) {
						auto& src0 = subpassAtchViews[spIdx]; assert(spIdx < subpassAtchViews.size());
						for(size_t atchIdx = 0; atchIdx < src0.size(); ++ atchIdx) {
							auto& src1 = src0[atchIdx];      assert(atchIdx < src0.size());
							auto& src2 = src1[gframeOffset]; assert(gframeOffset < src1.size());
							dstSpSlot.push_back({ });
							auto& dstAtchSlot = dstSpSlot.back();
							logger.trace("render_process: attachment image view {:016x} set for gframe {}, subpass {} attachment {}", size_t(src2), gframeOffset, spIdx, atchIdx);
							dstAtchSlot.push_back(src2);
						}
					}
				};
				for(size_t i = 0; i < rp_gframeCount; ++i) appendGframeViews(i);
				subpassAtchViews = std::move(swapOut);
			}
			if(desc.subpasses.size() > 1) logger.trace("render_process: finished appending subpasses");
			VkRenderPassCreateInfo rpcInfo = { }; {
				rpcInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpcInfo.attachmentCount = atchDescs.size();
				rpcInfo.pAttachments = atchDescs.data();
				rpcInfo.subpassCount = subpassDescs.size();
				rpcInfo.pSubpasses = subpassDescs.data();
				rpcInfo.dependencyCount = subpassDeps.size();
				rpcInfo.pDependencies = subpassDeps.data();
			}
			assert(atchDescs.size() == atchRefs.size());
			VK_CHECK(vkCreateRenderPass, vkDev, &rpcInfo, nullptr, &dst.handle);
			dst.framebuffers.reserve(rp_gframeCount);
			// Create one framebuffer for each rpass, for each gframe
			std::vector<VkImageView> framebufferAttachmentList;
			for(size_t spIdx = 0; spIdx < desc.subpasses.size(); ++ spIdx) {
				framebufferAttachmentList.clear();
				for(auto& sp : subpassAtchViews[spIdx]) { for(auto* a : sp) {
					logger.trace("render_process: subpass {} attachment {} is {:x}", spIdx, framebufferAttachmentList.size(), size_t(a));
					framebufferAttachmentList.push_back(a);
				} }
				VkFramebufferCreateInfo fbcInfo = { };
				fbcInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				fbcInfo.renderPass = dst.handle;
				auto& fbSize = subpassFbSizes[spIdx];
				fbcInfo.width  = fbSize.width;
				fbcInfo.height = fbSize.height;
				fbcInfo.layers = fbSize.depth;
				for(size_t gframeIdx = 0; gframeIdx < rp_gframeCount; ++ gframeIdx) {
					assert(subpassAtchViews.size() > spIdx);
					fbcInfo.attachmentCount = framebufferAttachmentList.size();
					fbcInfo.pAttachments    = framebufferAttachmentList.data();
					VkFramebuffer fb;
					VK_CHECK(vkCreateFramebuffer, vkDev, &fbcInfo, nullptr, &fb);
					dst.framebuffers.push_back(fb);
				}
			}
		}

		for(size_t i = 0; i < rp_renderers.size(); ++i) rp_renderers[i] = seqDesc.renderers[i].lock();

		++ rp_waveIterValidity;
		rp_initialized = true;

		logger.debug("render_process: setup took {}ms", (long double)(timer.count()) / 1000.0l);
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

		for(auto& img : rp_depthImages) {
			vkDestroyImageView(vkDev, img.second, nullptr);
			vkutil::ManagedImage::destroy(rp_vkState.vma, img.first);
			#ifndef NDEBUG
				img = { };
			#endif
		}
		rp_depthImages.clear();

		rp_rtargetStorage = { };

		rp_gframeCount = 0;
		rp_initialized = false;
	}


	RenderProcess::WaveRange RenderProcess::waveRange() & {
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
				decltype(VkExtent2D::width)  width;
				decltype(VkExtent2D::height) height;
			};
			auto& target = r.rtsFactory->dst.getDescription(id);
			return R { imageByteSize<uint_fast32_t>(target.extent, target.format), target.extent.width, target.extent.height };
		};

		auto subpassDepthSizes = [&](
			VkExtent2D& dstExt,
			uint_fast32_t& dstSize,
			uint_fast32_t& dstCount,
			const std::vector<RenderPassDescription::Subpass>& subpasses
		) {
			dstSize = 0;
			dstExt = { 0, 0 };
			for(auto& subpass : subpasses) {
				if(subpass.requiresDepthAttachments) {
					for(auto& atch : subpass.colorAttachments) {
						auto sizes = getTargetSizes(atch.rtarget);
						dstSize = std::max(dstSize, sizes.bytes);
						dstExt.width  = std::max(dstExt.width,  sizes.width);
						dstExt.height = std::max(dstExt.height, sizes.height);
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
			VkExtent2D    depthImageExtent = { 0, 0 };
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
