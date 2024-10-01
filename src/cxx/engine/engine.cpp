#include "engine.hpp"
#include "debug.inl.hpp"

#include "init/init.hpp"

#include <posixfio_tl.hpp>

#include <fmamdl/fmamdl.hpp>

#include <vk-util/error.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <deque>

#include <vulkan/vk_enum_string_helper.h>



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


	static void draw(Engine& e, LoopInterface& loop) {
		#define IF_WORLD_RPASS_(RENDERER_) if(RENDERER_->pipelineInfo().rpass == Renderer::RenderPass::eWorld)
		#define IF_UI_RPASS_(RENDERER_)    if(RENDERER_->pipelineInfo().rpass == Renderer::RenderPass::eUi)
		GframeData* gframe;
		uint32_t    sc_img_idx = ~ uint32_t(0);
		auto        delta_avg  = e.mGraphicsReg.estDelta();
		auto        delta_last = e.mGraphicsReg.lastDelta();
		auto        delta      = choose_delta(delta_avg, delta_last);
		auto        concurrent_access = ConcurrentAccess(&e, true);

		e.mGraphicsReg.beginCycle();

		{ // Acquire image
			VkFence sc_img_fence;
			try {
				sc_img_fence = selectGframeFence(e);
			} catch(vkutil::VulkanError& err) {
				auto str = std::string_view(string_VkResult(err.vkResult()));
				e.mLogger->error("Failed to acquire gframe fence ({})", str);
				return;
			}
			VkResult res = vkAcquireNextImageKHR(e.mDevice, e.mSwapchain, UINT64_MAX, nullptr, sc_img_fence, &sc_img_idx);
			switch(res) {
				case VK_SUCCESS:
					break;
				case VK_ERROR_OUT_OF_DATE_KHR:
					e.logger().trace("Swapchain is out of date");
					e.signal(Signal::eReinit);
					return;
				case VK_SUBOPTIMAL_KHR:
					e.logger().trace("Swapchain is suboptimal");
					e.signal(Signal::eReinit);
					break;
				case VK_TIMEOUT:
					e.logger().trace("Swapchain image request timed out");
					return;
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
		gframe->frame_delta = delta;

		for(auto& r : e.mRenderers) r->beforePreRender(concurrent_access, sc_img_idx);
		loop.loop_async_preRender(concurrent_access, delta, delta_last);

		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_prepare, &cbb_info);
		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_draw[0], &cbb_info);
		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_draw[1], &cbb_info);

		e.mRendererMutex.lock();
		for(auto& r : e.mRenderers) r->duringPrepareStage(concurrent_access, sc_img_idx, gframe->cmd_prepare);

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
			for(auto& r : e.mRenderers) IF_WORLD_RPASS_(r) r->duringDrawStage(concurrent_access, sc_img_idx, cmd);
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
			rpb_info.renderArea  = { VkOffset2D { 0, 0 }, e.mPresentExtent };
			vkCmdBeginRenderPass(gframe->cmd_draw[1], &rpb_info, VK_SUBPASS_CONTENTS_INLINE);
		}

		for(auto& r : e.mRenderers) IF_UI_RPASS_(r) r->duringDrawStage(concurrent_access, sc_img_idx, gframe->cmd_draw[1]);

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
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
				0 };
			VkSemaphore drawSems[3] = { gframe->sem_prepare, gframe->sem_drawWorld, gframe->sem_drawGui };
			subm.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			subm.commandBufferCount = 1;
			subm.pCommandBuffers    = &gframe->cmd_prepare;
			subm.pWaitDstStageMask  = waitStages + 0;
			subm.signalSemaphoreCount = 1;
			subm.pSignalSemaphores    = drawSems + 0;
			VK_CHECK(vkResetFences, e.mDevice,          1,       &gframe->fence_prepare);
			VK_CHECK(vkQueueSubmit, e.mQueues.graphics, 1, &subm, gframe->fence_prepare);
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
		if(e.mPrefs.wait_for_gframe) {
			VkFence fences[2] = { gframe->fence_prepare, e.mGframes[sc_img_idx].fence_draw };
			vkWaitForFences(e.mDevice, std::size(fences), fences, true, UINT64_MAX);
		} else {
			vkWaitForFences(e.mDevice, 1, &gframe->fence_prepare, true, UINT64_MAX);
		}
		e.mRendererMutex.unlock();

		for(auto& r : e.mRenderers) r->beforePostRender(concurrent_access, sc_img_idx);
		loop.loop_async_postRender(concurrent_access, delta, e.mGraphicsReg.lastDelta());
		for(auto& r : e.mRenderers) r->afterPostRender(concurrent_access, sc_img_idx);

		e.mGraphicsReg.endCycle();

		e.mUiRenderer_TMP_UGLY_NAME->trimTextCaches(e.mPrefs.font_max_cache_size);
		#undef IF_WORLD_RPASS_
		#undef IF_UI_RPASS_
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


	static void handleSignals(Engine& e) {
		bool reinitGate = false;
		auto reinit = [&]() {
			using tickreg::delta_t;

			if(reinitGate) return;
			reinitGate = true;

			auto time = std::chrono::duration_cast<std::chrono::duration<decltype(Engine::mLastResizeTime), std::milli>>(std::chrono::steady_clock::now().time_since_epoch()).count();
			if(e.mLastResizeTime + 1000 > time) return;

			// Some compositors resize the window as soon as it appears, and this seems to cause problems
			e.mGraphicsReg.resetEstimates (delta_t(1.0) / delta_t(e.mPrefs.target_framerate));
			e.mLogicReg.resetEstimates    (delta_t(1.0) / delta_t(e.mPrefs.target_tickrate));

			auto init = reinterpret_cast<Engine::RpassInitializer*>(&e);
			auto ca = ConcurrentAccess(&e, true);
			init->reinit(ca);
		};

		std::unique_lock<std::mutex> gframeLock;

		bool gframeLockExists = false;
		auto awaitRpass = [&]() {
			if(! gframeLockExists) {
				gframeLock = e.pauseRenderPass();
				gframeLockExists = true;
			}
		};

		auto extractSignal = [&]() {
			auto r = e.mSignalXthread.exchange(Signal::eNone, std::memory_order::seq_cst);
			return Signal(r);
		};

		auto handleSignal = [&](Signal sig) {
			switch(sig) {
				default: [[fallthrough]];
				case Signal::eNone: return false; break;
				case Signal::eReinit: reinit(); break;
			}
			return true;
		};

		// Handle the local signal once
		Signal curSignal = Signal::eNone;
		std::swap(curSignal, e.mSignalGthread);
		if(curSignal != Signal::eNone) {
			handleSignal(curSignal);
			e.mLogger->trace("Handled signal {} from graphics thread", signalString(curSignal));
		}

		awaitRpass();
		curSignal = extractSignal();
		if(curSignal != Signal::eNone) {
			do {
				handleSignal(curSignal);
				e.mLogger->trace("Handled signal {} from external thread, polling new immediate signal", signalString(curSignal));
				e.mLogger->flush();
				curSignal = extractSignal();
			} while(curSignal != Signal::eNone);
		}
		assert(e.mSignalGthread == Signal::eNone);
		assert(e.mSignalXthread.load(std::memory_order::relaxed) == Signal::eNone);
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



	void ConcurrentAccess::setPresentExtent(VkExtent2D ext) {
		bool extChanged =
			(ext.width  != ca_engine->mPresentExtent.width) ||
			(ext.height != ca_engine->mPresentExtent.height);
		if(extChanged) {
			ca_engine->mPrefs.init_present_extent = ext;

			auto gframeLock = std::unique_lock(ca_engine->mGframeMutex, std::defer_lock_t());
			bool gframeNotRunning = gframeLock.try_lock();
			if(gframeNotRunning) {
				ca_engine->signal(Engine::Signal::eReinit);
			} else {
				// A gframe is already running, and would deadlock on mRendererMutex;
				// we need to wait for the gframe to end, and the state of the engine
				// protected by mRendererMutex may change unbeknownst to the caller.
				ca_engine->mGframePriorityOverride.store(true, std::memory_order::seq_cst);
				ca_engine->mRendererMutex.unlock();
				ca_engine->signal(Engine::Signal::eReinit, true);
				ca_engine->mGframePriorityOverride.store(false, std::memory_order::seq_cst);
				ca_engine->mGframeResumeCond.notify_one();
				ca_engine->mRendererMutex.lock();
			}
		}
	}



	Engine::Engine(
			const DeviceInitInfo&    di,
			const EnginePreferences& ep,
			std::shared_ptr<ShaderCacheInterface> sci,
			std::shared_ptr<AssetSourceInterface> asi,
			std::shared_ptr<spdlog::logger>       logger
	):
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
			debug::setLogger(r);
			return r;
		} ()),
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
		mGframePriorityOverride(false),
		mGframeCounter(0),
		mGframeSelector(0),
		mSignalGthread(Signal::eNone),
		mSignalXthread(Signal::eNone),
		mLastResizeTime(0),
		mAssetSource(std::move(asi)),
		mPrefs(ep),
		mIsRunning(false)
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

		{ // Temporary RenderProcess test output
			auto depGraph = RenderProcess::DependencyGraph(mLogger, mGframes.size());
			using RpDesc = RenderPassDescription;
			using SpDesc = RpDesc::Subpass;
			using SpDep = SpDesc::Dependency;
			using Atch = SpDesc::Attachment;
			using RtDesc = RenderTargetDescription;
			using ImgRef = RtDesc::ImageRef;
			using RtResize = RenderProcess::RtargetResizeInfo;
			auto renderExt3d  = VkExtent3D { mRenderExtent .width, mRenderExtent .height, 1 };
			auto presentExt3d = VkExtent3D { mPresentExtent.width, mPresentExtent.height, 1 };
			auto worldRtargetRefs = std::make_shared<std::vector<ImgRef>>();
			auto uiRtargetRefs    = std::make_shared<std::vector<ImgRef>>();
			worldRtargetRefs->reserve(mGframes.size());
			uiRtargetRefs   ->reserve(mGframes.size());
			for(auto& gframe : mGframes) {
				worldRtargetRefs->push_back(ImgRef { .image = gframe.atch_color,      .imageView = gframe.atch_color_view      });
				uiRtargetRefs   ->push_back(ImgRef { .image = gframe.swapchain_image, .imageView = gframe.swapchain_image_view });
			}
			RtDesc rtDesc[2] = {
				RtDesc { worldRtargetRefs, renderExt3d,  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mSurfaceFormat.format, false, false, false, true },
				RtDesc { uiRtargetRefs,    presentExt3d, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mSurfaceFormat.format, false, false, false, true } };
			RpDesc worldRpDesc, uiRpDesc;
			Atch worldColAtch = {
				.rtarget = depGraph.addRtarget(rtDesc[0]),
				.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE };
			Atch uiColAtch = {
				.rtarget = depGraph.addRtarget(rtDesc[1]),
				.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE };
			Atch dummyInAtch[2]; for(size_t i = 0; i < std::size(dummyInAtch); ++i) dummyInAtch[i] = {
				.rtarget = depGraph.addRtarget(RtDesc { nullptr, presentExt3d, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_FORMAT_R32_SFLOAT, false, false, false, true }),
				.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE };
			Atch dummyColAtch[2]; for(size_t i = 0; i < std::size(dummyColAtch); ++i) dummyColAtch[i] = {
				.rtarget = depGraph.addRtarget(RtDesc { nullptr, presentExt3d, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_FORMAT_R32_SFLOAT, false, false, false, true }),
				.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
			worldRpDesc.subpasses.push_back(SpDesc {
					.inputAttachments = { }, .colorAttachments = { worldColAtch },
					.subpassDependencies = { },
					.depthLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
					.requiresDepthAttachments = true });
			worldRpDesc.subpasses.push_back(SpDesc {
					.inputAttachments = { }, .colorAttachments = { worldColAtch },
					.subpassDependencies = { SpDep {
						.srcSubpass = 0,
						.srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, .dstStageMask  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
						.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
						.dependencyFlags = { } }},
					.depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.requiresDepthAttachments = true });
			worldRpDesc.framebufferSize = renderExt3d;
			uiRpDesc.subpasses.push_back(SpDesc {
				.inputAttachments = { dummyInAtch[0], dummyInAtch[1] },
				.colorAttachments = { uiColAtch, dummyColAtch[0], dummyColAtch[1] },
				.subpassDependencies = { SpDesc::Dependency {
					.srcSubpass = VK_SUBPASS_EXTERNAL,
					.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
					.dependencyFlags = { } }},
				.depthLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.requiresDepthAttachments = true });
			uiRpDesc.framebufferSize = presentExt3d;
			const auto& worldRpassId = depGraph.addRpass(worldRpDesc);
			const auto& uiRpassId    = depGraph.addRpass(uiRpDesc   );
			auto addStep = [&depGraph](RenderPassId rpass, RendererId renderer, const VkExtent3D& ext) {
				RenderProcess::StepDescription desc = { };
				desc.rpass = rpass;
				desc.renderer = renderer;
				desc.renderArea.extent = { ext.width, ext.height };
				return depGraph.addStep(std::move(desc));
			};
			RendererId renderers[] = { depGraph.addRenderer({ }), depGraph.addRenderer({ }) };
			auto sg0 = addStep(worldRpassId, renderers[0], renderExt3d );
			;          addStep(uiRpassId,    renderers[1], presentExt3d).after(sg0);
			try {
				mRenderProcess.setup(mVma, mLogger, mDepthAtchFmt, mGframes.size(), depGraph.assembleSequence());
				mLogger->debug("Renderer list:");
				size_t waveIdx = 0;
				for(auto wave : mRenderProcess.waveRange()) {
					for(auto& step : wave) {
						auto& ra = step.second.renderArea;
						mLogger->debug("  Wave {}: step {} renderer {} renderArea ({},{})x({},{})",
							waveIdx,
							RenderProcess::step_id_e(step.first),
							render_target_id_e(step.second.renderer),
							ra.offset.x, ra.offset.y, ra.extent.width, ra.extent.height );
					}
					++ waveIdx;
				}
				if(waveIdx == 0) mLogger->debug("  (empty)");
				mRenderProcess.reset(mRenderProcess.gframeCount() - 2, {
					RtResize { dummyInAtch [1].rtarget, { 2100,2100,1 } },
					RtResize { dummyColAtch[1].rtarget, { 2000,2000,1 } } });
				mRenderProcess.destroy();
			} catch(RenderProcess::UnsatisfiableDependencyError& err) {
				mLogger->error("{}:", err.what());
				auto& chain = err.dependencyChain();
				for(auto step : chain) mLogger->error("  Renderer {:3}", RenderProcess::step_id_e(step));
				mLogger->error("  Renderer {:3} ...", RenderProcess::step_id_e(chain.front()));
			}
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
		using namespace posixfio;
		static_assert(4 == sizeof(uint32_t));

		VkShaderModuleCreateInfo    sm_info = { };
		std::unique_ptr<uint32_t[]> buffer;
		try {
			auto file    = posixfio::File::open(file_path.c_str(), OpenFlags::eRdonly);
			size_t lsize = file.lseek(0, Whence::eEnd);
			if(lsize > UINT32_MAX) throw ShaderModuleReadError("Shader file is too long");
			if(lsize % 4 != 0)     throw ShaderModuleReadError("Misaligned shader file size");
			file.lseek(0, Whence::eSet);
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


	void Engine::run(LoopInterface& loop) {
		auto loop_state = loop.loop_pollState();
		auto exception  = std::exception_ptr(nullptr);

		auto handle_exception = [&]() {
			auto lock = std::unique_lock(mGframeMutex, std::try_to_lock);
			exception = std::current_exception();
			loop_state = LoopInterface::LoopState::eShouldStop;
		};

		mGraphicsThread = std::thread([&]() {
			#ifdef NDEBUG
				mIsRunning.store(true, std::memory_order::seq_cst);
			#else
				bool wasRunningBeforeBeginning = mIsRunning.exchange(true, std::memory_order::seq_cst);
				assert(! wasRunningBeforeBeginning);
			#endif
			while(loop_state != LoopInterface::LoopState::eShouldStop) {
				try {
					auto gframeLock = std::unique_lock(mGframeMutex, std::defer_lock);
					if(mGframePriorityOverride.load(std::memory_order_consume)) [[unlikely]] {
						gframeLock.lock();
						mGframeResumeCond.wait(gframeLock);
					} else {
						gframeLock.lock(); // This must be done for both branches, but before waiting on the cond var AND after checking the atomic var
					}
					Implementation::draw(*this, loop);
					Implementation::handleSignals(*this);
					gframeLock.unlock();
					mGraphicsReg.awaitNextTick();
				} catch(...) {
					handle_exception();
				}
			}
			mIsRunning.store(false, std::memory_order::seq_cst);
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


	bool Engine::isRunning() const noexcept {
		return mIsRunning.load(std::memory_order::seq_cst);
	}


	void Engine::signal(Signal newSig, bool discardDuplicate) noexcept {
		using Ms = std::chrono::duration<float, std::milli>;
		Signal oldSig;
		bool thisIsGraphicsThread = mGraphicsThread.get_id() == std::this_thread::get_id();
		auto exchangeSig = [&]() { auto r = mSignalXthread.exchange(newSig, std::memory_order::seq_cst); return Signal(r); };
		auto discardBecauseDuplicateFn = [&]() { return discardDuplicate && (oldSig == newSig); };

		if(thisIsGraphicsThread) {
			mLogger->trace("Graphics thread signaling {}", signalString(newSig));
			oldSig = Signal(mSignalGthread);
			mSignalGthread = newSig;
			assert(oldSig == Signal::eNone);
		} else {
			auto gframeLock = std::unique_lock(mGframeMutex, std::defer_lock_t());
			oldSig = exchangeSig();
			bool discardBecauseDuplicate = discardBecauseDuplicateFn();
			while((oldSig != Signal::eNone) && (! discardBecauseDuplicate)) {
				auto sleepTime = Ms(100.0f* 1000.0f / (mPrefs.target_tickrate * float(mPrefs.max_concurrent_frames)));
				mSignalXthread.store(oldSig, std::memory_order::seq_cst);
				std::this_thread::sleep_for(sleepTime);
				mGframeResumeCond.notify_one();
				oldSig = exchangeSig();
				discardBecauseDuplicate = discardBecauseDuplicateFn();
			}
			if(discardBecauseDuplicate) {
				assert(oldSig == newSig);
				mLogger->trace("Discarded duplicate signal {}", signalString(newSig));
			} else {
				assert(oldSig == Signal::eNone);
				mLogger->trace("External thread signaling {}", signalString(newSig));
				mLogger->flush();
			}
		}
	}


	std::unique_lock<std::mutex> Engine::pauseRenderPass() {
		auto lock = std::unique_lock(mGframeMutex, std::defer_lock_t());

		auto wait_for_fences = [&]() {
			// Wait for all fences, in this order: selection -> prepare -> draw
			std::vector<VkFence> fences;
			for(auto& gff : mGframeSelectionFences) vkWaitForFences(mDevice, 1, &gff, true, UINT64_MAX);
			fences.reserve(mGframes.size());
			for(auto& gframe : mGframes) fences.push_back(gframe.fence_prepare);
			vkWaitForFences(mDevice, fences.size(), fences.data(), true, UINT64_MAX);
			fences.clear();
			fences.reserve(mGframes.size());
			for(auto& gframe : mGframes) fences.push_back(gframe.fence_draw);
			vkWaitForFences(mDevice, fences.size(), fences.data(), true, UINT64_MAX);

			// Text caches no longer need synchronization, and MUST forget about potentially soon-to-be deleted fences
			mUiRenderer_TMP_UGLY_NAME->forgetTextCacheFences();
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
		vkDeviceWaitIdle(mDevice);

		return lock;
	}


	void Engine::setWaitForGframe(bool wait_for_gframe) {
		mPrefs.wait_for_gframe = wait_for_gframe;
	}


	MutexAccess<ConcurrentAccess> Engine::getConcurrentAccess() noexcept {
		assert(mGraphicsThread.get_id() != std::this_thread::get_id() && "This *will* cause a deadlock");
		auto gframeLock = std::unique_lock(mGframeMutex, std::defer_lock);
		auto rendererLock = std::unique_lock(mRendererMutex);
		if(this->isRunning()) {
			mGframeResumeCond.notify_one();
		}
		auto ca = ConcurrentAccess(this, false);
		return MutexAccess(std::move(ca), std::move(rendererLock));
	}

}
