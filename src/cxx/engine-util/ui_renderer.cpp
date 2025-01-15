#include "ui_renderer.hpp"

#include "gui.hpp"

#include <engine/engine.hpp>
#include <engine/draw-geometry/core.hpp>

#include <vk-util/error.hpp>

#include <set>



namespace SKENGINE_NAME_NS {

	namespace ui { namespace {

		#define B_(BINDING_, DSET_N_, DSET_T_, STAGES_) VkDescriptorSetLayoutBinding { .binding = BINDING_, .descriptorType = DSET_T_, .descriptorCount = DSET_N_, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
		constexpr VkDescriptorSetLayoutBinding ui_dset_layout_bindings[] = {
		B_(0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) };
		#undef B_

		constexpr ShaderRequirement ui_shreq[] = {
			{ .name = "shape-fill",    .pipelineLayout = PipelineLayoutId::eGeometry },
			{ .name = "shape-outline", .pipelineLayout = PipelineLayoutId::eGeometry },
			{ .name = "text",          .pipelineLayout = PipelineLayoutId::eImage } };

		#define PI_ Renderer::PipelineInfo
		constexpr auto ui_renderer_shape_subpass_info = PI_ {
			.dsetLayoutBindings = PI_::DsetLayoutBindings::referenceTo(ui_dset_layout_bindings) };
		#undef PI_


		template <typename visit_fn_tp>
		void visitUi(
			ui::Canvas& canvas,
			visit_fn_tp visitFn
		) {
			using DfsList = std::deque<std::pair<LotId, Lot*>>;
			DfsList dfsQueue;

			for(auto& lot : canvas.lots()) dfsQueue.emplace_back(lot.first, lot.second.get());

			while(! dfsQueue.empty()) {
				auto extracted = std::move(dfsQueue.back());
				visitFn(extracted.first, *extracted.second);
				dfsQueue.pop_back();
				if(extracted.second->hasChildGrid()) {
					auto child = extracted.second->childGrid();
					for(auto& lot : child->lots()) dfsQueue.emplace_back(lot.first, lot.second.get());
				}
			}
		}


		VkDescriptorSetLayout createDsetLayout(VkDevice dev) {
			VkDescriptorSetLayoutBinding dslb[1] = { };
			dslb[0].binding = 0;
			dslb[0].descriptorCount = 1;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			dslb[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo dslc_info = { };
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslc_info.bindingCount = std::size(dslb);
			dslc_info.pBindings = dslb;

			VkDescriptorSetLayout r = { };
			VK_CHECK(vkCreateDescriptorSetLayout, dev, &dslc_info, nullptr, &r);
			return r;
		}


		void destroyDsetLayout(VkDevice dev, VkDescriptorSetLayout dsetLayout) {
			vkDestroyDescriptorSetLayout(dev, dsetLayout, nullptr);
		}

	}}



	const UiRenderer::RdrParams UiRenderer::RdrParams::defaultParams = UiRenderer::RdrParams {
		.fontLocation = "font.otf",
		.fontMaxCacheSize = 512
	};


	UiRenderer::UiRenderer(): Renderer(ui_renderer_shape_subpass_info) { mState.initialized = false; }

	UiRenderer::UiRenderer(UiRenderer&& mv):
		Renderer(ui_renderer_shape_subpass_info),
		mState(std::move(mv.mState))
	{
		mv.mState.initialized = false;
	}

	UiRenderer::~UiRenderer() {
		if(mState.initialized) {
			UiRenderer::destroy(*this);
			mState.initialized = false;
		}
	}


