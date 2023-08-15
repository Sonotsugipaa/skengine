#include "engine.hpp"

#include "init/init.hpp"

#include <posixfio_tl.hpp>

#include <spdlog/spdlog.h>

#include <fmamdl/fmamdl.hpp>

#include <vk-util/error.hpp>



#warning "Global test model"
auto testModel        = posixfio::File::open("assets/test-model.fma", O_RDONLY);
auto testModelMap     = testModel.mmap(testModel.lseek(0, SEEK_END), posixfio::MemProtFlags::eRead, posixfio::MemMapFlags::ePrivate, 0);
auto testModelIndices = (fmamdl::Header { testModelMap.get<std::byte>(), testModelMap.size() }).indices();

struct SKENGINE_NAME_NS::Engine::Implementation {

	struct PipelineCreateInfo {
		ShaderCacheInterface* shaderCache;
		std::string_view      materialName;
	};


	static constexpr auto NO_FRAME_AVAILABLE = ~ decltype(Engine::mGframeSelector)(0);


	static vkutil::BufferDuplex createVertexBuffer_PLACEHOLDER(Engine& e, VkCommandBuffer cmd) {
		const fmamdl::Header h = { testModelMap.get<std::byte>(), testModelMap.size() };
		auto& vertices = h.vertices();

		vkutil::BufferCreateInfo bc_info = {
			.size  = vertices.size_bytes(),
			.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			.qfamSharing = { } };

		auto r = vkutil::BufferDuplex::createVertexInputBuffer(e.mVma, bc_info);

		memcpy(r.mappedPtr<void>(), vertices.data(), vertices.size_bytes());

		r.flush(cmd, e.mVma);
		return r;
	}


	static vkutil::BufferDuplex createIndexBuffer_PLACEHOLDER(Engine& e, VkCommandBuffer cmd) {
		const fmamdl::Header h = { testModelMap.get<std::byte>(), testModelMap.size() };
		auto& indices = h.indices();

		vkutil::BufferCreateInfo bc_info = {
			.size  = indices.size_bytes(),
			.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			.qfamSharing = { } };

		auto r = vkutil::BufferDuplex::createIndexInputBuffer(e.mVma, bc_info);

		memcpy(r.mappedPtr<void>(), indices.data(), indices.size_bytes());

		r.flush(cmd, e.mVma);
		return r;
	}


