#include "memory.hpp"

#include "error.hpp"

#include <exception>
#include <cassert>



namespace vkutil {

	BufferDuplex BufferDuplex::create(
			VmaAllocator                allocator,
			const BufferCreateInfo&     bc_info,
			const AllocationCreateInfo& ac_info,
			HostAccess                  host_access
	) {
		BufferDuplex r;

		if(bc_info.size == 0) {
			r.mBufferHostVisibility = 0;
			r.mInfo          = { };
			r.mMappedPtr     = nullptr;
			r.mStagingBuffer = { };
			return r;
		}

		BufferCreateInfo local_bc_info = bc_info;
		local_bc_info.usage = VkBufferUsageFlagBits(
			bc_info.usage |
			(bool(uint32_t(host_access) & uint32_t(HostAccess::eRd)) * VK_BUFFER_USAGE_TRANSFER_SRC_BIT) |
			(bool(uint32_t(host_access) & uint32_t(HostAccess::eWr)) * VK_BUFFER_USAGE_TRANSFER_DST_BIT) );
		ManagedBuffer local_buffer = ManagedBuffer::create(allocator, local_bc_info, ac_info);
		r.value = local_buffer;
		r.alloc = local_buffer;
		r.mInfo = local_buffer.info();

		if(local_buffer.info().memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
			// Use the local buffer itself as the staging buffer
			r.mStagingBuffer = std::move(local_buffer);
			r.mBufferHostVisibility = (local_buffer.info().memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)? 2 : 1;
		} else {
			// Create a staging buffer
			try {
				VkBufferUsageFlagBits usage = VkBufferUsageFlagBits(
					(bool(uint32_t(host_access) & uint32_t(HostAccess::eRd)) * VK_BUFFER_USAGE_TRANSFER_DST_BIT) |
					(bool(uint32_t(host_access) & uint32_t(HostAccess::eWr)) * VK_BUFFER_USAGE_TRANSFER_SRC_BIT) );
				BufferCreateInfo staging_bc_info  = {
					.size  = local_bc_info.size,
					.usage = usage,
					.qfamSharing = { } };
				r.mStagingBuffer = ManagedBuffer::createStagingBuffer(allocator, staging_bc_info);
			} catch(...) {
				ManagedBuffer::destroy(allocator, local_buffer);
				std::rethrow_exception(std::current_exception());
			}
			r.mBufferHostVisibility = 0;
		}

		r.mMappedPtr = r.mStagingBuffer.map<void>(allocator);

		return r;
	}


	void BufferDuplex::destroy(VmaAllocator allocator, BufferDuplex& buffer) noexcept {
		buffer.mStagingBuffer.unmap(allocator);
		if(buffer.mBufferHostVisibility < 1) ManagedBuffer::destroy(allocator, buffer.mStagingBuffer);
		vmaDestroyBuffer(allocator, buffer.value, buffer.alloc);
	}


	BufferDuplex BufferDuplex::createVertexInputBuffer(
			VmaAllocator            allocator,
			const BufferCreateInfo& bc_info,
			HostAccess ha
	) {
		constexpr AllocationCreateInfo ac_info = {
			.requiredMemFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			.preferredMemFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
			.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
			.vmaUsage = VmaAutoMemoryUsage::eAutoPreferDevice };

		return create(allocator, bc_info, ac_info, ha);
	}


	BufferDuplex BufferDuplex::createIndexInputBuffer(
			VmaAllocator            allocator,
			const BufferCreateInfo& bc_info,
			HostAccess ha
	) {
		return createVertexInputBuffer(allocator, bc_info, ha);
	}


	BufferDuplex BufferDuplex::createUniformBuffer(
			VmaAllocator            allocator,
			const BufferCreateInfo& bc_info,
			HostAccess ha
	) {
		constexpr AllocationCreateInfo ac_info = {
			.requiredMemFlags  = 0,
			.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
			.vmaUsage = VmaAutoMemoryUsage::eAutoPreferDevice };

		return create(allocator, bc_info, ac_info, ha);
	}


	BufferDuplex BufferDuplex::createStorageBuffer(
			VmaAllocator            allocator,
			const BufferCreateInfo& bc_info,
			HostAccess ha
	) {
		constexpr AllocationCreateInfo ac_info = {
			.requiredMemFlags  = 0,
			.preferredMemFlags = 0,
			.vmaFlags = 0,
			.vmaUsage = VmaAutoMemoryUsage::eAutoPreferDevice };

		return create(allocator, bc_info, ac_info, ha);
	}


	void BufferDuplex::invalidate(VkCommandBuffer cmd, VmaAllocator allocator, std::span<const VkBufferCopy> ranges) {
		if(mBufferHostVisibility < 1) {
			// The buffer needs to be transfered back to the host

			assert(cmd != nullptr);
			vkCmdCopyBuffer(cmd, value, mStagingBuffer, ranges.size(), ranges.data());
		}

		for(auto& range : ranges) {
			VK_CHECK(vmaInvalidateAllocation, allocator, mStagingBuffer, range.dstOffset, range.size);
		}
	}


	void BufferDuplex::flush(VkCommandBuffer cmd, VmaAllocator allocator, std::span<const VkBufferCopy> ranges) {
		for(auto& range : ranges) {
			VK_CHECK(vmaFlushAllocation, allocator, mStagingBuffer, range.dstOffset, range.size);
		}

		if(mBufferHostVisibility < 1) {
			// The buffer needs to be committed to the device

			assert(cmd != nullptr);
			vkCmdCopyBuffer(cmd, mStagingBuffer, value, ranges.size(), ranges.data());
		}
	}


	ManagedBuffer BufferDuplex::detachStagingBuffer(VmaAllocator allocator) && noexcept {
		vmaUnmapMemory(allocator, mStagingBuffer);
		if(mBufferHostVisibility < 1) {
			ManagedBuffer::destroy(allocator, mStagingBuffer);
		}
		return *this;
	}

}
