#include "memory.hpp"

#include "error.hpp"

#include <spdlog/spdlog.h>



namespace vkutil {

	void log_alloc_memory_type(
			VmaAllocator                   vma,
			const VmaAllocationCreateInfo& ac_info,
			const VmaAllocationInfo&       a_info
	) {
		VkMemoryPropertyFlags mem_props;
		vmaGetMemoryTypeProperties(vma, a_info.memoryType, &mem_props);
		std::string req_flags = fmt::format("{:04b}", unsigned(ac_info.requiredFlags)  & 0x1111);
		std::string prf_flags = fmt::format("{:04b}", unsigned(ac_info.preferredFlags) & 0x1111);
		std::string flags     = fmt::format("{:04b}", unsigned(ac_info.flags));
		spdlog::trace("Allocation: req {:04b}, pref {:04b}, flags {}; got {:04b}",
			unsigned(ac_info.requiredFlags)  & 0x1111,
			unsigned(ac_info.preferredFlags) & 0x1111,
			unsigned(ac_info.flags),
			mem_props );
	}


	template<>
	void* Buffer::map<void>(VmaAllocator allocator) {
		void* r;
		VK_CHECK(vmaMapMemory, allocator, alloc, &r);
		return r;
	}

}