	static VkPipeline createPipeline(Engine& e, PipelineCreateInfo& ci) {
		VkPipeline pipeline;

		VkVertexInputAttributeDescription vtx_attr[2];
		VkVertexInputBindingDescription   vtx_bind[1];
		{ // Hard-coded input descriptions and bindings
			#warning "Vertex inputs depend on the memory-mappable model format, but it is currently hard-coded"
			constexpr size_t POS = 0;
			constexpr size_t COL = 1;
			vtx_attr[POS].binding  = 0;
			vtx_attr[POS].format   = VK_FORMAT_R32G32B32_SFLOAT;
			vtx_attr[POS].location = 0;
			vtx_attr[POS].offset   = 0;
			vtx_attr[COL].binding  = 0;
			vtx_attr[COL].format   = VK_FORMAT_R32G32B32_SFLOAT;
			vtx_attr[COL].location = 1;
			vtx_attr[COL].offset   = offsetof(fmamdl::Vertex, normal);
			vtx_bind[0].binding   = 0;
			vtx_bind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			vtx_bind[0].stride    = sizeof(fmamdl::Vertex);
		}

		VkPipelineVertexInputStateCreateInfo vi = { }; {
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vi.vertexAttributeDescriptionCount = std::size(vtx_attr);
			vi.pVertexAttributeDescriptions    = vtx_attr;
			vi.vertexBindingDescriptionCount = std::size(vtx_bind);
			vi.pVertexBindingDescriptions    = vtx_bind;
		}

		VkPipelineInputAssemblyStateCreateInfo ia = { }; {
			ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.primitiveRestartEnable = true;
			ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		}

		VkPipelineTessellationStateCreateInfo t = { }; {
			t.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
		}

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

		VkPipelineViewportStateCreateInfo v = { }; {
			v.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			v.viewportCount = 1;
			v.pViewports    = &viewport;
			v.scissorCount  = 1;
			v.pScissors     = &scissor;
		}

		VkPipelineRasterizationStateCreateInfo r = { }; {
			r.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			r.cullMode    = VK_CULL_MODE_NONE;
			r.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			r.polygonMode = VK_POLYGON_MODE_FILL;
			r.lineWidth   = 1.0f;
			r.rasterizerDiscardEnable = false;
		}

		VkPipelineMultisampleStateCreateInfo m = { }; {
			m.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			m.minSampleShading = 1.0f;
			m.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			m.sampleShadingEnable  = false;
		}

		VkPipelineDepthStencilStateCreateInfo ds = { }; {
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable  = false;
			ds.depthWriteEnable = false;
			ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
		}

		VkPipelineColorBlendAttachmentState atch_color[1]; {
			*atch_color = { };
			atch_color->blendEnable = false;
			atch_color->colorWriteMask = /* rgba */ VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		}

		VkPipelineColorBlendStateCreateInfo cb = { }; {
			cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.logicOpEnable   = false;
			cb.attachmentCount = 1;
			cb.pAttachments    = atch_color;
		}

		VkPipelineDynamicStateCreateInfo d = { }; {
			d.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		}

		VkPipelineShaderStageCreateInfo stages[2]; {
			constexpr size_t VTX = 0;
			constexpr size_t FRG = 1;
			ShaderRequirement req = {
				.world = { ci.materialName },
				.type  = ShaderRequirement::Type::eWorld };
			auto set = ci.shaderCache->shader_cache_requestModuleSet(e, req);
			stages[VTX] = { };
			stages[VTX].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[VTX].pName  = "main";
			stages[VTX].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[VTX].module = set.vertex;
			stages[FRG] = stages[VTX];
			stages[FRG].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[FRG].module = set.fragment;
		}

		VkGraphicsPipelineCreateInfo gpc_info = { };
		gpc_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gpc_info.renderPass = e.mRpass;
		gpc_info.layout     = e.mPipelineLayout;
		gpc_info.subpass    = 0;
		gpc_info.stageCount = std::size(stages);
		gpc_info.pStages    = stages;
		gpc_info.pVertexInputState   = &vi;
		gpc_info.pInputAssemblyState = &ia;
		gpc_info.pTessellationState  = &t;
		gpc_info.pViewportState      = &v;
		gpc_info.pRasterizationState = &r;
		gpc_info.pMultisampleState   = &m;
		gpc_info.pDepthStencilState  = &ds;
		gpc_info.pColorBlendState    = &cb;
		gpc_info.pDynamicState       = &d;

		VK_CHECK(vkCreateGraphicsPipelines, e.mDevice, e.mPipelineCache, 1, &gpc_info, nullptr, &pipeline);

		return pipeline;
	}


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
				spdlog::trace("Selected gframe {}/{}", candidate_idx+1, e.mGframes.size());
				VK_CHECK(vkResetFences, e.mDevice, 1, &candidate.fence_draw);
				VK_CHECK(vkResetCommandPool, e.mDevice, candidate.cmd_pool, 0);
				return candidate_idx;
			} else {
				spdlog::trace("Skipped unavailable gframe #{}", candidate_idx);
			}
		}
		++ e.mGframeSelector;
		return NO_FRAME_AVAILABLE;
	}


	// Returns `false` if the swapchain is out of date
	static bool draw(
		Engine& e,
		ShaderCacheInterface& shaders,
		LoopInterface&        loop
	) {
		size_t      gframe_idx;
		GframeData* gframe;
		uint32_t    sc_img_idx = ~ uint32_t(0);
		VkImage     sc_img;

		{ // Select gframe
			gframe_idx = selectGframe(e);
			gframe     = e.mGframes.data() + gframe_idx;
			if(gframe_idx == NO_FRAME_AVAILABLE) {
				spdlog::trace("No gframe available");
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
					spdlog::trace("Swapchain is suboptimal or out of date");
					return false;
				case VK_TIMEOUT:
					spdlog::trace("Swapchain image request timed out");
					return true;
				default:
					assert(res < VkResult(0));
					throw vkutil::VulkanError("vkAcquireNextImage2KHR", res);
			}
			spdlog::trace("Acquired swapchain image {}", sc_img_idx);
			sc_img = e.mSwapchainImages[sc_img_idx].image;
		}


		VkPipeline pipeline_PLACEHOLDER;
		{ // PLACEHOLDER
			PipelineCreateInfo pci = {
				.shaderCache  = &shaders,
				.materialName = "default" };
			pipeline_PLACEHOLDER = createPipeline(e, pci);
		}

		VkCommandBufferBeginInfo cbb_info = { };
		cbb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		loop.loop_async_preRender();

		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_prepare, &cbb_info);
		VK_CHECK(vkBeginCommandBuffer, gframe->cmd_draw, &cbb_info);

		vkutil::BufferDuplex vtx_buffer_PLACEHOLDER;
		vkutil::BufferDuplex idx_buffer_PLACEHOLDER;
		{ // Prepare the buffers
			vtx_buffer_PLACEHOLDER = createVertexBuffer_PLACEHOLDER(e, gframe->cmd_prepare);
			idx_buffer_PLACEHOLDER = createIndexBuffer_PLACEHOLDER(e, gframe->cmd_prepare);
		}

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
			vkCmdBindPipeline(gframe->cmd_draw, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_PLACEHOLDER);
			vkCmdBindVertexBuffers(gframe->cmd_draw, 0, 1, &vtx_buffer_PLACEHOLDER.value, &offset);
			vkCmdBindIndexBuffer(gframe->cmd_draw, idx_buffer_PLACEHOLDER.value, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(gframe->cmd_draw, testModelIndices.size(), 1, 0, 0, 0);
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

		loop.loop_async_postRender();

		VK_CHECK(vkWaitForFences, e.mDevice, 1, &gframe->fence_draw, true, UINT64_MAX);
		vkDestroyPipeline(e.mDevice, pipeline_PLACEHOLDER, nullptr);
		vkutil::BufferDuplex::destroy(e.mVma, vtx_buffer_PLACEHOLDER);
		vkutil::BufferDuplex::destroy(e.mVma, idx_buffer_PLACEHOLDER);
		return true;
	}


	static LoopInterface::LoopState runIteration(
		Engine& e,
		ShaderCacheInterface& shaders,
		LoopInterface&        loop
	) {
		loop.loop_processEvents();
		if(e.mGframeMutex.try_lock()) {
			try {
				draw(e, shaders, loop);
				e.mGframeMutex.unlock();
			} catch(...) {
				e.mGframeMutex.unlock();
				std::rethrow_exception(std::current_exception());
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		return loop.loop_pollState();
	}

};



namespace SKENGINE_NAME_NS {

	Engine::Engine(const DeviceInitInfo& di, const EnginePreferences& ep) {
		{
			auto init = reinterpret_cast<Engine::DeviceInitializer*>(this);
			init->init(&di, &ep);
		}

		{
			auto rpass_cfg = RpassConfig::default_cfg;
			auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
			init->init(rpass_cfg);
		}
	}


	Engine::~Engine() {
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
		spdlog::trace("Loaded shader module from memory");
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
		spdlog::trace("Loaded shader module from file \"{}\"", file_path);
		return r;
	}


	void Engine::destroyShaderModule(VkShaderModule module) {
		vkDestroyShaderModule(mDevice, module, nullptr);
	}


	void Engine::run(
			ShaderCacheInterface& shaders,
			LoopInterface&        loop
	) {
		auto cleanup = [&]() {
			shaders.shader_cache_releaseAllModules(*this);
		};

		auto loop_state = loop.loop_pollState();
		while(loop_state != LoopInterface::LoopState::eShouldStop) {
			if(loop_state == LoopInterface::LoopState::eShouldDelay) {
				spdlog::warn("Engine instructed to delay the loop, but the functionality isn't implemented yet");
				std::this_thread::yield();
			}

			try {
				loop_state = Implementation::runIteration(*this, shaders, loop);
			} catch(...) {
				cleanup();
				std::rethrow_exception(std::current_exception());
			}

			loop_state = loop.loop_pollState();
		}

		cleanup();
	}

}
