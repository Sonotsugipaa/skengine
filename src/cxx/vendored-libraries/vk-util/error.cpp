#include "error.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include <string>



namespace vkutil {

	VulkanError::VulkanError(const char* fnName, VkResult r):
			std::runtime_error(std::string(fnName) + " -> " + string_VkResult(r)),
			ve_result(r)
	{ }

}
