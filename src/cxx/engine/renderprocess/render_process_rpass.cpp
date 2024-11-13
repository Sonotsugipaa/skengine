#include "_render_process.inl.hpp"

#include <cassert>

#include <vk-util/error.hpp>



namespace SKENGINE_NAME_NS {

	void createRprocRpass(
		RenderPass* dst,
		size_t rpassIdx,
		const RenderPassDescription* rpassDesc,
		const RprocRpassCreateInfo& rprocRpcInfo,
		const RenderTargetStorage& rtStorage,
		RprocRpassCreateVectorCache& vectorCache
	) {
		#define MK_ALIAS_(S_, M_) auto& M_ = S_.M_;
		MK_ALIAS_(rprocRpcInfo, logger)
		MK_ALIAS_(rprocRpcInfo, vma)
		MK_ALIAS_(rprocRpcInfo, gframeCount)
		MK_ALIAS_(rprocRpcInfo, rtargetStorage)
		MK_ALIAS_(rprocRpcInfo, depthImageFormat)
		MK_ALIAS_(vectorCache, atchDescs)
		MK_ALIAS_(vectorCache, atchRefs)
		MK_ALIAS_(vectorCache, atchRefIndices)
		MK_ALIAS_(vectorCache, subpassDescs)
		MK_ALIAS_(vectorCache, subpassDeps)
		MK_ALIAS_(vectorCache, subpassAtchViews)
		#undef MK_ALIAS_
		auto vkDev = [&](auto vma) { VmaAllocatorInfo i; vmaGetAllocatorInfo(vma, &i); return i.device; } (vma);
		*dst = RenderPass { };
		dst->description = *rpassDesc;
		logger.trace("Creating rpass {}", rpassIdx);
		vectorCache.clear();
		dst->framebuffers.resize(gframeCount, { });

		constexpr auto isDepthRtValid = [](RenderTargetId id) { return id != idgen::invalidId<RenderTargetId>(); };

		// Populate the attachment vectors.
		// Layout of subpassAtchViews:
		//    sp0 { a0 { gf0, gf1, gf2 } }, a1 { gf0, gf1, gf2 } },
		//    sp1 { a0 { gf0, gf1, gf2 } }, a1 { gf0, gf1, gf2 } } ...
		for(size_t spIdx = 0; spIdx < rpassDesc->subpasses.size(); ++ spIdx) {
			auto& rpSubpass = rpassDesc->subpasses[spIdx];
			logger.trace("Appending subpass {}", spIdx);
			subpassAtchViews.push_back({ });
			auto& subpassAtchViewSet_sp = subpassAtchViews.back();
			auto appendAttachment = [&](const decltype(rpSubpass.inputAttachments[0])& rpAtchDescs) {
				auto rtarget = rtargetStorage.getEntrySet(rpAtchDescs.rtarget); assert(rtarget.begin() != rtarget.end());
				auto atchIdx = atchDescs.size();
				logger.trace("Attachment {} has rtarget ID {}", atchDescs.size(), render_target_id_e(rpAtchDescs.rtarget));
				subpassAtchViewSet_sp.push_back({ });
				auto& subpassAtchViewSet_a = subpassAtchViewSet_sp.back();
				for(size_t i = 0; i < gframeCount; ++i) { // Append one image view for each gframe
					subpassAtchViewSet_a.push_back(rtarget.getImageView(i));
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
			};
			size_t firstInputAtch = atchRefs.size();
			logger.trace("Appending {} input attachment{} at index {}", rpSubpass.inputAttachments.size(), (rpSubpass.inputAttachments.size()) == 1? "":"s", firstInputAtch);
			for(auto& atch : rpSubpass.inputAttachments) appendAttachment(atch);
			size_t firstColorAtch = atchRefs.size();
			logger.trace("Appending {} color attachment{} at index {}", rpSubpass.colorAttachments.size(), (rpSubpass.colorAttachments.size()) == 1? "":"s", firstColorAtch);
			for(auto& atch : rpSubpass.colorAttachments) appendAttachment(atch);
			VkAttachmentDescription depthAtchDesc;
			size_t depthAtch = atchRefs.size();
			if(isDepthRtValid(rpSubpass.depthRtarget)) { // Subpass has depth attachment
				atchDescs.push_back({ }); auto& atchDesc = atchDescs.back();
				atchRefs.push_back({ });  auto& atchRef  = atchRefs.back();
				subpassAtchViewSet_sp.push_back({ });
				auto& subpassAtchViewSet_a = subpassAtchViewSet_sp.back();
				auto  depthRtSet = rtStorage.getEntrySet(rpSubpass.depthRtarget);
				assert(rtStorage.getDescription(rpSubpass.depthRtarget).requiresImageView && "Depth render target requires an image view	");
				for(size_t i = 0; i < gframeCount; ++i) {
					assert(i < depthRtSet.entries.size());
					auto depthImageView = depthRtSet.getImageView(i);
					logger.trace("Appending depth attachment a{},gf{} = {:016x}", subpassAtchViewSet_sp.size(), subpassAtchViewSet_a.size(), size_t(depthImageView));
					subpassAtchViewSet_a.push_back(depthImageView);
				}
				depthAtchDesc = { };
				atchDesc.format = depthImageFormat;
				atchDesc.samples = VK_SAMPLE_COUNT_1_BIT;
				std::tie(atchDesc.loadOp,        atchDesc.storeOp)        = std::pair(rpSubpass.depthLoadOp,           rpSubpass.depthStoreOp);
				std::tie(atchDesc.stencilLoadOp, atchDesc.stencilStoreOp) = std::pair(VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE);
				std::tie(atchDesc.initialLayout, atchDesc.finalLayout)    = std::pair(rpSubpass.depthInitialLayout,    rpSubpass.depthFinalLayout);
				atchRef.attachment = depthAtch;
				atchRef.layout = atchDesc.finalLayout;
			}
			atchRefIndices.push_back(std::tuple(firstInputAtch, firstColorAtch, depthAtch));
		}

		// Populate the subpass descriptions.
		// This needs to happen in a separate loop from the population of attachments,
		// due to attachment vectors being invalidated.
		assert(rpassDesc->subpasses.size() == atchRefIndices.size());
		for(size_t spIdx = 0; spIdx < rpassDesc->subpasses.size(); ++ spIdx) {
			auto& rpSubpass = rpassDesc->subpasses[spIdx];
			subpassDescs.push_back({ });
			auto& subpassDesc = subpassDescs.back(); {
				auto& indices = atchRefIndices[spIdx];
				subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpassDesc.inputAttachmentCount = rpSubpass.inputAttachments.size();
				subpassDesc.pInputAttachments    = atchRefs.data() + std::get<0>(indices);
				subpassDesc.colorAttachmentCount = rpSubpass.colorAttachments.size();
				subpassDesc.pColorAttachments    = atchRefs.data() + std::get<1>(indices);
				subpassDesc.pDepthStencilAttachment = isDepthRtValid(rpSubpass.depthRtarget)? atchRefs.data() + std::get<2>(indices) : nullptr;
			}
			for(auto& rpDep : rpSubpass.subpassDependencies) {
				subpassDeps.push_back({ });
				auto& dstDep = subpassDeps.back(); {
					dstDep.srcSubpass      = rpDep.srcSubpass;
					dstDep.dstSubpass      = spIdx;
					dstDep.srcStageMask    = rpDep.srcStageMask;
					dstDep.dstStageMask    = rpDep.dstStageMask;
					dstDep.srcAccessMask   = rpDep.srcAccessMask;
					dstDep.dstAccessMask   = rpDep.dstAccessMask;
					dstDep.dependencyFlags = rpDep.dependencyFlags;
				}
			}
		}

		logger.trace("Finished appending subpasses for rpass {}", rpassIdx);
		VkRenderPassCreateInfo rpcInfo = { }; {
			rpcInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpcInfo.attachmentCount = atchDescs.size();
			rpcInfo.pAttachments = atchDescs.data();
			rpcInfo.subpassCount = subpassDescs.size();
			rpcInfo.pSubpasses = subpassDescs.data();
			rpcInfo.dependencyCount = subpassDeps.size();
			rpcInfo.pDependencies = subpassDeps.data();
		}

		try { // Create the rpass, then framebuffers (one for each rpass, for each gframe)
			VK_CHECK(vkCreateRenderPass, vkDev, &rpcInfo, nullptr, &dst->handle);
			std::vector<VkImageView> framebufferAttachmentList;
			VkFramebufferCreateInfo fbcInfo = { };
			fbcInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbcInfo.renderPass = dst->handle;
			auto& fbExtent = rpassDesc->framebufferSize;
			fbcInfo.width  = fbExtent.width;
			fbcInfo.height = fbExtent.height;
			fbcInfo.layers = fbExtent.depth;
			for(size_t gframeIdx = 0; gframeIdx < gframeCount; ++ gframeIdx) {
				framebufferAttachmentList.clear();
				for(size_t spIdx = 0; spIdx < rpassDesc->subpasses.size(); ++ spIdx) {
					assert(spIdx < subpassAtchViews.size());
					auto& spAtchViews_sp = subpassAtchViews[spIdx];
					for(size_t atchIdx = 0; atchIdx < spAtchViews_sp.size(); ++ atchIdx) {
						assert(atchIdx < spAtchViews_sp.size());
						auto& spAtchViews_a = spAtchViews_sp[atchIdx];
						assert(gframeIdx < spAtchViews_a.size());
						auto imgView = spAtchViews_a[gframeIdx];
						logger.trace("Gframe {} subpass {} attachment {} is {:016x}", gframeIdx, spIdx, framebufferAttachmentList.size(), size_t(imgView));
						framebufferAttachmentList.push_back(imgView);
					}
				}
				fbcInfo.attachmentCount = framebufferAttachmentList.size();
				fbcInfo.pAttachments    = framebufferAttachmentList.data();
				VkFramebuffer fb;
				VK_CHECK(vkCreateFramebuffer, vkDev, &fbcInfo, nullptr, &fb);
				dst->framebuffers[gframeIdx].handle = fb;
			}
		} catch(vkutil::VulkanError&) {
			for(auto fb : dst->framebuffers) {
				if(fb.handle != nullptr) vkDestroyFramebuffer(vkDev, fb.handle, nullptr);
			}
			dst->framebuffers.clear();
			vkDestroyRenderPass(vkDev, dst->handle, nullptr);
			std::rethrow_exception(std::current_exception());
		}
	}


	void destroyRprocRpass(RenderPass* rpass, VmaAllocator vma) {
		auto vkDev = [&](auto vma) { VmaAllocatorInfo i; vmaGetAllocatorInfo(vma, &i); return i.device; } (vma);
		for(auto fb : rpass->framebuffers) {
			if(fb.handle != nullptr) vkDestroyFramebuffer(vkDev, fb.handle, nullptr);
		}
		rpass->framebuffers.clear();
		vkDestroyRenderPass(vkDev, rpass->handle, nullptr);
		*rpass = { };
	}

}
