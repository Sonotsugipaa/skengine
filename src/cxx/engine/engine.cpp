#include "engine.hpp"

#include "init/init.hpp"
#include "shader_cache.hpp"

#include <posixfio_tl.hpp>

#include <fmamdl/fmamdl.hpp>

#include <vk-util/error.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include "spdlog/sinks/stdout_color_sinks.h"



struct SKENGINE_NAME_NS::Engine::Implementation {

	static constexpr auto NO_FRAME_AVAILABLE = ~ decltype(Engine::mGframeSelector)(0);


	static decltype(Engine::mGframeSelector) selectGframe(Engine& e) {
		using ctr_t = decltype(Engine::mGframeSelector);
		auto count = e.mGframes.size();
		for(ctr_t i = 0; i < count; ++i) {
			ctr_t    candidate_idx = (i + e.mGframeSelector) % count;
			auto&    candidate     = e.mGframes[candidate_idx];
			VkResult wait_res      = vkWaitForFences(e.mDevice, 1, &candidate.fence_draw, VK_TRUE, 0);
			bool     is_free       = (wait_res == VK_SUCCESS);
			if(is_free) {
				e.mGframeSelector = (1 + candidate_idx) % count;
				e.logger().trace("Selected gframe {}/{}", candidate_idx+1, e.mGframes.size());
				VK_CHECK(vkResetFences, e.mDevice, 1, &candidate.fence_draw);
				VK_CHECK(vkResetCommandPool, e.mDevice, candidate.cmd_pool, 0);
				return candidate_idx;
			} else {
				e.logger().trace("Skipped unavailable gframe #{}", candidate_idx);
			}
		}
		++ e.mGframeSelector;
		return NO_FRAME_AVAILABLE;
	}


