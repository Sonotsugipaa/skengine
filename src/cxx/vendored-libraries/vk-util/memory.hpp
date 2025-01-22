#pragma once

#include <vulkan/vulkan.h>

#include <vma/vk_mem_alloc.h>

#include <span>
#include <vector>
#include <stdexcept>
#include <unordered_map>

#include "command_pool.hpp"



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


	enum class VmaAutoMemoryUsage {
		eAuto             = VMA_MEMORY_USAGE_AUTO,
		eAutoPreferDevice = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		eAutoPreferHost   = VMA_MEMORY_USAGE_AUTO_PREFER_HOST
	};


	struct BufferCreateInfo {
		VkDeviceSize        size;
		VkBufferUsageFlags  usage;
		std::span<uint32_t> qfamSharing;
	};


	struct MemoryRange {
		VkDeviceSize offset;
		VkDeviceSize size;
	};


	struct ImageCreateInfo {
		VkImageCreateFlags    flags;
		VkImageUsageFlags     usage;
		VkExtent3D            extent;
		VkFormat              format;
		VkImageType           type;
		VkImageLayout         initialLayout;
		VkSampleCountFlagBits samples;
		VkImageTiling         tiling;
		std::span<uint32_t>   qfamSharing;
		uint32_t              arrayLayers;
		uint32_t              mipLevels;
	};


	struct AllocationCreateInfo {
		VkMemoryPropertyFlags    requiredMemFlags;
		VkMemoryPropertyFlags    preferredMemFlags;
		VmaAllocationCreateFlags vmaFlags;
		VmaAutoMemoryUsage       vmaUsage;
	};


	struct Buffer {
		VkBuffer      value;
		VmaAllocation alloc;

		static Buffer create  (VmaAllocator, const BufferCreateInfo&, const AllocationCreateInfo&);
		static void   destroy (VmaAllocator, Buffer&) noexcept;

		inline operator VkBuffer()      { return value; }
		inline operator VmaAllocation() { return alloc; }

		template<typename T>
		T* map(VmaAllocator);

		void invalidate (VmaAllocator, std::span<const MemoryRange>);
		void flush      (VmaAllocator, std::span<const MemoryRange>);
		void invalidate (VmaAllocator vma) { invalidate (vma, std::span<const MemoryRange>({ MemoryRange { 0, VK_WHOLE_SIZE } })); }
		void flush      (VmaAllocator vma) { flush      (vma, std::span<const MemoryRange>({ MemoryRange { 0, VK_WHOLE_SIZE } })); }
		void unmap      (VmaAllocator);
	};

	struct Image {
		VkImage       value;
		VmaAllocation alloc;

		static Image create  (VmaAllocator, const ImageCreateInfo&, const AllocationCreateInfo&);
		static void  destroy (VmaAllocator, Image&) noexcept;

		inline operator VkImage()       { return value; }
		inline operator VmaAllocation() { return alloc; }
	};

	template<>
	void* Buffer::map<void>(VmaAllocator);

	template<typename T>
	T* Buffer::map(VmaAllocator allocator) { return reinterpret_cast<T*>(map<void>(allocator)); }


	class ManagedBuffer : public Buffer {
	public:
		struct Info {
			VkMemoryPropertyFlags memoryProperties;
			uint32_t              memoryTypeIndex;
			VkDeviceSize          size;
		};

		/// \brief Exactely equivalent to Buffer::create.
		///
		/// The only difference with it is that a ManagedBuffer remembers
		/// its memory type, while Buffer::create discards that information.
		///
		/// A ManagedBuffer created with this function can be destroyed with
		/// Buffer::destroy.
		///
		static ManagedBuffer create(VmaAllocator, const BufferCreateInfo&, const AllocationCreateInfo&);

		/// \brief Exactely equivalent to Buffer::destroy.
		///
		/// \see ManagedBuffer::create
		///
		static void destroy(VmaAllocator, ManagedBuffer&) noexcept;

		static ManagedBuffer createStagingBuffer(VmaAllocator, const BufferCreateInfo&);
		static ManagedBuffer createUniformBuffer(VmaAllocator, const BufferCreateInfo&);
		static ManagedBuffer createStorageBuffer(VmaAllocator, const BufferCreateInfo&);

		inline operator VkBuffer()      const { return value; }
		inline operator VmaAllocation() const { return alloc; }

		inline const auto& info() const noexcept { return mInfo; }

	protected:
		Info mInfo;
	};


	class ManagedImage : public Image {
	public:
		struct Info {
			VkMemoryPropertyFlags memoryProperties;
			uint32_t              memoryTypeIndex;
			uint32_t              mipLevelCount;
			VkExtent3D            extent;
			VkFormat              format;
		};

		static ManagedImage create(VmaAllocator, const ImageCreateInfo&, const AllocationCreateInfo&);

		/// \brief Exactely equivalent to Image::destroy.
		///
		/// A ManagedImage created with this function can be destroyed with
		/// Image::destroy.
		///
		static void destroy(VmaAllocator, ManagedImage&) noexcept;

		inline operator VkImage()       const { return value; }
		inline operator VmaAllocation() const { return alloc; }

		inline const auto& info() const noexcept { return mInfo; }

	protected:
		Info mInfo;
	};


	template <typename HandleType>
	class Duplex : public HandleType {
	public:
		void*       mappedPtr()       noexcept { return mMappedPtr; }
		const void* mappedPtr() const noexcept { return mMappedPtr; }

		template <typename T>
		T*       mappedPtr()       noexcept { return reinterpret_cast<T*>(mMappedPtr); }

		template <typename T>
		const T* mappedPtr() const noexcept { return reinterpret_cast<const T*>(mMappedPtr); }

	protected:
		Duplex() = default;

		ManagedBuffer mStagingBuffer;
		void*         mMappedPtr;
	};


	class BufferDuplex : public Duplex<ManagedBuffer> {
	public:
		static BufferDuplex create  (VmaAllocator, const BufferCreateInfo&, const AllocationCreateInfo&, HostAccess);
		static void         destroy (VmaAllocator, BufferDuplex&) noexcept;

		static BufferDuplex createVertexInputBuffer (VmaAllocator, const BufferCreateInfo&, HostAccess = HostAccess::eWr);
		static BufferDuplex createIndexInputBuffer  (VmaAllocator, const BufferCreateInfo&, HostAccess = HostAccess::eWr);
		static BufferDuplex createUniformBuffer     (VmaAllocator, const BufferCreateInfo&, HostAccess = HostAccess::eWr);
		static BufferDuplex createStorageBuffer     (VmaAllocator, const BufferCreateInfo&, HostAccess);

		void invalidate (VkCommandBuffer, VmaAllocator, std::span<const VkBufferCopy>);
		void flush      (VkCommandBuffer, VmaAllocator, std::span<const VkBufferCopy>);
		void invalidate (VkCommandBuffer cmd, VmaAllocator vma) { invalidate (cmd, vma, std::span<const VkBufferCopy>({ VkBufferCopy { .srcOffset = 0, .dstOffset = 0, .size = mInfo.size } })); }
		void flush      (VkCommandBuffer cmd, VmaAllocator vma) { flush      (cmd, vma, std::span<const VkBufferCopy>({ VkBufferCopy { .srcOffset = 0, .dstOffset = 0, .size = mInfo.size } })); }

		bool isHostVisible() const noexcept { return mBufferHostVisibility >= 1; }
		bool isHostCoherent() const noexcept { return mBufferHostVisibility >= 2; }

		ManagedBuffer detachStagingBuffer(VmaAllocator) && noexcept;

	private:
		uint_least8_t mBufferHostVisibility; // 0: staged  1: visible  2: visible and coherent
	};

}
