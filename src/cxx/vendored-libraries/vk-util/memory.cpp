#include "memory.hpp"

#include "error.hpp"

#include <type_traits>

#include <spdlog/logger.h>



namespace vkutil {

	Buffer Buffer::create(
			VmaAllocator allocator,
			const BufferCreateInfo&     bc_info,
			const AllocationCreateInfo& ac_info
	) {
		return ManagedBuffer::create(allocator, bc_info, ac_info);
	}


	void Buffer::destroy(VmaAllocator allocator, Buffer& buffer) noexcept {
		vmaDestroyBuffer(allocator, buffer.value, buffer.alloc);
	}


	Image Image::create(
			VmaAllocator allocator,
			const ImageCreateInfo&      ic_info,
			const AllocationCreateInfo& ac_info
	) {
		Image r;

		VkImageCreateInfo vk_ic_info = { };
		vk_ic_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		vk_ic_info.flags = ic_info.flags;
		vk_ic_info.usage = ic_info.usage;
		vk_ic_info.extent = ic_info.extent;
		vk_ic_info.format = ic_info.format;
		vk_ic_info.imageType = ic_info.type;
		vk_ic_info.initialLayout = ic_info.initialLayout;
		vk_ic_info.samples = ic_info.samples;
		vk_ic_info.tiling = ic_info.tiling;
		vk_ic_info.arrayLayers = ic_info.arrayLayers;
		vk_ic_info.mipLevels = ic_info.mipLevels;
		if(ic_info.qfamSharing.size() > 1) {
			vk_ic_info.queueFamilyIndexCount = ic_info.qfamSharing.size();
			vk_ic_info.pQueueFamilyIndices   = ic_info.qfamSharing.data();
			vk_ic_info.sharingMode           = VK_SHARING_MODE_CONCURRENT;
		} else {
			vk_ic_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		VmaAllocationCreateInfo vma_ac_info = { };
		vma_ac_info.flags          = ac_info.vmaFlags;
		vma_ac_info.usage          = VmaMemoryUsage(ac_info.vmaUsage);
		vma_ac_info.requiredFlags  = ac_info.requiredMemFlags;
		vma_ac_info.preferredFlags = ac_info.preferredMemFlags;

		VK_CHECK(vmaCreateImage, allocator, &vk_ic_info, &vma_ac_info, &r.value, &r.alloc, nullptr);

		return r;
	}


	void Image::destroy(VmaAllocator allocator, Image& image) noexcept {
		vmaDestroyImage(allocator, image.value, image.alloc);
	}


	template<>
	void* Buffer::map<void>(VmaAllocator allocator) {
		void* r;
		VK_CHECK(vmaMapMemory, allocator, alloc, &r);
		return r;
	}


	void Buffer::invalidate(VmaAllocator allocator, std::span<const MemoryRange> ranges) {
		assert(value != VK_NULL_HANDLE);
		assert(alloc != nullptr);
		for(auto& range : ranges) {
			VK_CHECK(vmaInvalidateAllocation, allocator, alloc, range.offset, range.size);
		}
	}


	void Buffer::flush(VmaAllocator allocator, std::span<const MemoryRange> ranges) {
		assert(value != VK_NULL_HANDLE);
		assert(alloc != nullptr);
		for(auto& range : ranges) {
			VK_CHECK(vmaFlushAllocation, allocator, alloc, range.offset, range.size);
		}
	}


	void Buffer::unmap(VmaAllocator allocator) {
		vmaUnmapMemory(allocator, alloc);
	}


	ManagedBuffer ManagedBuffer::create(
			VmaAllocator                allocator,
			const BufferCreateInfo&     bc_info,
			const AllocationCreateInfo& ac_info
	) {
		ManagedBuffer     r;
		VmaAllocationInfo a_info;

		VkBufferCreateInfo vk_bc_info = { };
		vk_bc_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		vk_bc_info.usage = bc_info.usage;
		vk_bc_info.size  = bc_info.size;
		if(bc_info.qfamSharing.size() > 1) {
			vk_bc_info.queueFamilyIndexCount = bc_info.qfamSharing.size();
			vk_bc_info.pQueueFamilyIndices   = bc_info.qfamSharing.data();
			vk_bc_info.sharingMode           = VK_SHARING_MODE_CONCURRENT;
		} else {
			vk_bc_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		VmaAllocationCreateInfo vma_ac_info = { };
		vma_ac_info.flags          = ac_info.vmaFlags;
		vma_ac_info.usage          = VmaMemoryUsage(ac_info.vmaUsage);
		vma_ac_info.requiredFlags  = ac_info.requiredMemFlags;
		vma_ac_info.preferredFlags = ac_info.preferredMemFlags;

		VK_CHECK(vmaCreateBuffer, allocator, &vk_bc_info, &vma_ac_info, &r.value, &r.alloc, &a_info);
		r.mInfo.memoryTypeIndex = a_info.memoryType;
		r.mInfo.size = vk_bc_info.size;
		vmaGetMemoryTypeProperties(allocator, r.mInfo.memoryTypeIndex, &r.mInfo.memoryProperties);

		return r;
	}


	void ManagedBuffer::destroy(VmaAllocator allocator, ManagedBuffer& buffer) noexcept {
		vmaDestroyBuffer(allocator, buffer.value, buffer.alloc);
	}


	ManagedBuffer ManagedBuffer::createStagingBuffer(
			VmaAllocator            allocator,
			const BufferCreateInfo& bc_info
	) {
		AllocationCreateInfo ac_info = {
			.requiredMemFlags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			.preferredMemFlags = { },
			.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
			.vmaUsage = VmaAutoMemoryUsage::eAutoPreferHost };

		return create(allocator, bc_info, ac_info);
	}


	ManagedBuffer ManagedBuffer::createUniformBuffer(
			VmaAllocator            allocator,
			const BufferCreateInfo& bc_info
	) {
		constexpr AllocationCreateInfo ac_info = {
			.requiredMemFlags  = 0,
			.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
			.vmaUsage = VmaAutoMemoryUsage::eAutoPreferDevice };

		return create(allocator, bc_info, ac_info);
	}


	ManagedBuffer ManagedBuffer::createStorageBuffer(
			VmaAllocator            allocator,
			const BufferCreateInfo& bc_info
	) {
		constexpr AllocationCreateInfo ac_info = {
			.requiredMemFlags  = 0,
			.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			.vmaFlags = 0,
			.vmaUsage = VmaAutoMemoryUsage::eAutoPreferDevice };

		return create(allocator, bc_info, ac_info);
	}


	ManagedImage ManagedImage::create(
			VmaAllocator                allocator,
			const ImageCreateInfo&      ic_info,
			const AllocationCreateInfo& ac_info
	) {
		ManagedImage      r;
		VmaAllocationInfo a_info;

		VkImageCreateInfo vk_ic_info = { };
		vk_ic_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		vk_ic_info.flags   = ic_info.flags;
		vk_ic_info.usage   = ic_info.usage;
		vk_ic_info.extent  = ic_info.extent;
		vk_ic_info.format  = ic_info.format;
		vk_ic_info.samples = ic_info.samples;
		vk_ic_info.tiling  = ic_info.tiling;
		vk_ic_info.mipLevels = ic_info.mipLevels;
		vk_ic_info.imageType = ic_info.type;
		vk_ic_info.arrayLayers = ic_info.arrayLayers;
		vk_ic_info.initialLayout = ic_info.initialLayout;
		if(ic_info.qfamSharing.size() > 1) {
			vk_ic_info.queueFamilyIndexCount = ic_info.qfamSharing.size();
			vk_ic_info.pQueueFamilyIndices   = ic_info.qfamSharing.data();
			vk_ic_info.sharingMode           = VK_SHARING_MODE_CONCURRENT;
		} else {
			vk_ic_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		VmaAllocationCreateInfo vma_ac_info = { };
		vma_ac_info.flags          = ac_info.vmaFlags;
		vma_ac_info.usage          = VmaMemoryUsage(ac_info.vmaUsage);
		vma_ac_info.requiredFlags  = ac_info.requiredMemFlags;
		vma_ac_info.preferredFlags = ac_info.preferredMemFlags;

		VK_CHECK(vmaCreateImage, allocator, &vk_ic_info, &vma_ac_info, &r.value, &r.alloc, &a_info);
		r.mInfo.memoryTypeIndex = a_info.memoryType;
		r.mInfo.extent = vk_ic_info.extent;
		r.mInfo.format = vk_ic_info.format;
		r.mInfo.mipLevelCount = vk_ic_info.mipLevels;
		vmaGetMemoryTypeProperties(allocator, r.mInfo.memoryTypeIndex, &r.mInfo.memoryProperties);

		return r;
	}


	void ManagedImage::destroy(VmaAllocator allocator, ManagedImage& image) noexcept {
		vmaDestroyImage(allocator, image.value, image.alloc);
	}

}
