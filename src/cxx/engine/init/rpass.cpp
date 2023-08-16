#include "init.hpp"

#include <vk-util/error.hpp>

#include <posixfio_tl.hpp>

#include <spdlog/spdlog.h>

#include <memory>

#include <vulkan/vk_enum_string_helper.h>

#include <boost/interprocess/sync/scoped_lock.hpp>



namespace SKENGINE_NAME_NS {

	using mtx_scoped_lock = boost::interprocess::scoped_lock<boost::mutex>;


	struct Engine::RpassInitializer::State {
		VkSwapchainKHR oldSwapchain;
		bool reinit           : 1;
		bool createdGframes   : 1;
		bool destroyedGframes : 1;
	};


	void select_swapchain_extent(
			VkExtent2D*       dst,
			const VkExtent2D& desired,
			const VkSurfaceCapabilitiesKHR& capabs
	) {
		assert(capabs.maxImageExtent.width  > 0);
		assert(capabs.maxImageExtent.height > 0);
		const auto& capabsMinExt = capabs.minImageExtent;
		const auto& capabsMaxExt = capabs.maxImageExtent;
		dst->width  = std::clamp(desired.width,  capabsMinExt.width,  capabsMaxExt.width);
		dst->height = std::clamp(desired.height, capabsMinExt.height, capabsMaxExt.height);
		if(desired.width != dst->width || desired.height != dst->height) {
			spdlog::debug("Requested swapchain extent {}x{}, chosen {}x{}",
				desired.width, desired.height,
				dst->width,    dst->height );
		} else {
			spdlog::debug("Chosen swapchain extent {}x{}", desired.width, desired.height);
		}
	}


