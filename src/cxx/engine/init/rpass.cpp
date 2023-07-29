#include "init.hpp"

#include <vk-util/error.hpp>

#include <posixfio_tl.hpp>

#include <spdlog/spdlog.h>

#include <memory>



namespace SKENGINE_NAME_NS {

	void select_swapchain_fmt(
			VkSurfaceFormatKHR* dst,
			VkPhysicalDevice    phys_dev,
			VkSurfaceKHR        surface
	) {
		std::vector<VkSurfaceFormatKHR> formats;
		{
			uint32_t formatCount;
			VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR,
				phys_dev, surface,
				&formatCount, nullptr );
			formats.resize(formatCount);
			VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR,
				phys_dev, surface,
				&formatCount, formats.data() );
		}
		if(formats.size() < 1) {
			throw EngineRuntimeError("No Vulkan surface format found?");
		}

		#define SET_(WHAT_, WHAT_STR_, VAL_) constexpr auto WHAT_ = VAL_; constexpr const char* WHAT_STR_ = #VAL_;
		SET_(desired_fmt,       desired_fmt_str,       VK_FORMAT_B8G8R8A8_SRGB)
		SET_(desired_col_space, desired_col_space_str, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		#undef SET_

		auto found = std::find_if(formats.begin(), formats.end(),
			[](const VkSurfaceFormatKHR& fmt) {
				return
					(fmt.format == desired_fmt) &&
					(fmt.colorSpace == desired_col_space);
			} );
		if(found == formats.end()) {
			spdlog::info("No Vulkan surface format supports {} with color space {}",
				desired_fmt_str, desired_col_space_str );
			found = formats.begin();
		}
		spdlog::info("Using surface format {}",      desired_fmt_str);
		spdlog::info("Using surface color space {}", desired_col_space_str);
		*dst = *found;
	}


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
					spdlog::info("[+] " NM_ " is supported"); \
					return BIT_; \
				} else { \
					spdlog::info("[ ] " NM_ " is not supported"); \
				} \
			}

		TRY_PM_("preferred present mode",                 preferred_mode, VK_PRESENT_MODE_MAX_ENUM_KHR)
		TRY_PM_("present mode SHARED_DEMAND_REFRESH",     VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,     preferred_mode)
		TRY_PM_("present mode SHARED_CONTINUOUS_REFRESH", VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR, preferred_mode)
		TRY_PM_("present mode MAILBOX",                   VK_PRESENT_MODE_MAILBOX_KHR,                   preferred_mode)
		TRY_PM_("present mode FIFO_RELAXED",              VK_PRESENT_MODE_FIFO_RELAXED_KHR,              preferred_mode)

		#undef TRY_PM_

		spdlog::info("[x] Using fallback present mode VK_PRESENT_MODE_FIFO_KHR");
		return VK_PRESENT_MODE_FIFO_KHR;
	}


	Engine::RpassInitializer::RpassInitializer(Engine& e):
			#define MIRROR_(MEMBER_) MEMBER_(e.MEMBER_)
			MIRROR_(mSdlWindow),
			MIRROR_(mVkInstance),
			MIRROR_(mPhysDevice),
			MIRROR_(mDevice),
			MIRROR_(mVma),
			MIRROR_(mQueues),
			MIRROR_(mSurface),
			MIRROR_(mPresentQfamIndex),
			MIRROR_(mPresentQueue),
			MIRROR_(mSwapchain),
			MIRROR_(mSurfaceCapabs),
			MIRROR_(mSurfaceFormat),
			MIRROR_(mSwapchainImages),
			MIRROR_(mGframes),
			MIRROR_(mPrefs),
			MIRROR_(mRpassConfig)
			#undef MIRROR_
	{
		e.mSwapchainOod = false;
	}


	void Engine::RpassInitializer::init(const RpassConfig& rc) {
		mRpassConfig = rc;
		initSurface();
		initSwapchain(nullptr);
		initGframes();
	}


	void Engine::RpassInitializer::reinit() {
		VkSwapchainKHR retired_sc = nullptr;
		destroySwapchain(&retired_sc);
		initSwapchain(retired_sc);
	}


	void Engine::RpassInitializer::destroy() {
		destroyGframes(0);
		destroySwapchain(nullptr);
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


	void Engine::RpassInitializer::initSwapchain(VkSwapchainKHR old_swapchain) {
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

		select_swapchain_fmt(&mSurfaceFormat, mPhysDevice, mSurface);
		select_swapchain_extent(&mPrefs.present_extent, mPrefs.present_extent, mSurfaceCapabs);

		uint32_t concurrent_qfams[] = {
			mQueues.families.graphicsIndex,
			uint32_t(mPresentQfamIndex) };

		VkSwapchainCreateInfoKHR s_info = { };
		s_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		s_info.surface          = mSurface;
		s_info.imageFormat      = mSurfaceFormat.format;
		s_info.imageColorSpace  = mSurfaceFormat.colorSpace;
		s_info.imageExtent      = mPrefs.present_extent;
		s_info.imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		s_info.imageArrayLayers = 1;
		s_info.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		s_info.compositeAlpha   = select_composite_alpha(mSurfaceCapabs);
		s_info.presentMode      = select_present_mode(mPhysDevice, mSurface, mPrefs.present_mode);
		s_info.clipped          = VK_TRUE;
		s_info.oldSwapchain     = old_swapchain == nullptr? VK_NULL_HANDLE : old_swapchain;
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

		if(s_info.oldSwapchain == VK_NULL_HANDLE) [[unlikely]] {
			VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, &mSwapchain);
		} else {
			// The old swapchain is retired, but still exists:
			// not destroying it when an exception is thrown may
			// cause a memory leak.
			try {
				VK_CHECK(vkCreateSwapchainKHR, mDevice, &s_info, nullptr, &mSwapchain);
			} catch(vkutil::VulkanError& e) {
				vkDestroySwapchainKHR(mDevice, old_swapchain, nullptr);
				throw e;
			}
			vkDestroySwapchainKHR(mDevice, old_swapchain, nullptr);
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



	void Engine::RpassInitializer::initGframes() {
		vkutil::BufferCreateInfo ubo_bc_info = {
			.size  = sizeof(dev::FrameUniform),
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.qfam_sharing = { } };

		size_t missing = mPrefs.max_concurrent_frames - mGframes.size();
		spdlog::trace("Creating {}-{} gframes", missing, mGframes.size());
		for(size_t i = mGframes.size(); i < missing; ++i) {
			mGframes.push_back({ });
			GframeData& gf = mGframes.back();
			gf.frame_dset    = vkutil::DsetToken::eInvalid /* TODO */;
			gf.frame_ubo     = vkutil::ManagedBuffer::createUniformBuffer(mVma, ubo_bc_info);
			gf.frame_ubo_ptr = gf.frame_ubo.map<dev::FrameUniform>(mVma);
			gf.transfer_cmd_pool = vkutil::CommandPool(mDevice, mQueues.families.transferIndex, false);
			gf.render_cmd_pool   = vkutil::CommandPool(mDevice, mQueues.families.graphicsIndex, false);
		}
	}


	void Engine::RpassInitializer::destroyGframes(size_t keep) {
		spdlog::trace("Destroying {}-{} gframes", mGframes.size(), keep);
		for(size_t i = keep; i < mGframes.size(); ++i) {
			GframeData& gf = mGframes[i];
			gf.render_cmd_pool   = { };
			gf.transfer_cmd_pool = { };
			gf.frame_ubo.unmap(mVma);
			vkutil::Buffer::destroy(mVma, gf.frame_ubo);
			// TODO destroy dset
		}

		mGframes.resize(std::min(keep, mGframes.size()));
	}


	void Engine::RpassInitializer::destroySwapchain(VkSwapchainKHR* dst_old_swapchain) {
		if(mSwapchain == nullptr) return;

		mSwapchainImages.clear();

		if(dst_old_swapchain == nullptr) {
			vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
		} else {
			*dst_old_swapchain = mSwapchain;
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
