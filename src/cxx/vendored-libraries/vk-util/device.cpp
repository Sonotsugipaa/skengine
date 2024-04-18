#include "init.hpp"

#include "error.hpp"

#include <spdlog/logger.h>

#include <cassert>

#include <vulkan/vk_enum_string_helper.h>



namespace vkutil {

	constexpr uint32_t invalidQueueIndex = ~ uint32_t(0);


	void findQueueFamilies(
		spdlog::logger* logger,
		QueueFamilies& dst,
		VkPhysicalDevice                  physDev,
		const VkPhysicalDeviceProperties& physDevProps
	) {
		assert(physDev != nullptr);

		std::vector<VkQueueFamilyProperties> qFamProps;
		{ // Query the queue family properties
			uint32_t propCount;
			vkGetPhysicalDeviceQueueFamilyProperties(physDev, &propCount, nullptr);
			qFamProps.resize(propCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physDev, &propCount, qFamProps.data());
		}

		const auto findIdx = [&](
				const std::string_view& qtype,
				VkQueueFlags flag,
				unsigned offset
		) -> uint32_t {
			constexpr const auto familyQueueStr = [](VkQueueFlags f) { switch(f) {
				case VK_QUEUE_GRAPHICS_BIT: return "graphics";
				case VK_QUEUE_COMPUTE_BIT:  return "compute";
				case VK_QUEUE_TRANSFER_BIT: return "transfer";
				default: abort();
			} };

			for(uint32_t i=0; i < qFamProps.size(); ++i) {
				uint32_t iOffset = (i+offset) % qFamProps.size();
				assert(iOffset != invalidQueueIndex);
				if(qFamProps[iOffset].queueFlags & VkQueueFlagBits(flag)) {
					if(logger != nullptr) logger->info("Using queue family {} for {} queues", iOffset, familyQueueStr(flag));
					return uint32_t(iOffset);
				}
			}
			throw std::runtime_error(fmt::format(
				"No suitable queue for {} operations on device {:04x}:{:04x}",
				qtype,
				physDevProps.vendorID,
				physDevProps.deviceID ));
		};

		{ // Set the destination members
			dst.graphicsIndex = findIdx("graphics", VK_QUEUE_GRAPHICS_BIT, 0);
			dst.computeIndex  = findIdx("compute",  VK_QUEUE_COMPUTE_BIT,  uint32_t(dst.graphicsIndex) + 1);
			dst.transferIndex = findIdx("transfer", VK_QUEUE_TRANSFER_BIT, uint32_t(dst.computeIndex) + 1);
			dst.graphicsProps = qFamProps[std::size_t(dst.graphicsIndex)];
			dst.computeProps  = qFamProps[std::size_t(dst.computeIndex)];
			dst.transferProps = qFamProps[std::size_t(dst.transferIndex)];
		}
	}