	VkCompositeAlphaFlagBitsKHR select_composite_alpha(const VkSurfaceCapabilitiesKHR& capabs) {
		assert(capabs.supportedCompositeAlpha != 0);

		#define TRY_CA_(NM_, BIT_) \
			if(capabs.supportedCompositeAlpha & BIT_) { \
				spdlog::info("[+] " #NM_ " is supported"); \
				return BIT_; \
			} else { \
				spdlog::info("[ ] " #NM_ " is not supported"); \
			}

		TRY_CA_("composite alpha PRE_MULTIPLIED_BIT",  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
		TRY_CA_("composite alpha POST_MULTIPLIED_BIT", VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
		TRY_CA_("composite alpha INHERIT_BIT",         VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)

		#undef TRY_CA_

		spdlog::info("[x] Using fallback composite alpha VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR");
		return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	}


	VkPresentModeKHR select_present_mode(
			VkPhysicalDevice phys_device,
			VkSurfaceKHR     surface,
			VkPresentModeKHR preferred_mode
	) {
		std::vector<VkPresentModeKHR> avail_modes;
		{
			#define GET_(COUNT_, PTR_) vkGetPhysicalDeviceSurfacePresentModesKHR(phys_device, surface, COUNT_, PTR_);
			uint32_t count;
			GET_(&count, nullptr);
			avail_modes.resize(count, VkPresentModeKHR(~ uint32_t(0)));
			GET_(&count, avail_modes.data());
			#undef GET_
		}

		#define TRY_PM_(NM_, BIT_, DIFF_) \
			if(BIT_ != DIFF_) [[likely]] { \
				if(avail_modes.end() != std::find(avail_modes.begin(), avail_modes.end(), BIT_)) { \
					spdlog::info("[+] " NM_ "present mode {} is supported", string_VkPresentModeKHR(BIT_)); \
					return BIT_; \
				} else { \
					spdlog::info("[ ] " NM_ "is not supported"); \
				} \
			}

		TRY_PM_("preferred ", preferred_mode, VK_PRESENT_MODE_MAX_ENUM_KHR)
		TRY_PM_("",          VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,     preferred_mode)
		TRY_PM_("",          VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR, preferred_mode)
		TRY_PM_("",          VK_PRESENT_MODE_MAILBOX_KHR,                   preferred_mode)
		TRY_PM_("",          VK_PRESENT_MODE_FIFO_RELAXED_KHR,              preferred_mode)

		#undef TRY_PM_

		spdlog::info("[x] Using fallback present mode VK_PRESENT_MODE_FIFO_KHR");
		return VK_PRESENT_MODE_FIFO_KHR;
	}


	VkExtent2D select_render_extent(const VkExtent2D& present, const VkExtent2D& max, float upscale) {
		VkExtent2D r;
		upscale = std::max(upscale, std::numeric_limits<float>::min());
		r.width  = std::clamp<uint32_t>(std::ceil(present.width  / upscale), 1, max.width);
		r.height = std::clamp<uint32_t>(std::ceil(present.height / upscale), 1, max.height);
		r.width  = std::min(r.width,  max.width);
		r.height = std::min(r.height, max.height);
		return r;
	}


	mtx_scoped_lock pause_rendering(
		const vkutil::Queues& queues,
		boost::mutex&         gframeMtx
	) {
		auto lock = boost::interprocess::scoped_lock<boost::mutex>(gframeMtx);
		VK_CHECK(vkQueueWaitIdle, queues.compute);
		VK_CHECK(vkQueueWaitIdle, queues.transfer);
		VK_CHECK(vkQueueWaitIdle, queues.graphics);
		return lock;
	}


	#warning "Document how this works, since it's trippy, workaroundy and probably UB (hopefully not) (but it removes A LOT of boilerplate)"
	void Engine::RpassInitializer::init(const RpassConfig& rc) {
		mRpassConfig = rc;
		mSwapchainOod = false;
		State state = { };
		initSurface();
		initSwapchain(state);
		initGframes(state);
		initRpass(state);
		initFramebuffers(state);
	}


	void Engine::RpassInitializer::reinit() {
		auto lock = pause_rendering(mQueues, mGframeMutex);

		spdlog::trace("Recreating swapchain");

		State state = { };
		state.reinit = true;

		VkExtent2D old_render_xt  = mRenderExtent;
		VkExtent2D old_present_xt = mPresentExtent;

		destroySwapchain(state);
		state.oldSwapchain = nullptr; // idk how to cache yet
		initSwapchain(state);

		bool render_xt_changed =
			(old_render_xt.width  != mRenderExtent.width) ||
			(old_render_xt.height != mRenderExtent.height);
		bool present_xt_changed =
			(old_present_xt.width  != mPresentExtent.width) ||
			(old_present_xt.height != mPresentExtent.height);

		if(render_xt_changed || present_xt_changed) {
			destroyFramebuffers(state);
			destroyRpass(state);
			destroyGframes(state, 0);
			initGframes(state);
			initRpass(state);
			initFramebuffers(state);
		}
	}


	void Engine::RpassInitializer::destroy() {
		auto lock = pause_rendering(mQueues, mGframeMutex);

		State state = { };
		destroyFramebuffers(state);
		destroyRpass(state);
		destroyGframes(state, 0);
		destroySwapchain(state);
		destroySurface();
	}


	void Engine::RpassInitializer::initSurface() {
		assert(mPhysDevice != nullptr);
		assert(mSdlWindow  != nullptr);

		{ // Create surface from window
			SDL_bool result = SDL_Vulkan_CreateSurface(mSdlWindow, mVkInstance, &mSurface);
			if(result != SDL_TRUE) {
				const char* err = SDL_GetError();
				throw std::runtime_error(fmt::format(
					"Failed to create a window surface: {}", err ));
			}
		}

		{ // Determine the queue to use for present operations
			bool found = false;
			#define TRY_FAM_(FAM_) \
				if(! found) { \
					VkBool32 supported; \
					VK_CHECK( \
						vkGetPhysicalDeviceSurfaceSupportKHR, \
						mPhysDevice, \
						uint32_t(mQueues.families.FAM_##Index), \
						mSurface, \
						&supported ); \
					if(supported) { \
						mPresentQfamIndex = QfamIndex(mQueues.families.FAM_##Index); \
						mPresentQueue = mQueues.FAM_; \
						spdlog::debug("[+] Queue family {} can be used to present", \
							uint32_t(mQueues.families.FAM_##Index) ); \
					} else { \
						spdlog::debug("[ ] Queue family {} cannot be used to present", \
							uint32_t(mQueues.families.FAM_##Index) ); \
					} \
				}
			TRY_FAM_(graphics)
			TRY_FAM_(transfer)
			TRY_FAM_(compute)
			#undef TRY_FAM_
			spdlog::debug("Using queue family {} for the present queue", uint32_t(mPresentQfamIndex));
		}
	}


	void Engine::RpassInitializer::initSwapchain(State& state) {
		assert(mSurface != nullptr);

		{ // Verify that the surface supports writing to its swapchain images
			static constexpr auto required_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR,
				mPhysDevice, mSurface,
				&mSurfaceCapabs );
			if((mSurfaceCapabs.supportedUsageFlags & required_usage) != required_usage) {
				throw EngineRuntimeError("The Vulkan surface does not support VK_IMAGE_USAGE_TRANSFER_DST_BIT");
			}
		}

		mSurfaceFormat = vkutil::selectSwapchainFormat(mPhysDevice, mSurface);
		select_swapchain_extent(&mPresentExtent, mPrefs.init_present_extent, mSurfaceCapabs);
		mRenderExtent = select_render_extent(mPresentExtent, mPrefs.max_render_extent, mPrefs.upscale_factor);
		spdlog::debug("Chosen render extent {}x{}", mRenderExtent.width, mRenderExtent.height);

		uint32_t concurrent_qfams[] = {
			mQueues.families.graphicsIndex,
			uint32_t(mPresentQfamIndex) };

		VkSwapchainCreateInfoKHR s_info = { };
		s_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		s_info.surface          = mSurface;
		s_info.imageFormat      = mSurfaceFormat.format;
		s_info.imageColorSpace  = mSurfaceFormat.colorSpace;
		s_info.imageExtent      = mPrefs.init_present_extent;
		s_info.imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		s_info.imageArrayLayers = 1;
		s_info.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		s_info.compositeAlpha   = select_composite_alpha(mSurfaceCapabs);
		s_info.presentMode      = select_present_mode(mPhysDevice, mSurface, mPrefs.present_mode);
		s_info.clipped          = VK_TRUE;
		s_info.oldSwapchain     = state.oldSwapchain;
		s_info.pQueueFamilyIndices   = concurrent_qfams;
		s_info.queueFamilyIndexCount = 2;
		s_info.imageSharingMode      =
			(concurrent_qfams[0] == concurrent_qfams[1])?
			VK_SHARING_MODE_EXCLUSIVE :
			VK_SHARING_MODE_CONCURRENT;

		{ // Select min image count
			unsigned desired     = mPrefs.max_concurrent_frames + 1;
			s_info.minImageCount = std::clamp<unsigned>(
				desired,
				mSurfaceCapabs.minImageCount,
				mSurfaceCapabs.maxImageCount );
			if(desired == s_info.minImageCount)
				spdlog::debug("Acquired {} swapchain image{}",              desired, desired == 1? "":"s");
			else
				spdlog::warn("Requested {} swapchain image{}, acquired {}", desired, desired == 1? "":"s", s_info.minImageCount);
		}

		if(s_info.oldSwapchain == nullptr) [[unlikely]] {
			VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, &mSwapchain);
		} else {
			// The old swapchain is retired, but still exists:
			// not destroying it when an exception is thrown may
			// cause a memory leak.
			try {
				VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, &mSwapchain);
			} catch(vkutil::VulkanError& e) {
				vkDestroySwapchainKHR(mDevice, state.oldSwapchain, nullptr);
				throw e;
			}
			vkDestroySwapchainKHR(mDevice, state.oldSwapchain, nullptr);
		}

		{ // Acquire swapchain images
			std::vector<VkImage> images;
			uint32_t count;
			vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, nullptr);
			images.resize(count);
			vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, images.data());

			assert(mSwapchainImages.empty());
			mSwapchainImages.reserve(images.size());
			for(size_t i = 0; i < images.size(); ++i) {
				mSwapchainImages.push_back({ });
				auto& img_data = mSwapchainImages.back();
				auto& img      = images[i];
				img_data.image = img;
			}
		}
	}



	void Engine::RpassInitializer::initGframes(State& state) {
		vkutil::BufferCreateInfo ubo_bc_info = {
			.size  = sizeof(dev::FrameUniform),
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.qfamSharing = { } };

		ssize_t missing = ssize_t(mPrefs.max_concurrent_frames) - ssize_t(mGframes.size());
		if(missing <= 0) return;

		state.createdGframes = true;

		mDescProxy.registerDsetLayout(mFrameUboDsetLayout, { VkDescriptorPoolSize {
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = mPrefs.max_concurrent_frames }});

		VkCommandPoolCreateInfo cpc_info = { };
		cpc_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpc_info.queueFamilyIndex = mQueues.families.graphicsIndex;
		VkCommandBufferAllocateInfo cba_info = { };
		cba_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cba_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		VkCommandBuffer cmd[2];
		cba_info.commandBufferCount = std::size(cmd);
		vkutil::ImageCreateInfo ic_info;
		ic_info.extent = VkExtent3D { mRenderExtent.width, mRenderExtent.height, 1 };
		ic_info.type   = VK_IMAGE_TYPE_2D;
		ic_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ic_info.samples = VK_SAMPLE_COUNT_1_BIT;
		ic_info.tiling  = VK_IMAGE_TILING_OPTIMAL;
		ic_info.qfamSharing  = { };
		ic_info.arrayLayers = 1;
		ic_info.mipLevels   = 1;
		vkutil::AllocationCreateInfo ac_info = { };
		ac_info.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		ac_info.vmaUsage = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
		VkSemaphoreCreateInfo sc_info = { };
		sc_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fc_info = { };
		fc_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fc_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		auto create_frame = [&](GframeData& gf) {
			VK_CHECK(vkCreateCommandPool, mDevice, &cpc_info, nullptr, &gf.cmd_pool);
			cba_info.commandPool = gf.cmd_pool;
			VK_CHECK(vkAllocateCommandBuffers, mDevice, &cba_info, cmd);
			gf.cmd_prepare = cmd[0];
			gf.cmd_draw    = cmd[1];

			gf.frame_dset    = mDescProxy.createToken(mFrameUboDsetLayout);
			gf.frame_ubo     = vkutil::ManagedBuffer::createUniformBuffer(mVma, ubo_bc_info);
			gf.frame_ubo_ptr = gf.frame_ubo.map<dev::FrameUniform>(mVma);

			ic_info.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			ic_info.format = mSurfaceFormat.format;
			gf.atch_color  = vkutil::ManagedImage::create(mVma, ic_info, ac_info);
			ic_info.usage        = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			ic_info.format       = mDepthAtchFmt;
			gf.atch_depthstencil = vkutil::ManagedImage::create(mVma, ic_info, ac_info);

			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_swapchain_image);
			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_prepare);
			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_draw);
			VK_CHECK(vkCreateFence,     mDevice, &fc_info, nullptr, &gf.fence_draw);
		};

		spdlog::trace("Creating {} gframe{}", missing, (missing != 1)? "s" : "");
		for(size_t i = mGframes.size(); i < mPrefs.max_concurrent_frames; ++i) {
			mGframes.resize(i + 1);
			GframeData& gf = mGframes.back();
			create_frame(gf);
		}
	}


	void Engine::RpassInitializer::initRpass(State& state) {
		constexpr size_t COLOR = 0;
		constexpr size_t DEPTH = 1;

		{ // Create the render pass
			VkAttachmentDescription atch_descs[2];
			atch_descs[COLOR] = { };
			atch_descs[COLOR].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			atch_descs[COLOR].samples = VK_SAMPLE_COUNT_1_BIT;

			atch_descs[COLOR].format = mSurfaceFormat.format;
			atch_descs[COLOR].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			atch_descs[COLOR].loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
			atch_descs[COLOR].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			atch_descs[COLOR].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			atch_descs[COLOR].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			atch_descs[DEPTH] = atch_descs[COLOR];
			atch_descs[DEPTH].format = mDepthAtchFmt;
			atch_descs[DEPTH].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			atch_descs[DEPTH].loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
			atch_descs[DEPTH].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			atch_descs[DEPTH].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
			atch_descs[DEPTH].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

			VkAttachmentReference subpass_refs[2];
			subpass_refs[COLOR].attachment = COLOR;
			subpass_refs[COLOR].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			subpass_refs[DEPTH].attachment = DEPTH;
			subpass_refs[DEPTH].layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpasses[1];
			subpasses[0] = { };
			subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpasses[0].colorAttachmentCount    = 1;
			subpasses[0].pColorAttachments       = subpass_refs + COLOR;
			subpasses[0].pDepthStencilAttachment = subpass_refs + DEPTH;

			VkRenderPassCreateInfo rpc_info = { };
			rpc_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpc_info.attachmentCount = std::size(atch_descs);
			rpc_info.pAttachments    = atch_descs;
			rpc_info.subpassCount    = std::size(subpasses);
			rpc_info.pSubpasses      = subpasses;
			rpc_info.dependencyCount = 0;
			rpc_info.pDependencies   = nullptr;

			VK_CHECK(vkCreateRenderPass, mDevice, &rpc_info, nullptr, &mRpass);
		}

		if(! state.reinit) { // Create the pipeline layout(s) and pipeline cache
			VkDescriptorSetLayout layouts[] = { mStaticUboDsetLayout, mFrameUboDsetLayout, mShaderStorageDsetLayout };
			VkPipelineLayoutCreateInfo plc_info = { };
			plc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plc_info.setLayoutCount = /* PLACEHOLDER */ 0; //std::size(layouts);
			plc_info.pSetLayouts    = layouts;
			VK_CHECK(vkCreatePipelineLayout, mDevice, &plc_info, nullptr, &mPipelineLayout);

			VkPipelineCacheCreateInfo pcc_info = { };
			pcc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
			VK_CHECK(vkCreatePipelineCache, mDevice, &pcc_info, nullptr, &mPipelineCache);
		}
	}


	void Engine::RpassInitializer::initFramebuffers(State& state) {
		// Framebuffer initialization only required if the attachments are out of date
		bool ood = state.createdGframes || state.destroyedGframes;
		if(! ood) return;

		constexpr size_t ATCH_COUNT = 2 /* color, depth/stencil */;

		VkImageViewCreateInfo ivc_info = { };
		ivc_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ivc_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivc_info.components.r =
		ivc_info.components.g =
		ivc_info.components.b =
		ivc_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		ivc_info.subresourceRange.layerCount = 1;
		ivc_info.subresourceRange.levelCount = 1;
		VkFramebufferCreateInfo fc_info = { };
		fc_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fc_info.width  = mRenderExtent.width;
		fc_info.height = mRenderExtent.height;
		fc_info.layers = 1;
		fc_info.renderPass = mRpass;
		fc_info.attachmentCount = ATCH_COUNT;

		for(size_t i = 0; i < mPrefs.max_concurrent_frames; ++i) {
			GframeData& gf = mGframes[i];
			ivc_info.image  = gf.atch_color;
			ivc_info.format = mSurfaceFormat.format;
			ivc_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			VK_CHECK(vkCreateImageView, mDevice, &ivc_info, nullptr, &gf.atch_color_view);
			ivc_info.image  = gf.atch_depthstencil;
			ivc_info.format = mDepthAtchFmt;
			ivc_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			VK_CHECK(vkCreateImageView, mDevice, &ivc_info, nullptr, &gf.atch_depthstencil_view);
			VkImageView atchs[ATCH_COUNT] = { gf.atch_color_view, gf.atch_depthstencil_view };
			fc_info.pAttachments = atchs;
			VK_CHECK(vkCreateFramebuffer, mDevice, &fc_info, nullptr, &gf.framebuffer);
		}
	}


	void Engine::RpassInitializer::destroyFramebuffers(State& state) {
		// Framebuffer initialization only required if the attachments are out of date
		bool ood = state.createdGframes || state.destroyedGframes;
		if(! ood) return;

		for(size_t i = 0; i < mPrefs.max_concurrent_frames; ++i) {
			GframeData& gf = mGframes[i];
			vkDestroyFramebuffer(mDevice, gf.framebuffer, nullptr);
			vkDestroyImageView(mDevice, gf.atch_depthstencil_view, nullptr);
			vkDestroyImageView(mDevice, gf.atch_color_view, nullptr);
		}
	}


	void Engine::RpassInitializer::destroyRpass(State& state) {
		if(! state.reinit) {
			vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
			vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
		}
		vkDestroyRenderPass(mDevice, mRpass, nullptr);
	}


	void Engine::RpassInitializer::destroyGframes(State& state, size_t keep) {
		ssize_t excess = ssize_t(mGframes.size()) - ssize_t(keep);
		if(excess <= 0) return;

		state.destroyedGframes = true;

		auto destroy_frame = [&](GframeData& gf) {
			vkDestroyFence     (mDevice, gf.fence_draw, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_draw, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_prepare, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_swapchain_image, nullptr);

			vkutil::ManagedImage::destroy(mVma, gf.atch_color);
			vkutil::ManagedImage::destroy(mVma, gf.atch_depthstencil);

			gf.frame_ubo.unmap(mVma);
			vkutil::Buffer::destroy(mVma, gf.frame_ubo);
			mDescProxy.destroyToken(gf.frame_dset);

			vkDestroyCommandPool(mDevice, gf.cmd_pool, nullptr);
		};

		spdlog::trace("Destroying {} gframe{}", excess, (excess != 1)? "s" : "");
		for(size_t i = keep; i < mGframes.size(); ++i) {
			GframeData& gf = mGframes[i];
			destroy_frame(gf);
		}

		mGframes.resize(std::min(keep, mGframes.size()));
	}


	void Engine::RpassInitializer::destroySwapchain(State& state) {
		if(mSwapchain == nullptr) return;

		mSwapchainImages.clear();

		if(state.reinit) {
			state.oldSwapchain = mSwapchain;
			#warning "delete next line for swapchain salvage"
			vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
		} else {
			vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
		}

		mSwapchain = nullptr;
	}


	void Engine::RpassInitializer::destroySurface() {
		assert(mVkInstance != nullptr);

		if(mSurface != nullptr) {
			vkDestroySurfaceKHR(mVkInstance, mSurface, nullptr);
			mSurface = nullptr;
		}
	}

}
