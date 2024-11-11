#pragma once

#include <vulkan/vulkan.h>

#include <stdexcept>



namespace vkutil {

	class VulkanError : public std::runtime_error {
	public:
		VulkanError(const char* functionName, VkResult);

		VkResult vkResult() const noexcept { return ve_result; }

	private:
		VkResult ve_result;
	};


	template <typename Fn, typename... Args>
	VkResult vkCheck(const char* fnName, Fn fn, Args&&... args) {
		VkResult r = fn(std::forward<Args>(args)...);
		if(r < 0) {
			throw VulkanError(fnName, r);
		}
		return r;
	}


	#define VK_CHECK(FN_, ...) vkutil::vkCheck(#FN_, FN_, __VA_ARGS__)

}