	UiRenderer UiRenderer::create(
		VmaAllocator vma,
		RdrParams rdrParams,
		Logger logger
	) {
		UiRenderer r;
		r.mState.logger = std::move(logger);
		r.mState.vma = vma;
		r.mState.rdrParams = std::move(rdrParams);
		r.mState.pipelines = { };
		r.mState.srcRtarget = idgen::invalidId<RenderTargetId>();
		r.mState.initialized = true;
		auto dev = vmaGetAllocatorDevice(r.mState.vma);

		{ // Init freetype
			auto error = FT_Init_FreeType(&r.mState.freetype);
			if(error) throw FontError("failed to initialize FreeType", error);
		}

		r.mState.dsetLayout = createDsetLayout(dev);

		{ // Pipeline layout
			VkPipelineLayoutCreateInfo plcInfo = { };
			VkPushConstantRange pcRanges[1] = { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(geom::PushConstant) } };
			plcInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plcInfo.setLayoutCount = 1;
			plcInfo.pSetLayouts    = &r.mState.dsetLayout;
			plcInfo.pushConstantRangeCount = std::size(pcRanges);
			plcInfo.pPushConstantRanges = pcRanges;
			VK_CHECK(vkCreatePipelineLayout, dev, &plcInfo, nullptr, &r.mState.pipelineLayout);
		}

		{ // Hardcoded GUI canvas
			float ratio = 1.0f;
			float hSize = 0.1f;
			float wSize = hSize * ratio;
			float wComp = 0.5f * (hSize - wSize);
			float chBlank = (1.0 - hSize) / 2.0;
			auto& canvas = r.mState.canvas;
			canvas = std::make_unique<ui::Canvas>(ComputedBounds { 0.01, 0.01, 0.98, 0.98 });
			canvas->setRowSizes    ({ chBlank,       hSize, chBlank });
			canvas->setColumnSizes ({ chBlank+wComp, wSize, chBlank+wComp });
		}

		return r;
	}


	void UiRenderer::destroy(UiRenderer& r) {
		assert(r.mState.initialized);
		auto* vma = r.mState.vma;
		auto dev = vmaGetAllocatorDevice(r.mState.vma);

		for(auto& gf : r.mState.gframes) {
			(void) gf; // NOP
			(void) vma;
		}
		r.mState.gframes.clear();

		vkDestroyPipelineLayout(dev, r.mState.pipelineLayout, nullptr);

		destroyDsetLayout(dev, r.mState.dsetLayout);

		r.mState.textCaches.clear();
		r.mState.canvas = { };
		FT_Done_FreeType(r.mState.freetype);

		r.mState.initialized = false;
	}


	void UiRenderer::prepareSubpasses(const SubpassSetupInfo& ssInfo, VkPipelineCache plCache, ShaderCacheInterface*) {
		assert(mState.pipelines.polyLine == nullptr);
		assert(mState.pipelines.polyFill == nullptr);
		assert(mState.pipelines.text == nullptr);
		auto dev = vmaGetAllocatorDevice(mState.vma);
		PipelineSetCreateInfo pscInfo = {
			.renderPass = ssInfo.rpass,
			.subpass = 0,
			.pipelineCache = plCache,
			.pipelineLayout = mState.pipelineLayout,
			.polyDsetLayout = nullptr,
			.textDsetLayout = mState.dsetLayout };
		mState.pipelines = geom::PipelineSet::create(dev, pscInfo);
	}


	void UiRenderer::forgetSubpasses(const SubpassSetupInfo&) {
		auto dev = vmaGetAllocatorDevice(mState.vma);
		forgetTextCacheFences(); // This call should happen between gframes, so text cache fences should be free to be forgotten
		geom::PipelineSet::destroy(dev, mState.pipelines);
		mState.pipelines = { };
	}


