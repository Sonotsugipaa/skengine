#include "init.hpp"

#include <vk-util/error.hpp>

#include <posixfio_tl.hpp>

#include <memory>

#include <vulkan/vk_enum_string_helper.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <ui-structure/ui.hpp>



namespace SKENGINE_NAME_NS {

	struct Engine::RpassInitializer::State {
		bool reinit         : 1;
		bool createGframes  : 1;
		bool destroyGframes : 1;
	};


	void select_swapchain_extent(
			spdlog::logger&   logger,
			VkExtent2D*       dst,
			const VkExtent2D& desired,
			const VkSurfaceCapabilitiesKHR& capabs,
			bool do_log
	) {
		assert(capabs.maxImageExtent.width  > 0);
		assert(capabs.maxImageExtent.height > 0);
		const auto& capabsMinExt = capabs.minImageExtent;
		const auto& capabsMaxExt = capabs.maxImageExtent;
		dst->width  = std::clamp(desired.width,  capabsMinExt.width,  capabsMaxExt.width);
		dst->height = std::clamp(desired.height, capabsMinExt.height, capabsMaxExt.height);
		if(desired.width != dst->width || desired.height != dst->height) {
			if(do_log) logger.debug("Requested swapchain extent {}x{}, chosen {}x{}",
				desired.width, desired.height,
				dst->width,    dst->height );
		} else {
			if(do_log) logger.debug("Chosen swapchain extent {}x{}", desired.width, desired.height);
		}
	}


