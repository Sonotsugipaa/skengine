#include "init.hpp"

#include <vk-util/error.hpp>

#include <posixfio_tl.hpp>

#include <memory>

#include <vulkan/vk_enum_string_helper.h>

#include <ui-structure/ui.hpp>



namespace SKENGINE_NAME_NS {

	using stages_t = uint_fast8_t;


	struct Engine::RpassInitializer::State {
		ConcurrentAccess& concurrentAccess;
		stages_t stages;
		bool reinit         : 1;
		bool createGframes  : 1;
		bool destroyGframes : 1;
		State(ConcurrentAccess& ca, bool reinit, stages_t stages = ~ stages_t(0)): concurrentAccess(ca), stages(stages), reinit(reinit) { }
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
		MAX_(shade_step_count, 0)
		MAX_(dithering_steps,  0)
		#undef MAX_

		#define UL_ [[unlikely]]
		if(prefs.shade_step_smoothness < 0.0f) UL_ prefs.shade_step_smoothness = -1.0f - (-1.0f / -(-1.0f + prefs.shade_step_smoothness)); // Negative values (interval (-1, 0)) behave strangely
		#undef UL_
	}


	#warning "Document how this works, since it's trippy, workaroundy and *definitely* UB (but it removes A LOT of boilerplate)"
	void Engine::RpassInitializer::init(ConcurrentAccess& ca, const RpassConfig& rc) {
		#define SET_STAGE_(B_) state.stages = state.stages | (stages_t(1) << stages_t(B_));
		mRpassConfig = rc;
		mSwapchainOod = false;
		mHdrEnabled   = false;
		validate_prefs(mPrefs);
		auto state = State(ca, false, 0);
		try {
			initSurface(); SET_STAGE_(0)
			initSwapchain(state); SET_STAGE_(1)
			initRenderers(state); SET_STAGE_(2)
			initGframes(state); SET_STAGE_(3)
			initRpasses(state); SET_STAGE_(3)
			initFramebuffers(state); SET_STAGE_(4)
			initTop(state); SET_STAGE_(5)
		} catch(...) {
			unwind(state);
			std::rethrow_exception(std::current_exception());
		}
		#undef SET_STAGE_
	}


	void Engine::RpassInitializer::reinit(ConcurrentAccess& ca) {
		#define SET_STAGE_(B_) state.stages = state.stages | (stages_t(1) << stages_t(B_));
		#define UNSET_STAGE_(B_) state.stages = state.stages & (~ (stages_t(1) << stages_t(B_)));
		logger().trace("Recreating swapchain");

		auto state = State(ca, true);

		validate_prefs(mPrefs);

		VkExtent2D old_render_xt  = mRenderExtent;
		VkExtent2D old_present_xt = mPresentExtent;

		try {
			destroyTop(state); UNSET_STAGE_(6)
			destroyFramebuffers(state); UNSET_STAGE_(5)
			destroySwapchain(state); UNSET_STAGE_(1)
			destroyGframes(state); UNSET_STAGE_(3)
			destroyRenderers(state); UNSET_STAGE_(2)
			initSwapchain(state); SET_STAGE_(1)
			initRenderers(state); SET_STAGE_(2)
			initGframes(state); SET_STAGE_(3)
			initFramebuffers(state); SET_STAGE_(5)

			bool render_xt_changed =
				(old_render_xt.width  != mRenderExtent.width) ||
				(old_render_xt.height != mRenderExtent.height);
			bool present_xt_changed =
				(old_present_xt.width  != mPresentExtent.width) ||
				(old_present_xt.height != mPresentExtent.height);

			if(render_xt_changed || present_xt_changed) {
				destroyRpasses(state); UNSET_STAGE_(4)
				initRpasses(state); SET_STAGE_(4)
			}

			initTop(state); SET_STAGE_(6)
		} catch(...) {
			state.reinit = false;
			unwind(state);
			std::rethrow_exception(std::current_exception());
		}
		#undef SET_STAGE_
		#undef UNSET_STAGE_
	}


	void Engine::RpassInitializer::destroy(ConcurrentAccess& ca) {
		auto state = State(ca, false);
		unwind(state);
	}


