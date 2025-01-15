#include "init.hpp"

#include <vk-util/error.hpp>

#include <posixfio_tl.hpp>

#include <memory>

#include <vulkan/vk_enum_string_helper.h>

#include <ui-structure/ui.hpp>

#include <timer.tpp>



namespace SKENGINE_NAME_NS {

	using stages_t = uint_fast8_t;


	struct Engine::RpassInitializer::State {
		util::SteadyTimer<> timer;
		ConcurrentAccess& concurrentAccess;
		stages_t stages;
		bool reinit         : 1;
		bool createGframes  : 1;
		bool destroyGframes : 1;
		State(ConcurrentAccess& ca, bool reinit, stages_t stages = ~ stages_t(0)): concurrentAccess(ca), stages(stages), reinit(reinit) { }
	};


	void select_swapchain_extent(
			Logger&           logger,
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


	VkCompositeAlphaFlagBitsKHR select_composite_alpha(Logger& logger, const VkSurfaceCapabilitiesKHR& capabs, bool do_log) {
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
			Logger&          logger,
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


	#warning "Document how this works, since it's trippy, workaroundy and *definitely* UB (but it removes A LOT of boilerplate)"
	void Engine::RpassInitializer::init(ConcurrentAccess& ca, const RpassConfig& rc) {
		#define SET_STAGE_(B_) state.stages = state.stages | (stages_t(1) << stages_t(B_));
		mRpassConfig = rc;
		mSwapchainOod = false;
		mHdrEnabled   = false;
		auto state = State(ca, false, 0);
		try {
			initSurface(); SET_STAGE_(0)
			initSwapchain(state); SET_STAGE_(1)
			initGframes(state); SET_STAGE_(2)
			initRpasses(state); SET_STAGE_(3)
			initTop(state); SET_STAGE_(4)
			mLogger.debug("Render process initialization took {}ms", float(state.timer.count<std::micro>()) / 1000.0f);
		} catch(...) {
			unwind(state);
			std::rethrow_exception(std::current_exception());
		}
		#undef SET_STAGE_
	}


	void Engine::RpassInitializer::reinit(ConcurrentAccess& ca) {
		#define SET_STAGE_(B_) state.stages = state.stages | (stages_t(1) << stages_t(B_));
		#define UNSET_STAGE_(B_) state.stages = state.stages & (~ (stages_t(1) << stages_t(B_)));
		mLogger.trace("Recreating swapchain");

		auto state = State(ca, true);

		VkExtent2D old_render_xt  = mRenderExtent;
		VkExtent2D old_present_xt = mPresentExtent;

		try {
			destroyTop(state); UNSET_STAGE_(4)
			destroySwapchain(state); UNSET_STAGE_(1)
			destroyGframes(state); UNSET_STAGE_(2)
			initSwapchain(state); SET_STAGE_(1)
			initGframes(state); SET_STAGE_(2)

			bool render_xt_changed =
				(old_render_xt.width  != mRenderExtent.width) ||
				(old_render_xt.height != mRenderExtent.height);
			bool present_xt_changed =
				(old_present_xt.width  != mPresentExtent.width) ||
				(old_present_xt.height != mPresentExtent.height);

			if(render_xt_changed || present_xt_changed) {
				destroyRpasses(state); UNSET_STAGE_(3)
				initRpasses(state); SET_STAGE_(3)
			}

			initTop(state); SET_STAGE_(4)
			mLogger.debug("Render process reinitialization took {}ms", float(state.timer.count<std::micro>()) / 1000.0f);
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
		IF_STAGE_(4) destroyTop(state);
		IF_STAGE_(3) destroyRpasses(state);
		IF_STAGE_(1) destroySwapchain(state);
		IF_STAGE_(2) destroyGframes(state);
		IF_STAGE_(0) destroySurface();
		mLogger.debug("Render process destruction took {}ms", float(state.timer.count<std::micro>()) / 1000.0f);
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
						mLogger.debug("[+] Queue family {} can be used to present", \
							uint32_t(mQueues.families.FAM_##Index) ); \
						found = true; \
					} else { \
						mLogger.debug("[ ] Queue family {} cannot be used to present", \
							uint32_t(mQueues.families.FAM_##Index) ); \
					} \
				}
			TRY_FAM_(graphics)
			TRY_FAM_(transfer)
			TRY_FAM_(compute)
			#undef TRY_FAM_
			mLogger.debug("Using queue family {} for the present queue", uint32_t(mPresentQfamIndex));
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

		mSurfaceFormat = vkutil::selectSwapchainFormat(nullptr, mPhysDevice, mSurface);
		select_swapchain_extent(mLogger, &mPresentExtent, mPrefs.init_present_extent, mSurfaceCapabs, ! state.reinit);
		mRenderExtent = select_render_extent(mPresentExtent, mPrefs.max_render_extent, mPrefs.upscale_factor);
		if(! state.reinit) mLogger.debug("Chosen render extent {}x{}", mRenderExtent.width, mRenderExtent.height);

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
			s_info.compositeAlpha   = (! mPrefs.composite_alpha)? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : select_composite_alpha(mLogger, mSurfaceCapabs, ! state.reinit);
			s_info.presentMode      = select_present_mode(mLogger, mPhysDevice, mSurface, mPrefs.present_mode, ! state.reinit);
			s_info.clipped          = VK_TRUE;
			s_info.oldSwapchain     = mSwapchain;
			s_info.pQueueFamilyIndices   = concurrent_qfams;
			if(concurrent_qfams[0] == concurrent_qfams[1]) {
				s_info.queueFamilyIndexCount = 0;
				s_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
				mLogger.trace("Swapchain uses one queue family => exclusive sharing mode");
			} else {
				s_info.queueFamilyIndexCount = 2;
				s_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
				mLogger.trace("Swapchain uses queue families {} and {} => concurrent sharing mode", concurrent_qfams[0], concurrent_qfams[1]);
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
			mLogger.trace("Requesting {} swapchain image{}", s_info.minImageCount, s_info.minImageCount == 1? "":"s");
		}


		// Bandaid fix for vkCreateSwapchainKHR randomly throwing VK_ERROR_UNKNOWN around
		auto tryCreateSwapchain = [&](VkSwapchainKHR* dst) {
			for(auto i = 0u; i < 3u; ++i) {
				try {
					VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, dst);
					mLogger.trace("vkCreateSwapchainKHR -> VK_SUCCESS (attempt n. {})", i+1);
					return;
				} catch(vkutil::VulkanError& err) {
					mLogger.error("{} ({}) (attempt n. {})", err.what(), int32_t(err.vkResult()), i+1);
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				s_info.oldSwapchain = nullptr;
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
			mLogger.trace("Acquired {} swapchain image{}", count, count == 1? "":"s");

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


	void Engine::RpassInitializer::initGframes(State&) {
		assert(! mGframes.empty());
		auto frame_n = mGframes.size();

		mGframeLast = -1;

		VkFenceCreateInfo fc_info = { };
		fc_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fc_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		auto create_frame = [&](VkFence& gff) {
			VK_CHECK(vkCreateFence, mDevice, &fc_info, nullptr, &gff);
		};

		mLogger.trace("Creating {} gframe{}", frame_n, (frame_n != 1)? "s" : "");
		mGframeSelectionFences.resize(frame_n);
		for(size_t i = 0; i < frame_n; ++i) {
			VkFence& gff = mGframeSelectionFences[i];
			create_frame(gff);
		}
	}


	void Engine::RpassInitializer::initRpasses(State& state) {
		if(! state.reinit) { // Create the dset layouts, pipeline layouts, pipeline cache and pipelines
			VkPipelineCacheCreateInfo pcc_info = { };
			pcc_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
			VK_CHECK(vkCreatePipelineCache, mDevice, &pcc_info, nullptr, &mPipelineCache);
		}
	}


	void Engine::RpassInitializer::initTop(State&) { }


	void Engine::RpassInitializer::destroyTop(State&) {
		{ // Wait for fences
			const auto& fences = mGframeSelectionFences;
			vkWaitForFences(mDevice, fences.size(), fences.data(), VK_TRUE, UINT64_MAX);
		}
	}


	void Engine::RpassInitializer::destroyRpasses(State& state) {
		if(! state.reinit) {
			vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
		}
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

		auto destroy_frame = [&](VkFence& gframe_sel_fence) {
			vkDestroyFence(mDevice, gframe_sel_fence, nullptr);
		};

		mLogger.trace("Destroying {} gframe{}", mGframes.size(), (mGframes.size() != 1)? "s" : "");
		for(size_t i = 0; i < mGframes.size(); ++i) {
			VkFence& gff = mGframeSelectionFences[i];
			destroy_frame(gff);
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