	void UiRenderer::afterSwapchainCreation(ConcurrentAccess&, unsigned gframeCount) {
		size_t oldGframeCount = mState.gframes.size();

		auto setGframeData = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];
			(void) wgf; // NOP
		};

		auto unsetGframeData = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];
			(void) wgf; // NOP
		};

		if(oldGframeCount < gframeCount) {
			mState.gframes.resize(gframeCount);
			for(size_t i = oldGframeCount; i < gframeCount; ++i) setGframeData(i);
		} else
		if(oldGframeCount > gframeCount) {
			for(size_t i = gframeCount; i < oldGframeCount; ++i) unsetGframeData(i);
			mState.gframes.resize(gframeCount);
		}
	}


	void UiRenderer::duringPrepareStage(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		auto& e = ca.engine();

		gui::DrawContext guiCtx = gui::DrawContext {
			.magicNumber = gui::DrawContext::magicNumberValue,
			.engine = &e,
			.uiRenderer = this,
			.prepareCmdBuffer = cmd,
			.drawCmdBuffer = nullptr,
			.drawJobs = { } };
		ui::DrawContext uiCtx = { &guiCtx };

		std::deque<std::tuple<LotId, Lot*, Element*>> repeatList;
		std::deque<std::tuple<LotId, Lot*, Element*>> repeatListSwap;
		unsigned repeatCount = 1;

		auto swapchainImg = ca.getGframeData(drawInfo.gframeIndex).swapchain_image;

		{ // Barrier the swapchain image for transfer
			VkImageMemoryBarrier2 imb { };
			imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imb.subresourceRange.layerCount = 1;
			imb.subresourceRange.levelCount = 1;
			imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imb.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			imb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imb.image = swapchainImg;
			VkDependencyInfo imbDep = { };
			imbDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			imbDep.pImageMemoryBarriers = &imb;
			imbDep.imageMemoryBarrierCount = 1;
			vkCmdPipelineBarrier2(cmd, &imbDep);
		} { // Blit the image
			auto& renderExt = ca.engine().getRenderExtent();
			auto& presentExt = ca.engine().getPresentExtent();
			VkImageBlit2 region = { };
			region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
			region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.srcSubresource.layerCount = 1;
			region.srcOffsets[1] = { int32_t(renderExt.width), int32_t(renderExt.height), 1 };
			region.dstSubresource = region.srcSubresource;
			region.dstOffsets[1] = { int32_t(presentExt.width), int32_t(presentExt.height), 1 };
			VkBlitImageInfo2 blit = { };
			blit.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
			blit.srcImage       = ca.getRenderProcess().getRenderTarget(mState.srcRtarget, drawInfo.gframeIndex).devImage;
			blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			blit.dstImage       = swapchainImg;
			blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			blit.filter = VK_FILTER_NEAREST;
			blit.regionCount = 1;
			blit.pRegions = &region;
			vkCmdBlitImage2(cmd, &blit);
		} { // Barrier the swapchain image for drawing
			VkImageMemoryBarrier2 imb { };
			imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imb.subresourceRange.layerCount = 1;
			imb.subresourceRange.levelCount = 1;
			imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			imb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imb.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imb.image = swapchainImg;
			VkDependencyInfo imbDep = { };
			imbDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			imbDep.pImageMemoryBarriers = &imb;
			imbDep.imageMemoryBarrierCount = 1;
			vkCmdPipelineBarrier2(cmd, &imbDep);
		}

		ui::visitUi(*mState.canvas, [&](LotId lotId, Lot& lot) {
			for(auto& elem : lot.elements()) {
				auto ps = elem.second->ui_elem_prepareForDraw(lotId, lot, 0, uiCtx);
				if(ps == ui::Element::PrepareState::eDefer) repeatList.push_back({ lotId, &lot, elem.second.get() });
			}
		});

		while(! repeatList.empty()) {
			for(auto& row : repeatList) {
				auto ps = std::get<2>(row)->ui_elem_prepareForDraw(std::get<0>(row), *std::get<1>(row), repeatCount, uiCtx);
				if(ps == ui::Element::PrepareState::eDefer) repeatListSwap.push_back(row);
			}
			repeatList = std::move(repeatListSwap);
			++ repeatCount;
		}
	}


	void UiRenderer::duringDrawStage(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		gui::DrawContext guiCtx = gui::DrawContext {
			.magicNumber = gui::DrawContext::magicNumberValue,
			.engine = &ca.engine(),
			.uiRenderer = this,
			.prepareCmdBuffer = nullptr,
			.drawCmdBuffer = cmd,
			.drawJobs = { } };
		ui::DrawContext uiCtx = { &guiCtx };

		ui::visitUi(*mState.canvas, [&](LotId lotId, Lot& lot) {
			for(auto& elem : lot.elements()) elem.second->ui_elem_draw(lotId, lot, uiCtx);
		});

		// The caches will need for this draw op to finish before preparing for the next one
		// (unless they're up to date, in which case they won't do anything)
		for(auto& ln : mState.textCaches) ln.second.syncWithFence(drawInfo.syncPrimitives.fences.draw);

		VkPipeline             lastPl = nullptr;
		const ViewportScissor* lastVs = nullptr;
		VkDescriptorSet        lastImageDset = nullptr;
		VkPipelineLayout       geomPipelineLayout = mState.pipelineLayout;

		// This lambda ladder turns what follows into something less indented:
		//
		//    for(auto&          jobPl : guiCtx.drawJobs) {
		//       for(auto&       jobVs : jobPl.second) {
		//          for(auto&    jobDs : jobVs.second) {
		//             for(auto& job   : jobDs.second) { }
		//          }
		//       }
		//    }
		//
		// Note that the JobPl->JobVs->JobDs order appears to be reversed, but it isn't.
		using JobPl = gui::DrawJobSet::value_type;
		using JobVs = gui::DrawJobVsSet::value_type;
		using JobDs = gui::DrawJobDsetSet::value_type;
		auto forEachJob = [&](gui::DrawJob& job) {
			auto& shapeSet = *job.shapeSet;
			VkBuffer vtx_buffers[] = { shapeSet.vertexBuffer(), shapeSet.vertexBuffer() };
			VkDeviceSize offsets[] = { shapeSet.instanceCount() * sizeof(geom::Instance), 0 };
			vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(geom::PushConstant), &job.transform);
			vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
			vkCmdDrawIndirect(cmd, shapeSet.drawIndirectBuffer(), 0, shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
		};

		auto forEachDs = [&](JobDs& jobDs) {
			if(lastImageDset != jobDs.first) {
				lastImageDset = jobDs.first;
				if(lastImageDset != nullptr) {
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 0, 1, &lastImageDset, 0, nullptr);
				}
			}
			for(auto& job : jobDs.second) forEachJob(job);
		};

		auto forEachVs = [&](JobVs& jobVs) {
			if(lastVs != &jobVs.first) {
				lastVs = &jobVs.first;
				vkCmdSetViewport(cmd, 0, 1, &lastVs->viewport);
				vkCmdSetScissor(cmd, 0, 1, &lastVs->scissor);
			}
			for(auto& jobDs : jobVs.second) forEachDs(jobDs);
		};

		auto forEachPl = [&](JobPl& jobPl) {
			if(lastPl != jobPl.first) {
				lastPl = jobPl.first;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lastPl);
			}
			for(auto& jobVs : jobPl.second) forEachVs(jobVs);
		};

		for(auto& jobPl : guiCtx.drawJobs) forEachPl(jobPl);
	}


	void UiRenderer::afterRenderPass(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		// Barrier the swapchain image for presenting
		VkImageMemoryBarrier2 imb = { };
		imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imb.subresourceRange.layerCount = 1;
		imb.subresourceRange.levelCount = 1;
		imb.image = ca.getGframeData(drawInfo.gframeIndex).swapchain_image;
		imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		imb.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		imb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		imb.dstAccessMask = VK_ACCESS_2_NONE;
		imb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		imb.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		VkDependencyInfo imbDep = { };
		imbDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		imbDep.pImageMemoryBarriers = &imb;
		imbDep.imageMemoryBarrierCount = 1;
		vkCmdPipelineBarrier2(cmd, &imbDep);
	}


	void UiRenderer::afterPostRender(ConcurrentAccess&, const DrawInfo&) {
		trimTextCaches(mState.rdrParams.fontMaxCacheSize);
	}


	FontFace UiRenderer::createFontFace() {
		return FontFace::fromFile(mState.freetype, false, mState.rdrParams.fontLocation.c_str());
	}


	TextCache& UiRenderer::getTextCache(unsigned short size) {
		using Caches = decltype(mState.textCaches);
		auto& caches = mState.textCaches;
		auto dev = vmaGetAllocatorDevice(mState.vma);
		auto found = caches.find(size);
		if(found == caches.end()) {
			found = caches.insert(Caches::value_type(size, TextCache(
				dev, mState.vma,
				mState.dsetLayout,
				std::make_shared<FontFace>(createFontFace()),
				size ))).first;
		}
		return found->second;
	}


	void UiRenderer::trimTextCaches(codepoint_t maxCharCount) {
		for(auto& ln : mState.textCaches) ln.second.trimChars(maxCharCount);
	}


	void UiRenderer::forgetTextCacheFences() noexcept {
		for(auto& ln : mState.textCaches) ln.second.forgetFence();
	}

}