	void Engine::RpassInitializer::unwind(State& state) {
		#define IF_STAGE_(B_) if(0 != (state.stages & (stages_t(1) << stages_t(B_))))
		IF_STAGE_(6) destroyTop(state);
		IF_STAGE_(5) destroyFramebuffers(state);
		IF_STAGE_(4) destroyRpasses(state);
		IF_STAGE_(1) destroySwapchain(state);
		IF_STAGE_(3) destroyGframes(state);
		IF_STAGE_(2) destroyRenderers(state);
		IF_STAGE_(0) destroySurface();
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
						found = true; \
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

		mSurfaceFormat = vkutil::selectSwapchainFormat(state.reinit? nullptr : mLogger.get(), mPhysDevice, mSurface);
		select_swapchain_extent(logger(), &mPresentExtent, mPrefs.init_present_extent, mSurfaceCapabs, ! state.reinit);
		mRenderExtent = select_render_extent(mPresentExtent, mPrefs.max_render_extent, mPrefs.upscale_factor);
		if(! state.reinit) logger().debug("Chosen render extent {}x{}", mRenderExtent.width, mRenderExtent.height);

		uint32_t concurrent_qfams[] = {
			mQueues.families.graphicsIndex,
			uint32_t(mPresentQfamIndex) };

		mPrefs.init_present_extent = {
			std::clamp(mPrefs.init_present_extent.width,  mSurfaceCapabs.minImageExtent.width,  mSurfaceCapabs.maxImageExtent.width),
			std::clamp(mPrefs.init_present_extent.height, mSurfaceCapabs.minImageExtent.height, mSurfaceCapabs.maxImageExtent.height) };

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
			if(concurrent_qfams[0] == concurrent_qfams[1]) {
				s_info.queueFamilyIndexCount = 0;
				s_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
				logger().trace("Swapchain uses one queue family => exclusive sharing mode");
			} else {
				s_info.queueFamilyIndexCount = 2;
				s_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
				logger().trace("Swapchain uses queue families {} and {} => concurrent sharing mode", concurrent_qfams[0], concurrent_qfams[1]);
			}
		}

		{ // Select min image count
			unsigned desired = mPrefs.max_concurrent_frames + 1;
			unsigned max = (mSurfaceCapabs.maxImageCount == 0 /* 0 => no limit */)?
				std::max(desired, mSurfaceCapabs.minImageCount) :
				mSurfaceCapabs.maxImageCount;
			s_info.minImageCount = std::clamp<unsigned>(
				desired,
				mSurfaceCapabs.minImageCount,
				max );
			logger().trace("Requesting {} swapchain image{}", s_info.minImageCount, s_info.minImageCount == 1? "":"s");
		}