	// Returns `false` if the swapchain is out of date
	static bool draw(Engine& e, LoopInterface& loop) {
		size_t      gframe_idx;
		GframeData* gframe;
		uint32_t    sc_img_idx = ~ uint32_t(0);
		VkImage     sc_img;
		auto        delta_avg = e.mGraphicsReg.estDelta();

		e.mGraphicsReg.beginCycle();

		{ // Select gframe
			gframe_idx = selectGframe(e);
			gframe     = e.mGframes.data() + gframe_idx;
			if(gframe_idx == NO_FRAME_AVAILABLE) {
				e.logger().trace("No gframe available");
				return true;
			}
		}

		{ // Acquire image
			VkResult res = vkAcquireNextImageKHR(e.mDevice, e.mSwapchain, UINT64_MAX, gframe->sem_swapchain_image, nullptr, &sc_img_idx);
			switch(res) {
				case VK_SUCCESS:
					break;
				case VK_ERROR_OUT_OF_DATE_KHR:
				case VK_SUBOPTIMAL_KHR:
					e.logger().trace("Swapchain is suboptimal or out of date");
					return false;
				case VK_TIMEOUT:
					e.logger().trace("Swapchain image request timed out");
					return true;
				default:
					assert(res < VkResult(0));
					throw vkutil::VulkanError("vkAcquireNextImage2KHR", res);
			}
			e.logger().trace("Acquired swapchain image {}", sc_img_idx);
			sc_img = e.mSwapchainImages[sc_img_idx].image;
		}

		VkCommandBufferBeginInfo cbb_info = { };
		cbb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		loop.loop_async_preRender(delta_avg, e.mGraphicsReg.lastDelta());

		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_prepare, &cbb_info);
		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_draw, &cbb_info);

		{ // Prepare the gframe buffers
			auto& ubo = *gframe->frame_ubo.mappedPtr<dev::FrameUniform>();
			ubo.proj_transf     = e.mProjTransf;
			ubo.view_transf     = e.mWorldRenderer.getViewTransf();
			ubo.projview_transf = ubo.proj_transf * ubo.view_transf;
			gframe->frame_ubo.flush(gframe->cmd_prepare, e.mVma);
		}

		MeshId mesh_id = e.mWorldRenderer.fetchMesh("assets/test-model.fma");
		auto*  mesh    = e.mWorldRenderer.getMesh(mesh_id);
		assert(mesh != nullptr);

		VkImageMemoryBarrier imb[2] = { };
		imb[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imb[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imb[0].subresourceRange.layerCount = 1;
		imb[0].subresourceRange.levelCount = 1;
		imb[1] = imb[0];

		{ // Begin the render pass
			constexpr size_t COLOR = 0;
			constexpr size_t DEPTH = 1;
			constexpr float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.6f };
			VkClearValue clears[2];
			memcpy(clears[COLOR].color.float32, clear_color, 4 * sizeof(float));
			clears[DEPTH].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo rpb_info = { };
			rpb_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpb_info.framebuffer = gframe->framebuffer;
			rpb_info.renderPass  = e.mRpass;
			rpb_info.clearValueCount = 2;
			rpb_info.pClearValues    = clears;
			rpb_info.renderArea      = { VkOffset2D { 0, 0 }, e.mRenderExtent };

			vkCmdBeginRenderPass(gframe->cmd_draw, &rpb_info, VK_SUBPASS_CONTENTS_INLINE);
		}

		{ // Draw the objects
			constexpr VkDeviceSize offset = 0;
			auto& cmd = gframe->cmd_draw;
			VkDescriptorSet dsets[] = { gframe->frame_dset };

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

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e.mGenericGraphicsPipeline);
			vkCmdBindIndexBuffer(cmd, mesh->indices.value, 0, VK_INDEX_TYPE_UINT32);
			vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->vertices.value, &offset);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e.mPipelineLayout, 0, std::size(dsets), dsets, 0, nullptr);
			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
			vkCmdDrawIndexed(cmd, mesh->indices.size() / sizeof(fmamdl::Index), 1, 0, 0, 0);
		}

		vkCmdEndRenderPass(gframe->cmd_draw);

		{ // Barrier the color attachment and swapchain images for transfer
			imb[0].image = gframe->atch_color;
			imb[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imb[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imb[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			// imb[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			imb[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			// imb[0].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
			imb[1] = imb[0];
			imb[1].image = e.mSwapchainImages[sc_img_idx].image;
			imb[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imb[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imb[1].srcAccessMask = VK_ACCESS_NONE;
			// imb[1].srcStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			imb[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			// imb[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
			vkCmdPipelineBarrier(
				gframe->cmd_draw,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, { },
				0, nullptr, 0, nullptr, 1, imb+0 );
			vkCmdPipelineBarrier(
				gframe->cmd_draw,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, { },
				0, nullptr, 0, nullptr, 1, imb+1 );
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
			blit.dstImage       = sc_img;
			blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			blit.filter = VK_FILTER_NEAREST;
			blit.regionCount = 1;
			blit.pRegions = &region;
			vkCmdBlitImage2(gframe->cmd_draw, &blit);
		}

		{ // Barrier the swapchain image for presenting, and the color attachment for... color attaching?
			imb[0].image = e.mSwapchainImages[sc_img_idx].image;
			imb[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imb[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			imb[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			// imb[0].srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
			imb[0].dstAccessMask = VK_ACCESS_NONE;
			// imb[0].dstStageMask  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			imb[1].image = gframe->atch_color;
			imb[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imb[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imb[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			// imb[1].srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
			imb[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			// imb[1].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			vkCmdPipelineBarrier(
				gframe->cmd_draw,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, { },
				0, nullptr, 0, nullptr, 1, imb+0 );
			vkCmdPipelineBarrier(
				gframe->cmd_draw,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, { },
				0, nullptr, 0, nullptr, 1, imb+1 );
		}

		VK_CHECK(vkEndCommandBuffer, gframe->cmd_prepare);
		VK_CHECK(vkEndCommandBuffer, gframe->cmd_draw);

		VkSubmitInfo subm[2] = { };

		{ // Submit the prepare and draw commands
			constexpr VkPipelineStageFlags prepare_wait_stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
			constexpr VkPipelineStageFlags draw_wait_stages    = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			subm[0].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			subm[0].commandBufferCount   = 1;
			subm[0].signalSemaphoreCount = 1;
			subm[0].pCommandBuffers    = &gframe->cmd_prepare;
			subm[0].waitSemaphoreCount = 1;
			subm[0].pWaitSemaphores    = &gframe->sem_swapchain_image;
			subm[0].pWaitDstStageMask  = &prepare_wait_stages;
			subm[0].pSignalSemaphores  = &gframe->sem_prepare;
			subm[1] = subm[0];
			subm[1].pCommandBuffers    = &gframe->cmd_draw;
			subm[1].waitSemaphoreCount = 1;
			subm[1].pWaitSemaphores    = &gframe->sem_prepare;
			subm[1].pWaitDstStageMask  = &draw_wait_stages;
			subm[1].pSignalSemaphores  = &gframe->sem_draw;
			VK_CHECK(vkResetFences, e.mDevice, 1, &gframe->fence_draw);
			VK_CHECK(vkQueueSubmit, e.mQueues.graphics, std::size(subm), subm, gframe->fence_draw);
		}

		{ // Here's a present!
			VkResult res;
			VkPresentInfoKHR p_info = { };
			p_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			p_info.pResults = &res;
			p_info.swapchainCount = 1;
			p_info.pSwapchains    = &e.mSwapchain;
			p_info.pImageIndices  = &sc_img_idx;
			p_info.waitSemaphoreCount = 1;
			p_info.pWaitSemaphores    = &gframe->sem_draw;
			VK_CHECK(vkQueuePresentKHR, e.mPresentQueue, &p_info);
		}

		loop.loop_async_postRender(delta_avg, e.mGraphicsReg.lastDelta());

		e.mGraphicsReg.endCycle();
		e.mGraphicsReg.awaitNextTick();

		return true;
	}


	static LoopInterface::LoopState runIteration(Engine& e, LoopInterface& loop) {
		auto delta_avg = e.mLogicReg.estDelta();

		e.mLogicReg.beginCycle();
		loop.loop_processEvents(delta_avg, e.mLogicReg.lastDelta());

		if(e.mGframeMutex.try_lock()) {
			e.mLogicReg.beginCycle();
			try {
				draw(e, loop);
				e.mGframeMutex.unlock();
			} catch(...) {
				e.mGframeMutex.unlock();
				std::rethrow_exception(std::current_exception());
			}
			e.mLogicReg.endCycle();
		}

		e.mLogicReg.endCycle();

		auto r = loop.loop_pollState();
		e.mLogicReg.awaitNextTick();
		return r;
	}

};



namespace SKENGINE_NAME_NS {

	const EnginePreferences EnginePreferences::default_prefs = EnginePreferences {
		.phys_device_uuid      = "",
		.init_present_extent   = { 600, 400 },
		.max_render_extent     = { 0, 0 },
		.present_mode          = VK_PRESENT_MODE_FIFO_KHR,
		.sample_count          = VK_SAMPLE_COUNT_1_BIT,
		.logger                = { },
		.log_level             = spdlog::level::info,
		.max_concurrent_frames = 2,
		.fov_y            = glm::radians(110.0f),
		.z_near           = 1.0f / float(1 << 8),
		.z_far            = float(1 << 16),
		.upscale_factor   = 1.0f,
		.target_framerate = 60.0f,
		.target_tickrate  = 60.0f,
		.fullscreen       = false
	};


	constexpr auto regulator_params = tickreg::RegulatorParams {
		.deltaTolerance     = 0.25,
		.burstTolerance     = 0.01,
		.compensationFactor = 1.0 };


	Engine::Engine(
			const DeviceInitInfo&    di,
			const EnginePreferences& ep,
			std::unique_ptr<ShaderCacheInterface> sci
	):
		mShaderCache(std::move(sci)),
		mGraphicsReg(
			std::max<unsigned>(4, ep.target_framerate / 4),
			decltype(ep.target_framerate)(1.0) / ep.target_framerate,
			regulator_params ),
		mLogicReg(
			std::max<unsigned>(4, ep.target_tickrate / 4),
			decltype(ep.target_tickrate)(1.0) / ep.target_tickrate,
			regulator_params ),
		mLogger([&]() {
			decltype(mLogger) r;
			if(ep.logger) {
				r = ep.logger;
			} else {
				r = std::make_shared<spdlog::logger>(
					SKENGINE_NAME_CSTR,
					std::make_shared<spdlog::sinks::stdout_color_sink_mt>(spdlog::color_mode::automatic) );
			}
			r->set_pattern("[%^" SKENGINE_NAME_CSTR " %L%$] %v");
			r->set_level(ep.log_level);
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
			init->init(rpass_cfg);
		}
	}


	Engine::~Engine() {
		mShaderCache->shader_cache_releaseAllModules(*this);

		{
			auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
			init->destroy();
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


	void Engine::run(LoopInterface& loop) {
		auto loop_state = loop.loop_pollState();
		while(loop_state != LoopInterface::LoopState::eShouldStop) {
			if(loop_state == LoopInterface::LoopState::eShouldDelay) {
				logger().warn("Engine instructed to delay the loop, but the functionality isn't implemented yet");
				std::this_thread::yield();
			}

			loop_state = Implementation::runIteration(*this, loop);

			loop_state = loop.loop_pollState();
		}
	}


	scoped_lock Engine::pauseRenderPass() {
		auto lock = boost::interprocess::scoped_lock<boost::mutex>(mGframeMutex);
		for(auto& gframe : mGframes) VK_CHECK(vkWaitForFences, mDevice, 1, &gframe.fence_draw, true, UINT64_MAX);
		return lock;
	}


	void Engine::setPresentExtent(VkExtent2D ext) {
		mPrefs.init_present_extent = ext;

		auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
		init->reinit();
	}

}