	VkCompositeAlphaFlagBitsKHR select_composite_alpha(spdlog::logger& logger, const VkSurfaceCapabilitiesKHR& capabs, bool do_log) {
		assert(capabs.supportedCompositeAlpha != 0);

		#define TRY_CA_(NM_, BIT_) \
			if(capabs.supportedCompositeAlpha & BIT_) { \
				if(do_log) logger.debug("[+] " #NM_ " is supported"); \
				return BIT_; \
			} else { \
				if(do_log) logger.debug("[ ] " #NM_ " is not supported"); \
			}

		TRY_CA_("composite alpha PRE_MULTIPLIED_BIT",  VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
		TRY_CA_("composite alpha POST_MULTIPLIED_BIT", VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
		TRY_CA_("composite alpha INHERIT_BIT",         VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)

		#undef TRY_CA_

		if(do_log) logger.info("[x] Using fallback composite alpha VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR");
		return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	}


	VkPresentModeKHR select_present_mode(
			spdlog::logger&  logger,
			VkPhysicalDevice phys_device,
			VkSurfaceKHR     surface,
			VkPresentModeKHR preferred_mode,
			bool do_log
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
					if(do_log) logger.info("[+] " NM_ "present mode {} is supported", string_VkPresentModeKHR(BIT_)); \
					return BIT_; \
				} else { \
					if(do_log) logger.info("[ ] " NM_ "is not supported"); \
				} \
			}

		TRY_PM_("preferred ", preferred_mode, VK_PRESENT_MODE_MAX_ENUM_KHR)
		TRY_PM_("", VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,     preferred_mode)
		TRY_PM_("", VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR, preferred_mode)
		TRY_PM_("", VK_PRESENT_MODE_MAILBOX_KHR,                   preferred_mode)
		TRY_PM_("", VK_PRESENT_MODE_FIFO_RELAXED_KHR,              preferred_mode)

		#undef TRY_PM_

		if(do_log) logger.info("[x] Using fallback present mode VK_PRESENT_MODE_FIFO_KHR");
		return VK_PRESENT_MODE_FIFO_KHR;
	}


	VkExtent2D select_render_extent(VkExtent2D desired, VkExtent2D limit, float upscale) {
		upscale = std::max(upscale, std::numeric_limits<float>::min());

		desired = {
			uint32_t(std::ceil(desired.width  / upscale)),
			uint32_t(std::ceil(desired.height / upscale)) };
		float desired_fw = desired.width;
		float desired_fh = desired.height;

		if(limit.width == 0) {
			if(limit.height == 0) return desired;
			limit.width = float(limit.height) * desired_fw / desired_fh;
		}
		else if(limit.height == 0) limit.height = float(limit.width) * desired_fh / desired_fw;

		VkExtent2D r = {
			std::clamp<uint32_t>(desired.width,  1, limit.width),
			std::clamp<uint32_t>(desired.height, 1, limit.height) };

		auto select_height_for_width = [&]() { r.height = float(r.width) / float(desired.width) * float(desired.height); };
		auto select_width_for_height = [&]() { r.width = float(r.height) / float(desired.height) * float(desired.width); };

		if(desired.width > limit.width) {
			if(desired.height > limit.height) {
				// Width and height were both too high; change the lowest axis of the desired extent
				if(desired.width < desired.height) select_width_for_height();
				else select_height_for_width();
			} else {
				// Width was too high, height was fine
				select_height_for_width();
			}
		}
		else if(desired.height > limit.height) {
			// Height was too high, width was fine
			select_width_for_height();
		}

		return r;
	}


	void validate_prefs(EnginePreferences& prefs) {
		#define MAX_(M_, MAX_) { prefs.M_ = std::max<decltype(EnginePreferences::M_)>(MAX_, prefs.M_); }
		MAX_(shade_step_count,      0)
		MAX_(shade_step_smoothness, 0)
		#undef MAX_
	}


	#warning "Document how this works, since it's trippy, workaroundy and *definitely* UB (but it removes A LOT of boilerplate)"
	void Engine::RpassInitializer::init(const RpassConfig& rc) {
		mRpassConfig = rc;
		mSwapchainOod = false;
		mHdrEnabled   = false;
		validate_prefs(mPrefs);
		State state = { };
		initSurface();
		initSwapchain(state);
		initGframeDescPool(state);
		initGframes(state);
		initRpasses(state);
		initFramebuffers(state);
	}


	void Engine::RpassInitializer::reinit() {
		logger().trace("Recreating swapchain");

		State state = { };
		state.reinit = true;

		validate_prefs(mPrefs);

		VkExtent2D old_render_xt  = mRenderExtent;
		VkExtent2D old_present_xt = mPresentExtent;

		destroyFramebuffers(state);
		destroySwapchain(state);
		destroyGframes(state);
		initSwapchain(state);
		destroyGframeDescPool(state);
		initGframeDescPool(state);
		initGframes(state);
		initFramebuffers(state);

		bool render_xt_changed =
			(old_render_xt.width  != mRenderExtent.width) ||
			(old_render_xt.height != mRenderExtent.height);
		bool present_xt_changed =
			(old_present_xt.width  != mPresentExtent.width) ||
			(old_present_xt.height != mPresentExtent.height);

		if(render_xt_changed || present_xt_changed) {
			destroyRpasses(state);
			initRpasses(state);
		}
	}


	void Engine::RpassInitializer::destroy() {
		State state = { };
		destroyFramebuffers(state);
		destroyRpasses(state);
		destroySwapchain(state);
		destroyGframes(state);
		destroyGframeDescPool(state);
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
						logger().debug("[+] Queue family {} can be used to present", \
							uint32_t(mQueues.families.FAM_##Index) ); \
					} else { \
						logger().debug("[ ] Queue family {} cannot be used to present", \
							uint32_t(mQueues.families.FAM_##Index) ); \
					} \
				}
			TRY_FAM_(graphics)
			TRY_FAM_(transfer)
			TRY_FAM_(compute)
			#undef TRY_FAM_
			logger().debug("Using queue family {} for the present queue", uint32_t(mPresentQfamIndex));
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

		mSurfaceFormat = vkutil::selectSwapchainFormat(mLogger.get(), mPhysDevice, mSurface);
		select_swapchain_extent(logger(), &mPresentExtent, mPrefs.init_present_extent, mSurfaceCapabs, ! state.reinit);
		mRenderExtent = select_render_extent(mPresentExtent, mPrefs.max_render_extent, mPrefs.upscale_factor);
		if(! state.reinit) logger().debug("Chosen render extent {}x{}", mRenderExtent.width, mRenderExtent.height);

		mProjTransf = glm::perspective<float>(
			mPrefs.fov_y,
			float(mRenderExtent.width) / float(mRenderExtent.height),
			mPrefs.z_near, mPrefs.z_far );
		mProjTransf[1][1] *= -1.0f; // Clip +y is view -y

		uint32_t concurrent_qfams[] = {
			mQueues.families.graphicsIndex,
			uint32_t(mPresentQfamIndex) };

		mPrefs.init_present_extent = {
			std::clamp(mPrefs.init_present_extent.width,  mSurfaceCapabs.minImageExtent.width,  mSurfaceCapabs.maxImageExtent.width),
			std::clamp(mPrefs.init_present_extent.height, mSurfaceCapabs.minImageExtent.height, mSurfaceCapabs.maxImageExtent.height) };

		if(mSwapchainOld != nullptr) {
			vkDestroySwapchainKHR(mDevice, mSwapchainOld, nullptr);
		}

		VkSwapchainCreateInfoKHR s_info = { }; {
			s_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			s_info.surface          = mSurface;
			s_info.imageFormat      = mSurfaceFormat.format;
			s_info.imageColorSpace  = mSurfaceFormat.colorSpace;
			s_info.imageExtent      = mPrefs.init_present_extent;
			s_info.imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			s_info.imageArrayLayers = 1;
			s_info.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
			s_info.compositeAlpha   = (! mPrefs.composite_alpha)? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : select_composite_alpha(logger(), mSurfaceCapabs, ! state.reinit);
			s_info.presentMode      = select_present_mode(logger(), mPhysDevice, mSurface, mPrefs.present_mode, ! state.reinit);
			s_info.clipped          = VK_TRUE;
			s_info.oldSwapchain     = mSwapchain;
			s_info.pQueueFamilyIndices   = concurrent_qfams;
			s_info.queueFamilyIndexCount = 2;
			s_info.imageSharingMode      =
				(concurrent_qfams[0] == concurrent_qfams[1])?
				VK_SHARING_MODE_EXCLUSIVE :
				VK_SHARING_MODE_CONCURRENT;
		}

		{ // Select min image count
			unsigned desired     = mPrefs.max_concurrent_frames + 1;
			s_info.minImageCount = std::clamp<unsigned>(
				desired,
				mSurfaceCapabs.minImageCount,
				mSurfaceCapabs.maxImageCount );
			logger().trace("Requesting {} swapchain image{}", s_info.minImageCount, s_info.minImageCount == 1? "":"s");
		}

		mSwapchainOld = mSwapchain;

		if(mSwapchainOld == nullptr) [[unlikely]] {
			VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, &mSwapchain);
		} else {
			// The old swapchain is retired, but still exists:
			// not destroying it when an exception is thrown may
			// cause a memory leak.
			try {
				VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, &mSwapchain);
			} catch(vkutil::VulkanError& e) {
				if(mSwapchainOld != nullptr) {
					vkDestroySwapchainKHR(mDevice, mSwapchainOld, nullptr);
				}
				throw e;
			}
		}

		{ // Acquire swapchain images
			std::vector<VkImage> images;
			uint32_t count;
			vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, nullptr);
			images.resize(count);
			vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, images.data());
			logger().debug("Acquired {} swapchain image{}", count, count == 1? "":"s");

			assert(mGframes.empty() || state.reinit);
			if(mGframes.size() < images.size()) state.createGframes  = true;
			else
			if(mGframes.size() > images.size()) state.destroyGframes = true;
			mGframes.resize(images.size());
			for(size_t i = 0; i < images.size(); ++i) {
				auto& img_data = mGframes[i];
				auto& img      = images[i];
				img_data.swapchain_image = img;
				VkImageViewCreateInfo ivc_info = { };
				ivc_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				ivc_info.image = img;
				ivc_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
				ivc_info.format   = mSurfaceFormat.format;
				ivc_info.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
				ivc_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ivc_info.subresourceRange.layerCount = 1;
				ivc_info.subresourceRange.levelCount = 1;
				VK_CHECK(vkCreateImageView, mDevice, &ivc_info, nullptr, &img_data.swapchain_image_view);
			}
		}
	}


