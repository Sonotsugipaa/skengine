#pragma once

#include <vulkan/vulkan.h>

#include <vma/vk_mem_alloc.h>

#include <spdlog/logger.h>

#include <span>
#include <vector>
#include <string>
#include <string_view>



namespace vkutil {

	constexpr VkPhysicalDeviceFeatures commonFeatures = []() {
		VkPhysicalDeviceFeatures r = { };
		r.samplerAnisotropy = VK_TRUE;
		r.sampleRateShading = VK_TRUE;
		r.multiDrawIndirect = VK_TRUE;
		r.geometryShader    = VK_TRUE;
		return r;
	} ();


	float rankPhysDevice(float orderBias, const VkPhysicalDeviceProperties&);


	struct SelectBestPhysDeviceDst {
		VkPhysicalDevice&           selectedDevice;
		VkPhysicalDeviceProperties& selectedDeviceProps;
		unsigned&                   index;
	};

	void selectBestPhysDevice(
		spdlog::logger*,
		const SelectBestPhysDeviceDst&,
		const std::span<VkPhysicalDevice>& devices,
		const VkPhysicalDeviceFeatures&    requiredFeatures,
		std::string* preferredDevUuidOpt );


	bool checkDevMissingFeatures(
		const VkPhysicalDeviceFeatures& availableFeatures,
		const VkPhysicalDeviceFeatures& requiredFeatures );


	std::vector<std::string_view> listDevMissingFeatures(
		const VkPhysicalDeviceFeatures& availableFeatures,
		const VkPhysicalDeviceFeatures& requiredFeatures );


	VkSurfaceFormatKHR selectSwapchainFormat    (spdlog::logger*, VkPhysicalDevice, VkSurfaceKHR);
	VkFormat           selectDepthStencilFormat (spdlog::logger*, VkPhysicalDevice, VkImageTiling);


	struct QueueFamilies {
		uint32_t graphicsIndex;
		uint32_t computeIndex;
		uint32_t transferIndex;
		VkQueueFamilyProperties graphicsProps;
		VkQueueFamilyProperties computeProps;
		VkQueueFamilyProperties transferProps;
	};

	void findQueueFamilies(
		spdlog::logger*,
		QueueFamilies& dst,
		VkPhysicalDevice,
		const VkPhysicalDeviceProperties& );


	struct Queues {
		QueueFamilies families;
		VkQueue graphics;
		VkQueue compute;
		VkQueue transfer;
	};


	struct CreateDeviceInfo {
		VkPhysicalDevice                  physDev;
		std::vector<const char*>          extensions;
		const VkPhysicalDeviceProperties* pPhysDevProps;
		const VkPhysicalDeviceFeatures*   pRequiredFeatures;
	};

	struct CreateDeviceDst {
		VkDevice&     device;
		Queues&       queues;
	};

	void createDevice(spdlog::logger*, const CreateDeviceDst& dst, const CreateDeviceInfo&);

}
