#pragma once

#include <vulkan/vulkan.h>

#include <vma/vk_mem_alloc.h>

#include <span>
#include <vector>
#include <stdexcept>
#include <unordered_map>



namespace vkutil {

	class StaticBufferResizeError : public std::invalid_argument {
	public:
		using std::invalid_argument::invalid_argument;
	};


	class NotHostVisibleError : public std::invalid_argument {
	public:
		using std::invalid_argument::invalid_argument;
	};


	enum class HostAccess : uint32_t {
		eNone = 0b00,
		eRd   = 0b01,
		eWr   = 0b10,
		eRdWr = 0b11
	};


	struct Buffer {
		VkBuffer          value;
		VmaAllocation     alloc;

		inline operator VkBuffer()      { return value; }
		inline operator VmaAllocation() { return alloc; }

		template<typename T>
		T* map(VmaAllocator, HostAccess);

		void flush           (VmaAllocator);
		void invalidateCache (VmaAllocator);
		void unmap           (VmaAllocator);
	};

	struct Image {
		VkImage           value;
		VmaAllocation     alloc;

		inline operator VkImage()       { return value; }
		inline operator VmaAllocation() { return alloc; }
	};

	template<>
	void* Buffer::map<void>(VmaAllocator, HostAccess);

	template<typename T>
	T* Buffer::map(VmaAllocator a, HostAccess ha) { return reinterpret_cast<T*>(map<void>(a, ha)); }


	struct BufferAllocateInfo {
		VkDeviceSize        size;
		VkBufferUsageFlags  usage;
		std::span<uint32_t> qfam_sharing;
	};


	struct ImageAllocateInfo {
		VkImageUsageFlags     usage;
		uint32_t              array_layers;
		VkExtent3D            extent;
		VkFormat              format;
		VkImageType           type;
		VkImageLayout         initial_layout;
		uint32_t              mip_levels;
		VkSampleCountFlagBits samples;
		VkImageTiling         tiling;
		std::span<uint32_t>   qfam_sharing;
	};


	struct MemAllocateInfo {
		VkMemoryPropertyFlags    required_mem_flags;
		VkMemoryPropertyFlags    preferred_mem_flags;
		VmaAllocationCreateFlags vma_flags;
	};


	class ManagedBuffer : public Buffer {
	public:
		static ManagedBuffer create(VmaAllocator, const BufferAllocateInfo&, const MemAllocateInfo&);
		static void         destroy(VmaAllocator, ManagedBuffer&) noexcept;

		static ManagedBuffer createStagingBuffer(VmaAllocator, const BufferAllocateInfo&);

		void cmdCopyFrom (VkCommandBuffer, VkDeviceSize copy_length, VkBuffer src);
		void cmdCopyFrom (VkCommandBuffer, VkDeviceSize copy_length, VkImage  src);
		void cmdCopyTo   (VkCommandBuffer, VkDeviceSize copy_length, VkBuffer dst);
		void cmdCopyTo   (VkCommandBuffer, VkDeviceSize copy_length, VkImage  dst);

		inline auto memoryTypeIndex()     const noexcept { return mMemType; }
		inline auto memoryPropertyFlags() const noexcept { return mMemProps; }

		template<typename T>
		T* map(VmaAllocator, HostAccess);

		void flush           (VmaAllocator);
		void invalidateCache (VmaAllocator);
		void unmap           (VmaAllocator);

	private:
		uint32_t              mMemType;
		VkMemoryPropertyFlags mMemProps;
		HostAccess            mMappedHostAccess;
	};

	template<>
	void* ManagedBuffer::map<void>(VmaAllocator, HostAccess);

	template<typename T>
	T* ManagedBuffer::map(VmaAllocator a, HostAccess ha) { return reinterpret_cast<T*>(map<void>(a, ha)); }


	class BufferDuplex : public Buffer {
	public:
		static ManagedBuffer create(VmaAllocator, const BufferAllocateInfo&, const MemAllocateInfo&);
		static void         destroy(VmaAllocator, ManagedBuffer&) noexcept;

		static ManagedBuffer createVertexInputBuffer (VmaAllocator, const BufferAllocateInfo&);
		static ManagedBuffer createIndexInputBuffer  (VmaAllocator, const BufferAllocateInfo&);
		static ManagedBuffer createUniformBuffer     (VmaAllocator, const BufferAllocateInfo&);
		static ManagedBuffer createStorageBuffer     (VmaAllocator, const BufferAllocateInfo&);

		void flush           (VmaAllocator);
		void invalidateCache (VmaAllocator);

	private:
		ManagedBuffer mStagingBuffer;
		size_t        mSize;
		void*         mMappedPtr;
		bool          mBufferIsHostVisible;
	};

}