	void Engine::RpassInitializer::initGframeDescPool(State& state) {
		// Descriptor pool initialization only required if gframes will be created or deleted
		bool ood = state.createGframes || state.destroyGframes;
		if(state.reinit && ! ood) return;

		assert(! mGframes.empty() /* The gframe vector should be resized by the swapchain init procedure */);
		auto frame_n = uint32_t(mGframes.size());

		VkDescriptorPoolSize sizes[] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frame_n * 1 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, frame_n * 1 } };

		VkDescriptorPoolCreateInfo dpc_info = { };
		dpc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		dpc_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		dpc_info.poolSizeCount = std::size(sizes);
		dpc_info.pPoolSizes    = sizes;

		dpc_info.maxSets = 0;
		for(auto& sz : sizes) dpc_info.maxSets += sz.descriptorCount;

		VK_CHECK(vkCreateDescriptorPool, mDevice, &dpc_info, nullptr, &mGframeDescPool);
	}


	void Engine::RpassInitializer::initGframes(State&) {
		uint32_t light_storage_capacity = mWorldRenderer.lightStorage().bufferCapacity;

		assert(! mGframes.empty());
		auto frame_n = mGframes.size();

		vkutil::BufferCreateInfo ubo_bc_info = {
			.size  = sizeof(dev::FrameUniform),
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.qfamSharing = { } };
		vkutil::BufferCreateInfo light_storage_bc_info = {
			.size  = light_storage_capacity * sizeof(dev::Light),
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.qfamSharing = { } };

		VkCommandPoolCreateInfo cpc_info = { };
		cpc_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpc_info.queueFamilyIndex = mQueues.families.graphicsIndex;
		VkCommandBufferAllocateInfo cba_info = { };
		cba_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cba_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		VkCommandBuffer cmd[2];
		cba_info.commandBufferCount = std::size(cmd);
		VkDescriptorSetAllocateInfo dsa_info = { };
		dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsa_info.descriptorPool     = mGframeDescPool;
		dsa_info.descriptorSetCount = 1;
		dsa_info.pSetLayouts        = &mGframeDsetLayout;
		VkDescriptorBufferInfo frame_db_info = { };
		frame_db_info.range = VK_WHOLE_SIZE;
		VkDescriptorBufferInfo light_db_info = frame_db_info;
		light_db_info.range = light_storage_bc_info.size;
		VkWriteDescriptorSet dset_wr[2];
		dset_wr[FRAME_UBO_BINDING] = { };
		dset_wr[FRAME_UBO_BINDING].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		dset_wr[FRAME_UBO_BINDING].dstBinding = FRAME_UBO_BINDING;
		dset_wr[FRAME_UBO_BINDING].descriptorCount = 1;
		dset_wr[FRAME_UBO_BINDING].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		dset_wr[FRAME_UBO_BINDING].pBufferInfo     = &frame_db_info;
		dset_wr[LIGHT_STORAGE_BINDING] = dset_wr[FRAME_UBO_BINDING];
		dset_wr[LIGHT_STORAGE_BINDING].dstBinding  = LIGHT_STORAGE_BINDING;
		dset_wr[LIGHT_STORAGE_BINDING].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		dset_wr[LIGHT_STORAGE_BINDING].pBufferInfo = &light_db_info;
		vkutil::ImageCreateInfo ic_info = { };
		ic_info.extent = VkExtent3D { mRenderExtent.width, mRenderExtent.height, 1 };
		ic_info.type   = VK_IMAGE_TYPE_2D;
		ic_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ic_info.samples = VK_SAMPLE_COUNT_1_BIT;
		ic_info.tiling  = VK_IMAGE_TILING_OPTIMAL;
		ic_info.qfamSharing = { };
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

		auto create_frame = [&](GframeData& gf, VkFence& gff) {
			VK_CHECK(vkCreateCommandPool, mDevice, &cpc_info, nullptr, &gf.cmd_pool);
			cba_info.commandPool = gf.cmd_pool;
			VK_CHECK(vkAllocateCommandBuffers, mDevice, &cba_info, cmd);
			gf.cmd_prepare = cmd[0];
			gf.cmd_draw    = cmd[1];

			gf.frame_ubo     = vkutil::BufferDuplex::createUniformBuffer(mVma, ubo_bc_info);
			gf.light_storage = vkutil::ManagedBuffer::createStorageBuffer(mVma, light_storage_bc_info);
			gf.light_storage_capacity = light_storage_capacity;

			VK_CHECK(vkAllocateDescriptorSets, mDevice, &dsa_info, &gf.frame_dset);
			for(auto& wr : dset_wr) wr.dstSet = gf.frame_dset;
			frame_db_info.buffer = gf.frame_ubo;
			light_db_info.buffer = gf.light_storage;
			vkUpdateDescriptorSets(mDevice, std::size(dset_wr), dset_wr, 0, nullptr);

			ic_info.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			ic_info.format = mSurfaceFormat.format;
			gf.atch_color  = vkutil::ManagedImage::create(mVma, ic_info, ac_info);
			ic_info.usage        = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			ic_info.format       = mDepthAtchFmt;
			gf.atch_depthstencil = vkutil::ManagedImage::create(mVma, ic_info, ac_info);

			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_prepare);
			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_draw);
			VK_CHECK(vkCreateFence,     mDevice, &fc_info, nullptr, &gf.fence_prepare);
			VK_CHECK(vkCreateFence,     mDevice, &fc_info, nullptr, &gf.fence_draw);
			VK_CHECK(vkCreateFence,     mDevice, &fc_info, nullptr, &gff);
		};

		logger().trace("Creating {} gframe{}", frame_n, (frame_n != 1)? "s" : "");
		mGframeSelectionFences.resize(frame_n);
		for(size_t i = 0; i < frame_n; ++i) {
			GframeData& gf  = mGframes[i];
			VkFence&    gff = mGframeSelectionFences[i];
			create_frame(gf, gff);
		}
	}


	void Engine::RpassInitializer::initRpasses(State& state) {
		constexpr size_t COLOR = 0;
		constexpr size_t DEPTH = 1;

		{ // Create the render passes
			VkAttachmentDescription atch_descs[2]; {
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
				atch_descs[DEPTH].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				atch_descs[DEPTH].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			}

			VkAttachmentReference subpass_refs[2]; {
				subpass_refs[COLOR].attachment = COLOR;
				subpass_refs[COLOR].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				subpass_refs[DEPTH].attachment = DEPTH;
				subpass_refs[DEPTH].layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			}

			VkSubpassDescription subpasses[1]; {
				subpasses[0] = { };
				subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				subpasses[0].colorAttachmentCount    = 1;
				subpasses[0].pColorAttachments       = subpass_refs + COLOR;
				subpasses[0].pDepthStencilAttachment = subpass_refs + DEPTH;
			}

			VkRenderPassCreateInfo rpc_info = { }; {
				rpc_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpc_info.attachmentCount = std::size(atch_descs);
				rpc_info.pAttachments    = atch_descs;
				rpc_info.subpassCount    = std::size(subpasses);
				rpc_info.pSubpasses      = subpasses;
			}

			VK_CHECK(vkCreateRenderPass, mDevice, &rpc_info, nullptr, &mWorldRpass);

			{ // Recycle the previous infos for the UI render pass
				atch_descs[COLOR].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				atch_descs[COLOR].format = mSurfaceFormat.format;
				atch_descs[COLOR].loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
				atch_descs[COLOR].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				subpass_refs[COLOR].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				subpasses[0].pDepthStencilAttachment = nullptr;

				// No depth attachment, only color
				rpc_info.attachmentCount = 1;
				rpc_info.pAttachments    = atch_descs + COLOR;
			}

			VkSubpassDependency deps[1] = { }; {
				deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
				deps[0].dstSubpass = 0;
				deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
				deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				rpc_info.dependencyCount = std::size(deps);
				rpc_info.pDependencies   = deps;
			}

			VK_CHECK(vkCreateRenderPass, mDevice, &rpc_info, nullptr, &mUiRpass);
		}

		if(! state.reinit) { // Create the pipeline layouts, pipeline cache and pipelines
			VkDescriptorSetLayout layouts[] = { mGframeDsetLayout, mMaterialDsetLayout };
			VkPipelineLayoutCreateInfo plc_info = { };
			plc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plc_info.setLayoutCount = std::size(layouts);
			plc_info.pSetLayouts    = layouts;
			VK_CHECK(vkCreatePipelineLayout, mDevice, &plc_info, nullptr, &mPipelineLayout);

			VkPipelineCacheCreateInfo pcc_info = { };
			pcc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
			VK_CHECK(vkCreatePipelineCache, mDevice, &pcc_info, nullptr, &mPipelineCache);

			mGenericGraphicsPipeline = createPipeline("default", mWorldRpass);

			geom::PipelineSetCreateInfo gpsci = { };
			gpsci.subpass        = 0;
			gpsci.renderPass     = mUiRpass;
			gpsci.pipelineCache  = mPipelineCache;
			gpsci.polyDsetLayout = nullptr;
			gpsci.textDsetLayout = mGuiDsetLayout;
			mGeomPipelines = geom::PipelineSet::create(mDevice, gpsci);
		}

		{ // Hardcoded GUI
			constexpr float chSize = 50.0f;
			float ratio = float(mPresentExtent.height) / float(mPresentExtent.width);
			float hSize = chSize / float(mPresentExtent.height);
			float wSize = hSize * ratio;
			float wComp = 0.5f * (hSize - wSize);
			float chBlank = (1.0 - hSize) / 2.0;
			mUiCanvas = ui::Canvas(
				ComputedBounds { 0.0, 0.0, 1.0, 1.0 },
				{ chBlank,       hSize, chBlank },
				{ chBlank+wComp, wSize, chBlank+wComp } );
			// // Uncomment the next two lines (and delete this one) when the placeholder character is removed
			// auto ch = std::make_shared<gui::Cross>(mVma, 1.0f, 0.1f, glm::vec4 { 0.8f, 0.8f, 0.8f, 0.6f });
			// mUiCanvas.createLot({ 1, 1 }, { 1, 1 }).second->createElement(ch);
			{
				mPlaceholderChar = std::make_shared<gui::PlaceholderChar>(mVma, 0);
				auto gridCharLot = mUiCanvas.createLot({ 1, 1 }, { 1, 1 });
				gridCharLot.second->createElement(mPlaceholderChar);
				gridCharLot.second->createElement(std::make_shared<gui::Frame>(mVma, 0.05, glm::vec4 { 0.5f, 0.5f, 0.5f, 0.9f }));
				auto tcView = std::make_shared<gui::PlaceholderTextCacheView>(mVma, mPlaceholderTextCache);
				auto tcViewLot = mUiCanvas.createLot({ 0, 2 }, { 1, 1 });
				tcViewLot.second->createElement(tcView);
			}
		}
	}


	void Engine::RpassInitializer::initFramebuffers(State&) {
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
		fc_info.layers = 1;

		for(size_t i = 0; i < mGframes.size(); ++i) {
			GframeData& gf = mGframes[i];
			ivc_info.image  = gf.atch_color;
			ivc_info.format = mSurfaceFormat.format;
			ivc_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			VK_CHECK(vkCreateImageView, mDevice, &ivc_info, nullptr, &gf.atch_color_view);
			ivc_info.image  = gf.atch_depthstencil;
			ivc_info.format = mDepthAtchFmt;
			ivc_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			VK_CHECK(vkCreateImageView, mDevice, &ivc_info, nullptr, &gf.atch_depthstencil_view);

			{ // Create the world rpass framebuffer
				VkImageView atchs[] = { gf.atch_color_view, gf.atch_depthstencil_view };
				fc_info.renderPass = mWorldRpass;
				fc_info.attachmentCount = std::size(atchs);
				fc_info.pAttachments    = atchs;
				fc_info.width  = mRenderExtent.width;
				fc_info.height = mRenderExtent.height;
				VK_CHECK(vkCreateFramebuffer, mDevice, &fc_info, nullptr, &gf.worldFramebuffer);
			}

			{ // Create the world rpass framebuffer
				VkImageView atchs[] = { gf.swapchain_image_view };
				fc_info.renderPass = mUiRpass;
				fc_info.attachmentCount = std::size(atchs);
				fc_info.pAttachments    = atchs;
				fc_info.width  = mPresentExtent.width;
				fc_info.height = mPresentExtent.height;
				VK_CHECK(vkCreateFramebuffer, mDevice, &fc_info, nullptr, &gf.uiFramebuffer);
			}
		}
	}


	void Engine::RpassInitializer::destroyFramebuffers(State&) {
		for(size_t i = 0; i < mGframes.size(); ++i) {
			GframeData& gf = mGframes[i];
			vkDestroyFramebuffer(mDevice, gf.uiFramebuffer, nullptr);
			vkDestroyFramebuffer(mDevice, gf.worldFramebuffer, nullptr);
			vkDestroyImageView(mDevice, gf.atch_depthstencil_view, nullptr);
			vkDestroyImageView(mDevice, gf.atch_color_view, nullptr);
		}
	}


	void Engine::RpassInitializer::destroyRpasses(State& state) {
		if(! state.reinit) {
mPlaceholderChar = nullptr;
			mUiCanvas = { };
			geom::PipelineSet::destroy(mDevice, mGeomPipelines);
			vkDestroyPipeline(mDevice, mGenericGraphicsPipeline, nullptr);
			vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
			vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
		}
		vkDestroyRenderPass(mDevice, mUiRpass, nullptr);
		vkDestroyRenderPass(mDevice, mWorldRpass, nullptr);
	}


	void Engine::RpassInitializer::destroyGframeDescPool(State& state) {
		// Descriptor pool destruction only required if gframes have been created or deleted
		bool ood = state.createGframes || state.destroyGframes;
		if(state.reinit && ! ood) return;

		vkDestroyDescriptorPool(mDevice, mGframeDescPool, nullptr);
	}


	void Engine::RpassInitializer::destroySwapchain(State& state) {
		if(mSwapchain == nullptr) return;

		for(auto& gf : mGframes) vkDestroyImageView(mDevice, gf.swapchain_image_view, nullptr);
		if(! state.reinit) [[unlikely]] {
			if(mSwapchainOld != nullptr) vkDestroySwapchainKHR(mDevice, mSwapchainOld, nullptr);
		}
		vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);

		mSwapchain = nullptr;
	}


	void Engine::RpassInitializer::destroyGframes(State& state) {
		state.destroyGframes = true;

		auto destroy_frame = [&](GframeData& gf, VkFence& gframe_sel_fence) {
			vkDestroyFence     (mDevice, gframe_sel_fence, nullptr);
			vkDestroyFence     (mDevice, gf.fence_draw, nullptr);
			vkDestroyFence     (mDevice, gf.fence_prepare, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_draw, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_prepare, nullptr);

			vkutil::ManagedImage::destroy(mVma, gf.atch_color);
			vkutil::ManagedImage::destroy(mVma, gf.atch_depthstencil);

			VK_CHECK(vkFreeDescriptorSets, mDevice, mGframeDescPool, 1, &gf.frame_dset);
			vkutil::ManagedBuffer::destroy(mVma, gf.light_storage);
			vkutil::BufferDuplex::destroy(mVma, gf.frame_ubo);

			vkDestroyCommandPool(mDevice, gf.cmd_pool, nullptr);
		};

		logger().trace("Destroying {} gframe{}", mGframes.size(), (mGframes.size() != 1)? "s" : "");
		for(size_t i = 0; i < mGframes.size(); ++i) {
			GframeData& gf  = mGframes[i];
			VkFence&    gff = mGframeSelectionFences[i];
			destroy_frame(gf, gff);
		}
	}


	void Engine::RpassInitializer::destroySurface() {
		assert(mVkInstance != nullptr);

		if(mSurface != nullptr) {
			vkDestroySurfaceKHR(mVkInstance, mSurface, nullptr);
			mSurface = nullptr;
		}
	}

}