		// Bandaid fix for vkCreateSwapchainKHR randomly throwing VK_ERROR_UNKNOWN around
		auto tryCreateSwapchain = [&](VkSwapchainKHR* dst) {
			for(auto i = 0u; i < 3u; ++i) {
				try {
					VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, dst);
					mLogger->trace("vkCreateSwapchainKHR -> VK_SUCCESS (attempt n. {})", i+1);
					return;
				} catch(vkutil::VulkanError& err) {
					mLogger->error("{} ({}) (attempt n. {})", err.what(), int32_t(err.vkResult()), i+1);
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				s_info.oldSwapchain = nullptr;
				mLogger->flush();
			}
			VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, dst);
		};
		auto tryDestroySwapchain = [&]() {
			if(s_info.oldSwapchain != nullptr) vkDestroySwapchainKHR(mDevice, s_info.oldSwapchain, nullptr);
		};

		VkSwapchainKHR newSc;
		if(mSwapchain == nullptr) [[unlikely]] {
			assert(! state.reinit);
			tryCreateSwapchain(&newSc);
		} else {
			assert(state.reinit);
			// The old swapchain is retired, but still exists:
			// not destroying it when an exception is thrown may
			// cause a memory leak.
			try {
				tryCreateSwapchain(&newSc);
			} catch(vkutil::VulkanError& e) {
				tryDestroySwapchain();
				throw e;
			}
			tryDestroySwapchain();
		}
		mSwapchain = newSc;

		{ // Acquire swapchain images
			std::vector<VkImage> images;
			uint32_t count = 0;
			vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, nullptr);
			images.resize(count);
			vkGetSwapchainImagesKHR(mDevice, mSwapchain, &count, images.data());
			logger().trace("Acquired {} swapchain image{}", count, count == 1? "":"s");

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

		mSwapchainOod = false;
	}


	void Engine::RpassInitializer::initRenderers(State& state) {
		if(! state.reinit) {
			{ // 3D material dset layout, currently hardcodedly redundant with world_renderer.cpp:world_renderer_subpass_info
				VkDescriptorSetLayoutBinding dslb[5] = { };
				dslb[0].binding = DIFFUSE_TEX_BINDING;
				dslb[0].descriptorCount = 1;
				dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
				dslb[1] = dslb[0];
				dslb[1].binding = NORMAL_TEX_BINDING;
				dslb[2] = dslb[0];
				dslb[2].binding = SPECULAR_TEX_BINDING;
				dslb[3] = dslb[0];
				dslb[3].binding = EMISSIVE_TEX_BINDING;
				dslb[4] = dslb[0];
				dslb[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				dslb[4].binding = MATERIAL_UBO_BINDING;

				VkDescriptorSetLayoutCreateInfo dslc_info = { };
				dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				dslc_info.bindingCount = std::size(dslb);
				dslc_info.pBindings = dslb;
				VK_CHECK(vkCreateDescriptorSetLayout, mDevice, &dslc_info, nullptr, &m3dPipelineMaterialDsetLayout);
			}

			mObjectStorage = std::make_shared<ObjectStorage>(ObjectStorage::create(
				std::make_shared<spdlog::logger>(logger()),
				mVma,
				m3dPipelineMaterialDsetLayout,
				mAssetSupplier ));

			auto wrUptr = std::make_unique<WorldRenderer>(WorldRenderer::create(
				std::make_shared<spdlog::logger>(logger()),
				mObjectStorage,
				WorldRenderer::ProjectionInfo {
					.verticalFov = mPrefs.fov_y,
					.zNear       = mPrefs.z_near,
					.zFar        = mPrefs.z_far } ));
			mWorldRenderer_TMP_UGLY_NAME = wrUptr.get();
			mRenderers.emplace_back(std::move(wrUptr));

			auto uiUptr = std::make_unique<UiRenderer>(UiRenderer::create(
				std::make_shared<spdlog::logger>(logger()),
				mUiStorage ));
			mUiRenderer_TMP_UGLY_NAME = uiUptr.get();
			mRenderers.emplace_back(std::move(uiUptr));
		}

		assert(! mGframes.empty());
		auto frame_n = mGframes.size();
		for(auto& r : mRenderers) r->afterSwapchainCreation(state.concurrentAccess, frame_n);
	}


	void Engine::RpassInitializer::initGframes(State&) {
		assert(! mGframes.empty());
		auto frame_n = mGframes.size();

		mGframeLast = -1;

		VkCommandPoolCreateInfo cpc_info = { };
		cpc_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpc_info.queueFamilyIndex = mQueues.families.graphicsIndex;
		VkCommandBufferAllocateInfo cba_info = { };
		cba_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cba_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		VkCommandBuffer cmd[3];
		cba_info.commandBufferCount = std::size(cmd);
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
			memcpy(gf.cmd_draw, cmd+1, sizeof(gf.cmd_draw));

			ic_info.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			ic_info.format = mSurfaceFormat.format;
			gf.atch_color  = vkutil::ManagedImage::create(mVma, ic_info, ac_info);
			ic_info.usage        = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			ic_info.format       = mDepthAtchFmt;
			gf.atch_depthstencil = vkutil::ManagedImage::create(mVma, ic_info, ac_info);

			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_prepare);
			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_drawWorld);
			VK_CHECK(vkCreateSemaphore, mDevice, &sc_info, nullptr, &gf.sem_drawGui);
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

		if(mRenderers.empty()) return;

		VkAttachmentReference subpass_refs[2]; {
			subpass_refs[COLOR].attachment = COLOR;
			subpass_refs[COLOR].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			subpass_refs[DEPTH].attachment = DEPTH;
			subpass_refs[DEPTH].layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		using SpassDescs = std::vector<VkSubpassDescription>;
		using SpassDeps  = std::vector<VkSubpassDependency>;
		size_t subpassCountHeuristic = (mRenderers.size() * 3) / 4;
		SpassDescs subpassDescsWorld; subpassDescsWorld.reserve(subpassCountHeuristic);
		SpassDescs subpassDescsUi;    subpassDescsUi   .reserve(subpassCountHeuristic);
		SpassDeps subpassDepsWorld;   subpassDescsWorld.reserve(subpassCountHeuristic);
		SpassDeps subpassDepsUi;      subpassDescsUi   .reserve(subpassCountHeuristic);
		{ // Create subpass descriptions and references
			VkSubpassDescription spDescTemplate = { };
			spDescTemplate.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
			spDescTemplate.colorAttachmentCount    = 1;
			spDescTemplate.pColorAttachments       = subpass_refs + COLOR;
			spDescTemplate.pDepthStencilAttachment = subpass_refs + DEPTH;
			VkSubpassDependency spDepTemplate = { };
			spDepTemplate.srcSubpass = VK_SUBPASS_EXTERNAL;
			spDepTemplate.dstSubpass = 0;
			spDepTemplate.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			spDepTemplate.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			spDepTemplate.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			spDepTemplate.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			subpassDepsUi.push_back(spDepTemplate);
			spDepTemplate.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			spDepTemplate.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			spDepTemplate.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			spDepTemplate.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			auto insertInfo = [&](SpassDescs& descDst, SpassDeps& depDst, std::string_view name) {
				descDst.push_back(spDescTemplate);
				mLogger->trace("Creating subpass for renderer \"{}\"{}", name, (descDst.size() >= 2)? " (dependent on the previous subpass)" : " (no dependency)");
				if(descDst.size() >= 2 /* There can only be a dependency between two things */) [[unlikely]] {
					depDst.push_back(spDepTemplate);
					depDst.back().srcSubpass = descDst.size() - 2;
					depDst.back().dstSubpass = descDst.size() - 1;
				}
			};
			for(auto& r : mRenderers) {
				auto& spInfo = r->pipelineInfo();
				switch(spInfo.rpass) {
					default: mLogger->error("Renderer \"{}\" targets an unexistent render pass"); break;
					case Renderer::RenderPass::eWorld: insertInfo(subpassDescsWorld, subpassDepsWorld, r->name()); break;
					case Renderer::RenderPass::eUi:    insertInfo(subpassDescsUi,    subpassDepsUi, r->name()); subpassDescsUi.back().pDepthStencilAttachment = nullptr; break;
				}
			}
			mLogger->trace("Rpass 0 has {} subpass{}", subpassDescsWorld.size(), subpassDescsWorld.size() == 1? "":"es");
			mLogger->trace("Rpass 1 has {} subpass{}", subpassDescsUi   .size(), subpassDescsUi   .size() == 1? "":"es");
			for(auto& dep : subpassDepsWorld) mLogger->trace("Rpass 0 dependency: {} -> {}", dep.srcSubpass, dep.dstSubpass);
			for(auto& dep : subpassDepsUi)    mLogger->trace("Rpass 1 dependency: {} -> {}", dep.srcSubpass, dep.dstSubpass);
		}

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

			VkRenderPassCreateInfo rpc_info = { }; {
				rpc_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				rpc_info.attachmentCount = std::size(atch_descs);
				rpc_info.pAttachments    = atch_descs;
				rpc_info.dependencyCount = subpassDepsWorld.size();
				rpc_info.pDependencies   = subpassDepsWorld.data();
				rpc_info.subpassCount    = subpassDescsWorld.size();
				rpc_info.pSubpasses      = subpassDescsWorld.data();
			}

			VK_CHECK(vkCreateRenderPass, mDevice, &rpc_info, nullptr, &mWorldRpass);

			{ // Recycle the previous infos for the UI render pass
				atch_descs[COLOR].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				atch_descs[COLOR].format = mSurfaceFormat.format;
				atch_descs[COLOR].loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
				atch_descs[COLOR].storeOp = VK_ATTACHMENT_STORE_OP_STORE;

				// No depth attachment, only color
				rpc_info.attachmentCount = 1;
				rpc_info.pAttachments    = atch_descs + COLOR;
				rpc_info.dependencyCount = subpassDepsUi.size();
				rpc_info.pDependencies   = subpassDepsUi.data();
				rpc_info.subpassCount    = subpassDescsUi.size();
				rpc_info.pSubpasses      = subpassDescsUi.data();
			}

			VK_CHECK(vkCreateRenderPass, mDevice, &rpc_info, nullptr, &mUiRpass);
		}

		if(! state.reinit) { // Create the dset layouts, pipeline layouts, pipeline cache and pipelines
			VkDescriptorSetLayout layouts[] = { mGframeDsetLayout, m3dPipelineMaterialDsetLayout };
			VkPipelineLayoutCreateInfo plc_info = { };
			plc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plc_info.setLayoutCount = std::size(layouts);
			plc_info.pSetLayouts    = layouts;
			VK_CHECK(vkCreatePipelineLayout, mDevice, &plc_info, nullptr, &m3dPipelineLayout);

			VkPipelineCacheCreateInfo pcc_info = { };
			pcc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
			VK_CHECK(vkCreatePipelineCache, mDevice, &pcc_info, nullptr, &mPipelineCache);

			geom::PipelineSetCreateInfo gpsci = { };
			gpsci.subpass        = 0;
			gpsci.renderPass     = mUiRpass;
			gpsci.pipelineCache  = mPipelineCache;
			gpsci.polyDsetLayout = mGeometryPipelineDsetLayout;
			gpsci.textDsetLayout = mImagePipelineDsetLayout;
			auto& geomPipelines = mUiStorage->geomPipelines;
			geomPipelines = geom::PipelineSet::create(mDevice, gpsci);

			uint32_t worldSubpassIndex = 0;
			uint32_t uiSubpassIndex = 0;
			auto fwdSubpassIdx = [&](Renderer::RenderPass rp) { switch(rp) {
				default: throw EngineRuntimeError("Bad render pass target for renderer pipeline");
				case Renderer::RenderPass::eWorld: return worldSubpassIndex ++;
				case Renderer::RenderPass::eUi:    return uiSubpassIndex ++;
			}};
			for(auto& r : mRenderers) {
				using PipelineSet = decltype(mPipelines);
				auto& info = r->pipelineInfo();
				for(auto& shaderReq : info.shaderRequirements) {
					auto pl = shaderReq.pipelineLayout;
					switch(pl) {
						default: throw EngineRuntimeError(fmt::format("Renderer requires bad pipeline layout ({})", std::underlying_type_t<PipelineLayoutId>(pl)));
						case PipelineLayoutId::e3d:
							mPipelines.insert(PipelineSet::value_type(shaderReq, create3dPipeline(shaderReq, fwdSubpassIdx(info.rpass), mWorldRpass)));
							break;
						case PipelineLayoutId::eGeometry: [[fallthrough]];
						case PipelineLayoutId::eImage:
							break;
					}
				}
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

			{ // Create the ui rpass framebuffer
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


	void Engine::RpassInitializer::initTop(State&) { }


	void Engine::RpassInitializer::destroyTop(State&) {
		{ // Wait for fences
			std::vector<VkFence> fences;
			fences.reserve(3 * mGframes.size());
			for(size_t i = 0; i < mGframes.size(); ++i) {
				auto& gf = mGframes[i];
				fences.push_back(gf.fence_draw);
				fences.push_back(gf.fence_prepare);
				fences.push_back(mGframeSelectionFences[i]);
			}
			vkWaitForFences(mDevice, fences.size(), fences.data(), VK_TRUE, UINT64_MAX);
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
			geom::PipelineSet::destroy(mDevice, mUiStorage->geomPipelines);
			for(auto& p : mPipelines) vkDestroyPipeline(mDevice, p.second, nullptr);
			mPipelines.clear();
			vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
			vkDestroyPipelineLayout(mDevice, m3dPipelineLayout, nullptr);
		}
		vkDestroyRenderPass(mDevice, mUiRpass, nullptr);
		vkDestroyRenderPass(mDevice, mWorldRpass, nullptr);
	}


	void Engine::RpassInitializer::destroySwapchain(State& state) {
		if(mSwapchain == nullptr) return;

		for(auto& gf : mGframes) vkDestroyImageView(mDevice, gf.swapchain_image_view, nullptr);

		if(! state.reinit) {
			vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
			mSwapchain = nullptr;
		}
	}


	void Engine::RpassInitializer::destroyGframes(State& state) {
		state.destroyGframes = true;

		auto destroy_frame = [&](GframeData& gf, VkFence& gframe_sel_fence) {
			vkDestroyFence     (mDevice, gframe_sel_fence, nullptr);
			vkDestroyFence     (mDevice, gf.fence_draw, nullptr);
			vkDestroyFence     (mDevice, gf.fence_prepare, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_drawGui, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_drawWorld, nullptr);
			vkDestroySemaphore (mDevice, gf.sem_prepare, nullptr);

			vkutil::ManagedImage::destroy(mVma, gf.atch_color);
			vkutil::ManagedImage::destroy(mVma, gf.atch_depthstencil);

			vkDestroyCommandPool(mDevice, gf.cmd_pool, nullptr);
		};

		logger().trace("Destroying {} gframe{}", mGframes.size(), (mGframes.size() != 1)? "s" : "");
		for(size_t i = 0; i < mGframes.size(); ++i) {
			GframeData& gf  = mGframes[i];
			VkFence&    gff = mGframeSelectionFences[i];
			destroy_frame(gf, gff);
		}
	}


	void Engine::RpassInitializer::destroyRenderers(State& state) {
		if(! state.reinit) {
			WorldRenderer::destroy(*mWorldRenderer_TMP_UGLY_NAME);
			ObjectStorage::destroy(*mObjectStorage);
			vkDestroyDescriptorSetLayout(mDevice, m3dPipelineMaterialDsetLayout, nullptr);
			mRenderers.clear();
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
