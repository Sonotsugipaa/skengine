#include "engine.hpp"

#include "init/init.hpp"

#include <posixfio_tl.hpp>

#include <fmamdl/fmamdl.hpp>

#include <vk-util/error.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <random>
#include <deque>



namespace SKENGINE_NAME_NS {
namespace {

	constexpr tickreg::delta_t choose_delta(tickreg::delta_t avg, tickreg::delta_t last) {
		using tickreg::delta_t;
		constexpr auto tolerance_factor = delta_t(1.0) / delta_t(2.0);
		delta_t diff = (avg > last)? (avg - last) : (last - avg);
		if(diff > last * tolerance_factor) avg = last;
		return avg;
	}

}}



struct SKENGINE_NAME_NS::Engine::Implementation {

	using frame_counter_t = decltype(Engine::mGframeSelector);


	static VkFence& selectGframeFence(Engine& e) {
		auto i = (++ e.mGframeSelector) % frame_counter_t(e.mGframes.size());
		auto& r = e.mGframeSelectionFences[i];
		VK_CHECK(vkResetFences, e.mDevice, 1, &r);
		return r;
	}


	template <bool doPrepare, bool doDraw>
	static void recordUiCommands(Engine& e, GframeData& gframe) {
		static_assert(doPrepare != doDraw);

		gui::DrawContext guiCtx = gui::DrawContext {
			.magicNumber = gui::DrawContext::magicNumberValue,
			.engine = &e,
			.prepareCmdBuffer = doPrepare? gframe.cmd_prepare : nullptr,
			.drawJobs = { } };
		ui::DrawContext uiCtx = { &guiCtx };

		std::deque<std::pair<LotId, Lot*>> recursiveVisitList;

		std::function<void(LotId, Lot&)> drawLot = [&](LotId lotId, Lot& lot) {
			if(lot.hasChildGrid()) {
				auto grid = lot.childGrid();
				// // // Disregard the commented comments, grids can set themselves as modified but elements can't;
				// // // making it so that they can would probably be a nightmare.
				// // If the grid's structure has not been modified, there's no need to recursively prepare;
				// // there is, however, always need to recursively draw.
				// if(doDraw || grid->isModified()) {
				// 	for(auto& lot : grid->lots()) drawLot(lotId, *lot.second);
				// 	grid->resetModified();
				// }
				for(auto& lot : grid->lots()) drawLot(lotId, *lot.second);
			}

			recursiveVisitList.push_back({ lotId, &lot });
		};

		for(auto& lot : e.mGuiState.canvas->lots()) {
			drawLot(lot.first, *lot.second);
		}

		if constexpr(doPrepare) {
			std::deque<std::tuple<LotId, Lot*, Element*>> repeatList;
			for(auto& lot : recursiveVisitList) {
				for(auto& elem : lot.second->elements()) {
					auto ps = elem.second->ui_elem_prepareForDraw(lot.first, *lot.second, 0, uiCtx);
					if(ps == ui::Element::PrepareState::eDefer) repeatList.push_back({ lot.first, lot.second, elem.second.get() });
				}
			}
			unsigned repeatCount = 0;
			std::deque<std::tuple<LotId, Lot*, Element*>> repeatListSwap;
			while(! repeatList.empty()) {
				for(auto& row : repeatList) {
					auto ps = std::get<2>(row)->ui_elem_prepareForDraw(std::get<0>(row), *std::get<1>(row), repeatCount, uiCtx);
					if(ps == ui::Element::PrepareState::eDefer) repeatListSwap.push_back(row);
				}
				repeatList = std::move(repeatListSwap);
				++ repeatCount;
			}
		}

		if constexpr(doDraw) {
			for(auto& lot : recursiveVisitList) {
				for(auto& elem : lot.second->elements()) elem.second->ui_elem_draw(lot.first, *lot.second, uiCtx);
			}

			// The caches will need for this draw op to finish before preparing for the next one
			// (unless they're up to date, in which case they won't do anything)
			for(auto& ln : e.mGuiState.textCaches) ln.second.syncWithFence(gframe.fence_draw);

			auto& cmd = gframe.cmd_draw[1];
			VkPipeline             lastPl = nullptr;
			const ViewportScissor* lastVs = nullptr;
			VkDescriptorSet        lastImageDset = nullptr;

			// This lambda ladder turns what follows into something readable:
			//
			//    for(auto& jobPl : guiCtx.drawJobs) {
			//       // ...
			//       for(auto& jobVs : jobPl.second) {
			//          // ...
			//          for(auto& jobDs : jobVs.second) {
			//             // ...
			//             for(auto& job : jobDs.second) {
			//                // ...
			//             }
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
				vkCmdPushConstants(cmd, e.mGuiState.geomPipelines.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(geom::PushConstant), &job.transform);
				vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
				vkCmdDrawIndirect(cmd, shapeSet.drawIndirectBuffer(), 0, shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
			};

			auto forEachDs = [&](JobDs& jobDs) {
				if(lastImageDset != jobDs.first) {
					lastImageDset = jobDs.first;
					if(lastImageDset != nullptr) {
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e.mGuiState.geomPipelines.layout, 0, 1, &lastImageDset, 0, nullptr);
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
	}


	static void setHdrMetadata(Engine& e) {
		// HDR (This is just a stub, apparently HDR isn't a Linux thing yet and `vkSetHdrMetadataEXT` is not defined in the (standard?) Vulkan ICD)
		//
		(void) e;
		// VkHdrMetadataEXT hdr = { };
		// hdr.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
		// hdr.minLuminance = 0.0f;
		// hdr.maxLuminance = 1.0f;
		// hdr.maxFrameAverageLightLevel = 0.7f;
		// hdr.maxContentLightLevel      = 0.7f;
		// hdr.whitePoint                = VkXYColorEXT { 0.3127f, 0.3290f };
		// hdr.displayPrimaryRed         = VkXYColorEXT { 0.6400f, 0.3300f };
		// hdr.displayPrimaryGreen       = VkXYColorEXT { 0.3000f, 0.6000f };
		// hdr.displayPrimaryBlue        = VkXYColorEXT { 0.1500f, 0.0600f };
		// vkSetHdrMetadataEXT(e.mDevice, 1, &e.mSwapchain, &hdr);
	}


	// Returns `false` if the swapchain is out of date
	static bool draw(Engine& e, LoopInterface& loop) {
		GframeData* gframe;
		uint32_t    sc_img_idx = ~ uint32_t(0);
		auto        delta_avg  = e.mGraphicsReg.estDelta();
		auto        delta_last = e.mGraphicsReg.lastDelta();
		auto        delta      = choose_delta(delta_avg, delta_last);
		auto        concurrent_access = ConcurrentAccess(&e, true);

		e.mGraphicsReg.beginCycle();

		{ // Acquire image
			VkFence  sc_img_fence = selectGframeFence(e);
			VkResult res = vkAcquireNextImageKHR(e.mDevice, e.mSwapchain, UINT64_MAX, nullptr, sc_img_fence, &sc_img_idx);
			switch(res) {
				case VK_SUCCESS:
					break;
				case VK_ERROR_OUT_OF_DATE_KHR:
					e.logger().trace("Swapchain is  out of date");
					return false;
				case VK_SUBOPTIMAL_KHR:
					e.logger().trace("Swapchain is suboptimal");
					return false;
				case VK_TIMEOUT:
					e.logger().trace("Swapchain image request timed out");
					return true;
				default:
					assert(res < VkResult(0));
					throw vkutil::VulkanError("vkAcquireNextImage2KHR", res);
			}
			gframe = e.mGframes.data() + sc_img_idx;
			VK_CHECK(vkWaitForFences, e.mDevice, 1, &sc_img_fence,       VK_TRUE, UINT64_MAX);
			VK_CHECK(vkWaitForFences, e.mDevice, 1, &gframe->fence_draw, VK_TRUE, UINT64_MAX);
			VK_CHECK(vkResetCommandPool, e.mDevice, gframe->cmd_pool, 0);
		}

		e.mGframeCounter.fetch_add(1, std::memory_order_relaxed);

		VkCommandBufferBeginInfo cbb_info = { };
		cbb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		for(auto& r : e.mRenderers) r->beforePreRender(concurrent_access, sc_img_idx);
		loop.loop_async_preRender(concurrent_access, delta, delta_last);

		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_prepare, &cbb_info);
		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_draw[0], &cbb_info);
		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_draw[1], &cbb_info);

		e.mRendererMutex.lock();
		{ // Prepare the gframe buffers
			auto& ubo  = *gframe->frame_ubo.mappedPtr<dev::FrameUniform>();
			auto  rng  = std::minstd_rand(std::chrono::steady_clock::now().time_since_epoch().count());
			auto  dist = std::uniform_real_distribution(0.0f, 1.0f);
			for(auto& r : e.mRenderers) r->duringPrepareStage(concurrent_access, sc_img_idx, gframe->cmd_prepare);
			ubo.proj_transf       = e.mProjTransf;
			ubo.projview_transf   = ubo.proj_transf * ubo.view_transf;
			ubo.shade_step_count  = e.mPrefs.shade_step_count;
			ubo.shade_step_smooth = e.mPrefs.shade_step_smoothness;
			ubo.shade_step_exp    = e.mPrefs.shade_step_exponent;
			ubo.dithering_steps   = e.mPrefs.dithering_steps;
			ubo.rnd               = dist(rng);
			ubo.time_delta        = std::float32_t(delta);
			ubo.flags             = dev::FrameUniformFlags(e.mHdrEnabled? dev::FRAME_UNI_ZERO : dev::FRAME_UNI_HDR_ENABLED);
			gframe->frame_ubo.flush(gframe->cmd_prepare, e.mVma);
			recordUiCommands<true, false>(e, *gframe);
		}

		VK_CHECK(vkEndCommandBuffer, gframe->cmd_prepare);

		VkImageMemoryBarrier2 imb[2] = { }; {
			imb[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			imb[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imb[0].subresourceRange.layerCount = 1;
			imb[0].subresourceRange.levelCount = 1;
			imb[1] = imb[0];
		}
		VkDependencyInfo imbDep = { }; {
			imbDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			imbDep.pImageMemoryBarriers = imb;
		}

		{ // Begin the world render pass
			constexpr size_t COLOR = 0;
			constexpr size_t DEPTH = 1;
			constexpr float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.6f };
			VkClearValue clears[2];
			memcpy(clears[COLOR].color.float32, clear_color, 4 * sizeof(float));
			clears[DEPTH].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo rpb_info = { };
			rpb_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpb_info.framebuffer = gframe->worldFramebuffer;
			rpb_info.renderPass  = e.mWorldRpass;
			rpb_info.clearValueCount = 2;
			rpb_info.pClearValues    = clears;
			rpb_info.renderArea      = { VkOffset2D { 0, 0 }, e.mRenderExtent };

			vkCmdBeginRenderPass(gframe->cmd_draw[0], &rpb_info, VK_SUBPASS_CONTENTS_INLINE);
		}

		{ // Draw the objects
			auto& cmd = gframe->cmd_draw[0];

			VkViewport viewport = { }; {
				viewport.x      = 0.0f;
				viewport.y      = 0.0f;
				viewport.width  = e.mRenderExtent.width;
				viewport.height = e.mRenderExtent.height;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
			}

			VkRect2D scissor = { }; {
				scissor.offset = { };
				scissor.extent = { e.mRenderExtent.width, e.mRenderExtent.height };
			}

			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
			for(auto& r : e.mRenderers) r->duringDrawStage(concurrent_access, sc_img_idx, cmd);
		}

		vkCmdEndRenderPass(gframe->cmd_draw[0]);

		{ // Barrier the color attachment and swapchain images for transfer
			imb[0].image = gframe->atch_color;
			imb[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imb[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imb[0].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imb[0].dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			imb[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imb[0].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imb[1].image = gframe->swapchain_image;
			imb[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imb[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imb[1].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imb[1].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			imb[1].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imb[1].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imbDep.imageMemoryBarrierCount = 2;
			vkCmdPipelineBarrier2(gframe->cmd_draw[0], &imbDep);
		}

		{ // Blit the image
			VkImageBlit2 region = { };
			region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
			region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.srcSubresource.layerCount = 1;
			region.srcOffsets[1] = { int32_t(e.mRenderExtent.width), int32_t(e.mRenderExtent.height), 1 };
			region.dstSubresource = region.srcSubresource;
			region.dstOffsets[1] = { int32_t(e.mPresentExtent.width), int32_t(e.mPresentExtent.height), 1 };
			VkBlitImageInfo2 blit = { };
			blit.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
			blit.srcImage       = gframe->atch_color;
			blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			blit.dstImage       = gframe->swapchain_image;
			blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			blit.filter = VK_FILTER_NEAREST;
			blit.regionCount = 1;
			blit.pRegions = &region;
			vkCmdBlitImage2(gframe->cmd_draw[0], &blit);
		}

		VK_CHECK(vkEndCommandBuffer, gframe->cmd_draw[0]);

		{ // Barrier the swapchain image [0] for drawing the UI, and the color attachment [1] for... color attaching?
			imb[0].image = gframe->swapchain_image;
			imb[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imb[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imb[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			imb[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
			imb[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imb[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imbDep.imageMemoryBarrierCount = 1;
			vkCmdPipelineBarrier2(gframe->cmd_draw[1], &imbDep);
		}

		{ // Begin the ui render pass
			VkRenderPassBeginInfo rpb_info = { };
			rpb_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpb_info.framebuffer = gframe->uiFramebuffer;
			rpb_info.renderPass  = e.mUiRpass;
			rpb_info.renderArea  = { VkOffset2D { 0, 0 }, e.mRenderExtent };
			vkCmdBeginRenderPass(gframe->cmd_draw[1], &rpb_info, VK_SUBPASS_CONTENTS_INLINE);
		}

		recordUiCommands<false, true>(e, *gframe);

		vkCmdEndRenderPass(gframe->cmd_draw[1]);

		{ // Barrier the swapchain image for presenting
			imb[0].image = gframe->swapchain_image;
			imb[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imb[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			imb[0].srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imb[0].dstAccessMask = VK_ACCESS_2_NONE;
			imb[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imb[0].dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			imbDep.imageMemoryBarrierCount = 1;
			vkCmdPipelineBarrier2(gframe->cmd_draw[1], &imbDep);
		}

		VK_CHECK(vkEndCommandBuffer, gframe->cmd_draw[1]);

		VkSubmitInfo subm = { };

		{ // Submit the prepare and draw commands
			constexpr VkPipelineStageFlags waitStages[3] = {
				0,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT };
			subm.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			subm.commandBufferCount = 1;
			subm.pCommandBuffers    = &gframe->cmd_prepare;
			subm.pWaitDstStageMask  = waitStages + 0;
			subm.signalSemaphoreCount = 1;
			subm.pSignalSemaphores    = &gframe->sem_prepare;
			VK_CHECK(vkResetFences, e.mDevice,          1,       &gframe->fence_prepare);
			VK_CHECK(vkQueueSubmit, e.mQueues.graphics, 1, &subm, gframe->fence_prepare);
			VkSemaphore drawSems[3] = { gframe->sem_prepare, gframe->sem_drawWorld, gframe->sem_drawGui };
			subm.waitSemaphoreCount = 1;
			subm.pCommandBuffers    = gframe->cmd_draw + 0;
			subm.pWaitDstStageMask  = waitStages       + 1;
			subm.pWaitSemaphores    = drawSems         + 0;
			subm.pSignalSemaphores  = drawSems         + 1;
			VK_CHECK(vkQueueSubmit, e.mQueues.graphics, 1, &subm, nullptr);
			subm.pCommandBuffers    = gframe->cmd_draw + 1;
			subm.pWaitDstStageMask  = waitStages       + 2;
			subm.pWaitSemaphores    = drawSems         + 1;
			subm.pSignalSemaphores  = drawSems         + 2;
			VK_CHECK(vkResetFences, e.mDevice,          1,       &gframe->fence_draw);
			VK_CHECK(vkQueueSubmit, e.mQueues.graphics, 1, &subm, gframe->fence_draw);
		}

		setHdrMetadata(e);

		{ // Here's a present!
			VkResult res;
			VkPresentInfoKHR p_info = { };
			p_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			p_info.pResults = &res;
			p_info.swapchainCount = 1;
			p_info.pSwapchains    = &e.mSwapchain;
			p_info.pImageIndices  = &sc_img_idx;
			p_info.waitSemaphoreCount = 1;
			p_info.pWaitSemaphores    = &gframe->sem_drawGui;
			VK_CHECK(vkQueuePresentKHR, e.mPresentQueue, &p_info);
		}

		for(auto& r : e.mRenderers) r->afterPresent(concurrent_access, sc_img_idx);

		e.mGframeLast = int_fast32_t(sc_img_idx);
		if(e.mPrefs.wait_for_gframe && (e.mGframeLast >= 0)) {
			VkFence fences[2] = { gframe->fence_prepare, e.mGframes[e.mGframeLast].fence_draw };
			VK_CHECK(vkWaitForFences, e.mDevice, std::size(fences), fences, true, UINT64_MAX);
		} else {
			VK_CHECK(vkWaitForFences, e.mDevice, 1, &gframe->fence_prepare, true, UINT64_MAX);
		}
		e.mRendererMutex.unlock();

		for(auto& r : e.mRenderers) r->beforePostRender(concurrent_access, sc_img_idx);
		loop.loop_async_postRender(concurrent_access, delta, e.mGraphicsReg.lastDelta());
		for(auto& r : e.mRenderers) r->afterPostRender(concurrent_access, sc_img_idx);

		e.mGraphicsReg.endCycle();

		for(auto& ln : e.mGuiState.textCaches) ln.second.trimChars(e.mPrefs.font_max_cache_size);

		return true;
	}


	static LoopInterface::LoopState runLogicIteration(Engine& e, LoopInterface& loop) {
		e.mLogicReg.beginCycle();
		auto delta_last = e.mLogicReg.lastDelta();
		auto delta = choose_delta(e.mLogicReg.estDelta(), delta_last);

		e.mGframePriorityOverride.store(true, std::memory_order_seq_cst);
		loop.loop_processEvents(delta, delta_last);
		e.mGframePriorityOverride.store(false, std::memory_order_seq_cst);
		e.mGframeResumeCond.notify_one();

		e.mLogicReg.endCycle();

		auto r = loop.loop_pollState();

		e.mLogicReg.awaitNextTick();
		return r;
	}

};



namespace SKENGINE_NAME_NS {

	const EnginePreferences EnginePreferences::default_prefs = EnginePreferences {
		.phys_device_uuid      = "",
		.asset_filename_prefix = "",
		.font_location         = "font.otf",
		.init_present_extent   = { 600, 400 },
		.max_render_extent     = { 0, 0 },
		.present_mode          = VK_PRESENT_MODE_FIFO_KHR,
		.sample_count          = VK_SAMPLE_COUNT_1_BIT,
		.max_concurrent_frames = 2,
		.framerate_samples     = 16,
		.fov_y                 = glm::radians(110.0f),
		.z_near                = 1.0f / float(1 << 6),
		.z_far                 = float(1 << 10),
		.shade_step_count      = 0,
		.shade_step_smoothness = 0.0f,
		.shade_step_exponent   = 1.0f,
		.dithering_steps       = 256.0f,
		.upscale_factor        = 1.0f,
		.target_framerate      = 60.0f,
		.target_tickrate       = 60.0f,
		.font_max_cache_size   = 512,
		.fullscreen            = false,
		.composite_alpha       = false,
		.wait_for_gframe       = true
	};


	constexpr auto regulator_params = tickreg::RegulatorParams {
		.deltaTolerance     = 0.2,
		.burstTolerance     = 0.05,
		.compensationFactor = 0.0,
		.strategyMask       = tickreg::strategy_flag_t(tickreg::WaitStrategyFlags::eSleepUntil) };



	TextCache& GuiState::getTextCache(Engine& e, unsigned short size) {
		using Caches = decltype(e.mGuiState.textCaches);
		auto& caches = e.mGuiState.textCaches;
		auto found = caches.find(size);
		if(found == caches.end()) {
			found = caches.insert(Caches::value_type(size, TextCache(
				e.mDevice, e.mVma,
				e.mGuiDsetLayout,
				std::make_shared<FontFace>(e.createFontFace()),
				size ))).first;
		}
		return found->second;
	}


	void ConcurrentAccess::setPresentExtent(VkExtent2D ext) {
		auto reinit = [&]() {
			using tickreg::delta_t;
			auto lock = ca_engine->pauseRenderPass();
			ca_engine->mPrefs.init_present_extent = ext;

			// Some compositors resize the window as soon as it appears, and this seems to cause problems
			ca_engine->mGraphicsReg.resetEstimates (delta_t(1.0) / delta_t(ca_engine->mPrefs.target_framerate));
			ca_engine->mLogicReg.resetEstimates    (delta_t(1.0) / delta_t(ca_engine->mPrefs.target_tickrate));

			auto init = reinterpret_cast<Engine::RpassInitializer*>(ca_engine);
			init->reinit(*this);
		};

		// Only unlock/relock the renderer mutex if the access doesn't happen on the graphics thread
		if(ca_threadLocal) {
			reinit();
		} else {
			ca_engine->mRendererMutex.unlock();
			reinit();
			ca_engine->mRendererMutex.lock();
		}
	}



	Engine::Engine(
			const DeviceInitInfo&    di,
			const EnginePreferences& ep,
			std::shared_ptr<ShaderCacheInterface> sci,
			std::shared_ptr<AssetSourceInterface> asi,
			std::shared_ptr<spdlog::logger>       logger
	):
		mShaderCache(std::move(sci)),
		mGraphicsReg(
			ep.framerate_samples,
			decltype(ep.target_framerate)(1.0) / ep.target_framerate,
			tickreg::WaitStrategyState::eSleepUntil,
			regulator_params ),
		mLogicReg(
			ep.framerate_samples,
			decltype(ep.target_tickrate)(1.0) / ep.target_tickrate,
			tickreg::WaitStrategyState::eSleepUntil,
			regulator_params ),
		mGframeCounter(0),
		mGframeSelector(0),
		mAssetSource(std::move(asi)),
		mLogger([&]() {
			decltype(mLogger) r;
			if(logger) {
				r = std::move(logger);
			} else {
				r = std::make_shared<spdlog::logger>(
					SKENGINE_NAME_CSTR,
					std::make_shared<spdlog::sinks::stdout_color_sink_mt>(spdlog::color_mode::automatic) );
				r->set_pattern("[%^" SKENGINE_NAME_CSTR " %L%$] %v");
				#ifdef NDEBUG
					r->set_level(spdlog::level::info);
				#else
					r->set_level(spdlog::level::debug);
				#endif
			}
			return r;
		} ()),
		mPrefs(ep)
	{
		{
			auto init = reinterpret_cast<Engine::DeviceInitializer*>(this);
			init->init(&di);
		}

		{
			auto rpass_cfg = RpassConfig::default_cfg;
			auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
			auto ca = ConcurrentAccess(this, true);
			init->init(ca, rpass_cfg);
		}
	}


	Engine::~Engine() {
		mShaderCache->shader_cache_releaseAllModules(*this);

		{
			auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
			auto ca = ConcurrentAccess(this, true);
			init->destroy(ca);
		}

		{
			auto init = reinterpret_cast<Engine::DeviceInitializer*>(this);
			init->destroy();
		}
	}


	template <>
	VkShaderModule Engine::createShaderModuleFromMemory(
			std::span<const uint32_t> code
	) {
		VkShaderModuleCreateInfo sm_info = { };
		sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		sm_info.pCode    = code.data();
		sm_info.codeSize = code.size_bytes();
		VkShaderModule r;
		VK_CHECK(vkCreateShaderModule, mDevice, &sm_info, nullptr, &r);
		logger().trace("Loaded shader module from memory");
		return r;
	}


	VkShaderModule Engine::createShaderModuleFromFile(
			const std::string& file_path
	) {
		static_assert(4 == sizeof(uint32_t));

		VkShaderModuleCreateInfo    sm_info = { };
		std::unique_ptr<uint32_t[]> buffer;
		try {
			auto file    = posixfio::File::open(file_path.c_str(), O_RDONLY);
			size_t lsize = file.lseek(0, SEEK_END);
			if(lsize > UINT32_MAX) throw ShaderModuleReadError("Shader file is too long");
			if(lsize % 4 != 0)     throw ShaderModuleReadError("Misaligned shader file size");
			file.lseek(0, SEEK_SET);
			buffer    = std::make_unique_for_overwrite<uint32_t[]>(lsize / 4);
			size_t rd = posixfio::readAll(file, buffer.get(), lsize);
			if(rd != lsize) throw ShaderModuleReadError("Shader file partially read");
			sm_info.codeSize = uint32_t(lsize);
		} catch(const posixfio::FileError& e) {
			switch(e.errcode) {
				using namespace std::string_literals;
				case ENOENT: throw ShaderModuleReadError("Shader file not found: \""s      + file_path + "\""s); break;
				case EACCES: throw ShaderModuleReadError("Shader file not accessible: \""s + file_path + "\""s); break;
				default: throw e;
			}
		}
		sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		sm_info.pCode = buffer.get();
		VkShaderModule r;
		VK_CHECK(vkCreateShaderModule, mDevice, &sm_info, nullptr, &r);
		logger().trace("Loaded shader module from file \"{}\"", file_path);
		return r;
	}


	void Engine::destroyShaderModule(VkShaderModule module) {
		vkDestroyShaderModule(mDevice, module, nullptr);
	}


	FontFace Engine::createFontFace() {
		return FontFace::fromFile(mFreetype, false, mPrefs.font_location.c_str());
	}


	void Engine::run(LoopInterface& loop) {
		auto loop_state = loop.loop_pollState();
		auto exception  = std::exception_ptr(nullptr);

		auto handle_exception = [&]() {
			auto lock = std::unique_lock(mGframeMutex, std::try_to_lock);
			exception = std::current_exception();
		};

		mGraphicsThread = std::thread([&]() {
			while(loop_state != LoopInterface::LoopState::eShouldStop) {
				try {
					auto gframeLock = std::unique_lock(mGframeMutex, std::defer_lock);
					if(mGframePriorityOverride.load(std::memory_order_consume)) [[unlikely]] {
						gframeLock.lock();
						mGframeResumeCond.wait(gframeLock);
					} else {
						gframeLock.lock(); // This must be done for both branches, but before waiting on the cond var AND after checking the atomic var
					}
					bool swapchain_ood = ! Implementation::draw(*this, loop);
					if(swapchain_ood) {
						// Some compositors resize the window as soon as it appears, and this seems to cause problems
						auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
						auto ca = ConcurrentAccess(this, true);
						init->reinit(ca);
					}
					gframeLock.unlock();
					mGraphicsReg.awaitNextTick();
				} catch(...) {
					handle_exception();
				}
			}
		});

		while((exception == nullptr) && (loop_state != LoopInterface::LoopState::eShouldStop)) {
			try {
				loop_state = Implementation::runLogicIteration(*this, loop);
			} catch(...) {
				handle_exception();
			}
		}

		assert(mGraphicsThread.joinable());
		auto lock = pauseRenderPass();
		mGframePriorityOverride.store(false, std::memory_order_seq_cst);
		mGframeResumeCond.notify_one();
		lock.unlock();
		mGraphicsThread.join();
		mGraphicsThread = { };

		if(exception) {
			std::rethrow_exception(exception);
		}
	}


	std::unique_lock<std::mutex> Engine::pauseRenderPass() {
		auto lock = std::unique_lock(mGframeMutex, std::defer_lock_t());

		auto wait_for_fences = [&]() {
			// Wait for all fences, in this order: selection -> prepare -> draw
			std::vector<VkFence> fences;
			for(auto& gff : mGframeSelectionFences) VK_CHECK(vkWaitForFences, mDevice, 1, &gff, true, UINT64_MAX);
			fences.reserve(mGframes.size());
			for(auto& gframe : mGframes) fences.push_back(gframe.fence_prepare);
			VK_CHECK(vkWaitForFences, mDevice, fences.size(), fences.data(), true, UINT64_MAX);
			fences.clear();
			fences.reserve(mGframes.size());
			for(auto& gframe : mGframes) fences.push_back(gframe.fence_draw);
			VK_CHECK(vkWaitForFences, mDevice, fences.size(), fences.data(), true, UINT64_MAX);

			// Text caches no longer need synchronization, and MUST forget about potentially soon-to-be deleted fences
			for(auto& ln : mGuiState.textCaches) ln.second.forgetFence();
		};

		bool is_graphics_thread = std::this_thread::get_id() == mGraphicsThread.get_id();
		if(! is_graphics_thread) {
			mGframePriorityOverride.store(true, std::memory_order_seq_cst);
			lock.lock();
			wait_for_fences();
			mGframePriorityOverride.store(false, std::memory_order_seq_cst);
			mGframeResumeCond.notify_one();
		} else {
			wait_for_fences();
		}
		VK_CHECK(vkDeviceWaitIdle, mDevice);

		return lock;
	}


	MutexAccess<ConcurrentAccess> Engine::getConcurrentAccess() noexcept {
		assert(mGraphicsThread.get_id() != std::this_thread::get_id() && "This *will* cause a deadlock");
		auto ca = ConcurrentAccess(this, false);
		return MutexAccess(std::move(ca), mRendererMutex);
	}

}
