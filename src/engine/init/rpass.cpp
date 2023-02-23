#include "init.hpp"

#include <vkutil/error.hpp>

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

		#define TRY_CA_(BIT_) \
			if(capabs.supportedCompositeAlpha & BIT_) { \
				spdlog::info("[+] composite alpha {} is supported", #BIT_); \
				return BIT_; \
			} else { \
				spdlog::info("[ ] composite alpha {} is not supported", #BIT_); \
			}

		TRY_CA_(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
		TRY_CA_(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
		TRY_CA_(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)

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

		#define TRY_PM_(BIT_, DIFF_) \
			if(BIT_ != DIFF_) [[likely]] { \
				if(avail_modes.end() != std::find(avail_modes.begin(), avail_modes.end(), BIT_)) { \
					spdlog::info("[+] present mode {} is supported", #BIT_); \
					return BIT_; \
				} else { \
					spdlog::info("[ ] present mode {} is not supported", #BIT_); \
				} \
			}

		TRY_PM_(preferred_mode, VK_PRESENT_MODE_MAX_ENUM_KHR)
		TRY_PM_(VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,     preferred_mode)
		TRY_PM_(VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR, preferred_mode)
		TRY_PM_(VK_PRESENT_MODE_MAILBOX_KHR,                   preferred_mode)
		TRY_PM_(VK_PRESENT_MODE_FIFO_RELAXED_KHR,              preferred_mode)

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
			MIRROR_(mQfams),
			MIRROR_(mGraphicsQueue),
			MIRROR_(mComputeQueue),
			MIRROR_(mTransferQueue),
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
	}


	void Engine::RpassInitializer::reinit() {
		VkSwapchainKHR retired_sc = nullptr;
		destroySwapchain(&retired_sc);
		initSwapchain(retired_sc);
	}


	void Engine::RpassInitializer::destroy() {
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
				throw vkutil::SdlRuntimeError(fmt::format(
					"Failed to create a window surface: {}", err ));
			}
		}

		{ // Determine the queue to use for present operations
			bool found = false;
			#define TRY_FAM_(FAM_, Q_) \
				if(! found) { \
					VkBool32 supported; \
					VK_CHECK( \
						vkGetPhysicalDeviceSurfaceSupportKHR, \
						mPhysDevice, \
						uint32_t(mQfams.FAM_##_index), \
						mSurface, \
						&supported ); \
					if(supported) { \
						mPresentQfamIndex = mQfams.FAM_##_index; \
						mPresentQueue = m ## Q_ ## Queue; \
						spdlog::debug("[+] Queue family {} can be used to present", \
							uint32_t(mQfams.FAM_##_index) ); \
					} else { \
						spdlog::debug("[ ] Queue family {} cannot be used to present", \
							uint32_t(mQfams.FAM_##_index) ); \
					} \
				}
			TRY_FAM_(graphics, Graphics)
			TRY_FAM_(transfer, Transfer)
			TRY_FAM_(compute,  Compute)
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
			uint32_t(mQfams.graphics_index),
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
