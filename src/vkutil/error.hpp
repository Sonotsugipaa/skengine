#pragma once

#include <skengine_fwd.hpp>

#include <stdexcept>
#include <type_traits>

#include <vulkan/vulkan.h>



namespace vkutil {

	class VulkanError : public std::runtime_error {
	public:
		VulkanError(const char* fn_name, VkResult r):
				std::runtime_error(std::string(fn_name) + " -> " + std::to_string(r))
		{ }

		VkResult vkResult() const noexcept { return mResult; }

	private:
		VkResult mResult;
	};


	class SdlRuntimeError : public std::runtime_error {
	public:
		template <typename... Args>
		SdlRuntimeError(Args... args): std::runtime_error::runtime_error(args...) { }
	};


	template <typename Fn, typename... Args>
	VkResult vk_check(const char* fn_name, Fn fn, Args... args) {
		VkResult r = fn(args...);
		if(r < 0) {
			throw VulkanError(fn_name, r);
		}
		return r;
	}


	#define VK_CHECK(FN_, ...) vkutil::vk_check(#FN_, FN_, __VA_ARGS__)

}