	void createDevice(
		spdlog::logger* logger,
		const CreateDeviceDst&  dst,
		const CreateDeviceInfo& info
	) {
		findQueueFamilies(logger, dst.queues.families, info.physDev, *info.pPhysDevProps);

		auto& qFams = dst.queues.families;
		#define MERGE_(SRC_, DST_) \
			assert(qFams.DST_##Index != invalidQueueIndex); \
			assert(qFams.SRC_##Index != invalidQueueIndex); \
			if(qFams.DST_##Index == qFams.SRC_##Index) { \
				++ DST_##FamQCount; \
				SRC_##QIndex    = DST_##QIndex + 1; \
				SRC_##FamQCount = 0; \
				SRC_##QCount    = DST_##QCount; \
				if(DST_##FamQCount > qFams.DST_##Props.queueCount) { \
					DST_##FamQCount = qFams.DST_##Props.queueCount; \
				} \
			}
		uint32_t graphicsFamQCount = 1;
		uint32_t computeFamQCount  = 1;
		uint32_t transferFamQCount = 1;
		uint32_t* graphicsQCount = &graphicsFamQCount;
		uint32_t* computeQCount  = &computeFamQCount;
		uint32_t* transferQCount = &transferFamQCount;
		uint32_t graphicsQIndex = 0;
		uint32_t computeQIndex  = 0;
		uint32_t transferQIndex = 0;
		MERGE_(transfer, compute)
		MERGE_(          compute, graphics)
		MERGE_(                   graphics, transfer)
		assert(*graphicsQCount > 0); graphicsQIndex = graphicsQIndex % *graphicsQCount;
		assert(*computeQCount  > 0); computeQIndex  = computeQIndex  % *computeQCount;
		assert(*transferQCount > 0); transferQIndex = transferQIndex % *transferQCount;
		#undef MERGE_

		std::vector<VkDeviceQueueCreateInfo> dqInfos;
		dqInfos.reserve(3);
		const auto ins = [&](uint32_t fam, const VkQueueFamilyProperties& famProps, uint32_t count) {
			static constexpr float priorities[] = { .0f, .0f, .0f };
			assert(count <= std::size(priorities));
			if(count > 0) {
				VkDeviceQueueCreateInfo ins = { };
				ins.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				ins.queueFamilyIndex = fam;
				ins.queueCount       = std::min(count, famProps.queueCount);
				ins.pQueuePriorities = priorities;
				dqInfos.push_back(ins);
				if(logger != nullptr) logger->info("Assigned {} queue{} to family {}", count, count == 1? "":"s", fam);
			}
		};
		ins(dst.queues.families.graphicsIndex, dst.queues.families.graphicsProps, graphicsFamQCount);
		ins(dst.queues.families.computeIndex,  dst.queues.families.computeProps,  computeFamQCount);
		ins(dst.queues.families.transferIndex, dst.queues.families.transferProps, transferFamQCount);

		VkPhysicalDeviceSynchronization2Features features2 = { };
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
		features2.synchronization2 = true;
		VkDeviceCreateInfo dInfo = { };
		dInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		dInfo.pNext = &features2;
		dInfo.pQueueCreateInfos       = dqInfos.data();
		dInfo.queueCreateInfoCount    = dqInfos.size();
		dInfo.pEnabledFeatures        = info.pRequiredFeatures;
		dInfo.enabledExtensionCount   = info.extensions.size();
		dInfo.ppEnabledExtensionNames = info.extensions.data();
		VK_CHECK(vkCreateDevice, info.physDev, &dInfo, nullptr, &dst.device);

		vkGetDeviceQueue(dst.device, dst.queues.families.graphicsIndex, graphicsQIndex, &dst.queues.graphics);
		vkGetDeviceQueue(dst.device, dst.queues.families.computeIndex,  computeQIndex,  &dst.queues.compute);
		vkGetDeviceQueue(dst.device, dst.queues.families.transferIndex, transferQIndex, &dst.queues.transfer);
	}


	VkSurfaceFormatKHR selectSwapchainFormat(
		spdlog::logger* logger,
		VkPhysicalDevice physDev,
		VkSurfaceKHR     surface
	) {
		std::vector<VkSurfaceFormatKHR> formats;
		{
			uint32_t formatCount;
			VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR, physDev, surface,
				&formatCount, nullptr );
			formats.resize(formatCount);
			VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR, physDev, surface,
				&formatCount, formats.data() );
		}
		assert(formats.size() > 0);

		static constexpr auto desiredFormat = VK_FORMAT_B8G8R8A8_UNORM;
		static constexpr auto desiredColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

		auto found = std::find_if(formats.begin(), formats.end(),
			[](const VkSurfaceFormatKHR& fmt) {
				return
					(fmt.format == desiredFormat) &&
					(fmt.colorSpace == desiredColorSpace);
			} );
		if(found == formats.end()) {
			if(logger != nullptr) logger->debug("Desired surface format not found: {0} with color space {1}",
				string_VkFormat(desiredFormat), string_VkColorSpaceKHR(desiredColorSpace) );
			found = formats.begin();
		}
		if(logger != nullptr) logger->debug("Using surface format {0} with color space {1}",
			string_VkFormat(found->format), string_VkColorSpaceKHR(found->colorSpace) );
		return *found;
	}


	VkFormat selectDepthStencilFormat(
		spdlog::logger* logger,
		VkPhysicalDevice physDev,
		VkImageTiling    tiling
	) {
		/* Defines the order in which depth/stencil image formats
			* are attempted to be used, from best to worst. */
		constexpr std::array<VkFormat, 6> fmtPreference = {
			// No guaranteed fallback
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D16_UNORM,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D16_UNORM_S8_UINT
		};

		constexpr VkFormatFeatureFlagBits requiredFeatures =
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VkFormat r = VK_FORMAT_UNDEFINED;
		VkFormatProperties props;
		const auto trySelect = [&](VkFormat fmt) {
			vkGetPhysicalDeviceFormatProperties(physDev, fmt, &props);
			if(tiling == VK_IMAGE_TILING_OPTIMAL)
			if((props.optimalTilingFeatures & requiredFeatures) == requiredFeatures) {
				r = fmt;  return true; }
			if(tiling == VK_IMAGE_TILING_LINEAR)
			if((props.linearTilingFeatures & requiredFeatures) == requiredFeatures) {
				r = fmt;  return true; }
			return false;
		};
		for(VkFormat fmt : fmtPreference) {
			if(trySelect(fmt)) break; }
		if(r == VK_FORMAT_UNDEFINED) {
			throw std::runtime_error("failed to find a suitable depth/stencil image format"); }
		if(logger != nullptr) logger->debug("Using depth/stencil image format {0}", string_VkFormat(r));
		return r;
	}

}
