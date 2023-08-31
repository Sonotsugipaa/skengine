#include "engine.hpp"

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include <posixfio.hpp>

#include <vk-util/error.hpp>

#include <vulkan/vulkan_format_traits.hpp>



namespace SKENGINE_NAME_NS {

	namespace {

		VkFormat format_from_locator(std::string_view locator) {
			size_t sz = locator.size();
			if(0 == locator.compare(sz-9,  9,  ".fmat.r8u"))     return VK_FORMAT_R8_UNORM;
			if(0 == locator.compare(sz-10, 10, ".fmat.ra8u"))    return VK_FORMAT_R8G8_UNORM;
			if(0 == locator.compare(sz-11, 11, ".fmat.rgb8u"))   return VK_FORMAT_R8G8B8_UNORM;
			if(0 == locator.compare(sz-12, 12, ".fmat.rgba8u"))  return VK_FORMAT_R8G8B8A8_UNORM;
			if(0 == locator.compare(sz-13, 13, ".fmat.rgba16u")) return VK_FORMAT_R16G16B16A16_UNORM;
			if(0 == locator.compare(sz-13, 13, ".fmat.rgba16f")) return VK_FORMAT_R16G16B16A16_SFLOAT;
			if(0 == locator.compare(sz-13, 13, ".fmat.rgba32u")) return VK_FORMAT_R32G32B32A32_SFLOAT;
			return VK_FORMAT_UNDEFINED;
		}


		VkComponentMapping format_mapping(VkFormat fmt) {
			#define MAP_(F_, M_) case F_: return M_;
			static constexpr VkComponentMapping m1 = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE };
			static constexpr VkComponentMapping m2 = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G };
			static constexpr VkComponentMapping m3 = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_ONE };
			static constexpr VkComponentMapping m4 = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			switch(fmt) {
				default: return { };
				MAP_(VK_FORMAT_R8_UNORM,       m1)
				MAP_(VK_FORMAT_R8G8_UNORM,     m2)
				MAP_(VK_FORMAT_R8G8B8_UNORM,   m3)
				MAP_(VK_FORMAT_R8G8B8A8_UNORM, m4)
				MAP_(VK_FORMAT_R8_SNORM,       m1)
				MAP_(VK_FORMAT_R8G8_SNORM,     m2)
				MAP_(VK_FORMAT_R8G8B8_SNORM,   m3)
				MAP_(VK_FORMAT_R8G8B8A8_SNORM, m4)
			}
			#undef MAP_
		}

	}



	size_t texture_size_bytes(const Material::Texture& tex) {
		if(tex.is_copy) return 0;
		auto& info = tex.image.info();
		return info.extent.width * info.extent.height * info.extent.depth * vk::blockSize(vk::Format(info.format));
	}


	void create_texture_from_pixels(
			Engine& e,
			Material::Texture* dst,
			const void*        src,
			VkFormat fmt,
			size_t   width,
			size_t   height
	) {
		assert(width > 0); assert(height > 0);

		auto fmt_block_size = vk::blockSize(vk::Format(fmt));
		auto fmt_map        = format_mapping(fmt);

		auto dev = e.getDevice();
		auto vma = e.getVmaAllocator();

		auto staging_buffer_info = vkutil::BufferCreateInfo {
			.size  = fmt_block_size * width * height,
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.qfamSharing = { } };
		auto staging_buffer = vkutil::ManagedBuffer::createStagingBuffer(vma, staging_buffer_info);

		VkDependencyInfo      bar_dep = { };
		VkImageMemoryBarrier2 bar     = { }; {
			bar_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			bar_dep.imageMemoryBarrierCount = 1;
			bar_dep.pImageMemoryBarriers    = &bar;
			bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bar.subresourceRange.layerCount = 1;
			bar.subresourceRange.levelCount = 1;
		}

		vkutil::ImageCreateInfo ic_info = {
			.flags         = { },
			.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.extent        = { uint32_t(width), uint32_t(height), 1 },
			.format        = fmt,
			.type          = VK_IMAGE_TYPE_2D,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.samples       = VK_SAMPLE_COUNT_1_BIT,
			.tiling        = VK_IMAGE_TILING_LINEAR,
			.qfamSharing   = { },
			.arrayLayers   = 1,
			.mipLevels     = 1 };
		vkutil::AllocationCreateInfo ac_info = { };
		ac_info.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		ac_info.vmaUsage = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
		dst->image   = vkutil::ManagedImage::create(vma, ic_info, ac_info);
		dst->is_copy = false;

		{ // Copy the data into the staging buffer
			auto dst_ptr = staging_buffer.map<void>(vma);
			memcpy(dst_ptr, src, staging_buffer_info.size);
			staging_buffer.unmap(vma);
		}

		VkCommandBuffer cmd; { // Allocate and begin the buffer
			VkCommandBufferAllocateInfo cba_info = { };
			cba_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cba_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cba_info.commandPool = e.getTransferCmdPool();
			cba_info.commandBufferCount = 1;
			VK_CHECK(vkAllocateCommandBuffers, dev, &cba_info, &cmd);
			VkCommandBufferBeginInfo cbb_info = { };
			cbb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			VK_CHECK(vkBeginCommandBuffer, cmd, &cbb_info);
		}

		{ // Record the copy-to-image operation
			bar.image = dst->image;
			bar.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
			bar.srcAccessMask = VK_ACCESS_2_NONE;
			bar.srcStageMask  = VK_PIPELINE_STAGE_2_NONE;
			bar.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			bar.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			bar.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			vkCmdPipelineBarrier2(cmd, &bar_dep);
			VkBufferImageCopy cp = { };
			cp.bufferRowLength   = width;
			cp.bufferImageHeight = height;
			cp.imageExtent       = { uint32_t(width), uint32_t(height), 1 };
			cp.imageSubresource.layerCount = 1;
			cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vkCmdCopyBufferToImage(cmd, staging_buffer, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
			bar.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			bar.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			bar.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			bar.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			bar.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			bar.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			vkCmdPipelineBarrier2(cmd, &bar_dep);
		}

		{ // End and submit the buffer
			VK_CHECK(vkEndCommandBuffer, cmd);
			VkFence fence;
			VkFenceCreateInfo fc_info = { };
			fc_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			VK_CHECK(vkCreateFence, dev, &fc_info, nullptr, &fence);
			VkSubmitInfo submit = { };
			submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit.commandBufferCount = 1;
			submit.pCommandBuffers    = &cmd;
			VK_CHECK(vkQueueSubmit, e.getQueues().graphics, 1, &submit, fence);
			VK_CHECK(vkWaitForFences, dev, 1, &fence, true, UINT64_MAX);
			vkDestroyFence(dev, fence, nullptr);
		}

		vkFreeCommandBuffers(dev, e.getTransferCmdPool(), 1, &cmd);
		vkutil::ManagedBuffer::destroy(vma, staging_buffer);

		{ // Create the image view
			VkImageViewCreateInfo ivc_info = { };
			ivc_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivc_info.image      = dst->image;
			ivc_info.format     = fmt;
			ivc_info.viewType   = VK_IMAGE_VIEW_TYPE_2D;
			ivc_info.components = fmt_map;
			ivc_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ivc_info.subresourceRange.layerCount = 1;
			ivc_info.subresourceRange.levelCount = 1;
			VK_CHECK(vkCreateImageView, dev, &ivc_info, nullptr, &dst->image_view);
		}

		{ // Create the image sampler
			VkSamplerCreateInfo sc_info = { };
			sc_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			sc_info.maxLod = ic_info.mipLevels;
			sc_info.addressModeU =
			sc_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			sc_info.anisotropyEnable = true;
			sc_info.maxAnisotropy    = e.getPhysDeviceProperties().limits.maxSamplerAnisotropy;
			sc_info.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			sc_info.minFilter = VK_FILTER_NEAREST;
			sc_info.magFilter = VK_FILTER_NEAREST;
			VK_CHECK(vkCreateSampler, dev, &sc_info, nullptr, &dst->sampler);
		}
	}


	bool create_texture_from_file(
			Engine& e,
			Material::Texture* dst,
			size_t*            dst_width,
			size_t*            dst_height,
			const char*        locator
	) {
		using posixfio::MemProtFlags;
		using posixfio::MemMapFlags;
		#define FAILED_PRE_ "Failed to load texture \"{}\": "

		assert(dst_width  != nullptr);
		assert(dst_height != nullptr);

		std::string_view locator_sv = locator;

		VkFormat fmt = format_from_locator(locator_sv);
		if(fmt == VK_FORMAT_UNDEFINED) {
			e.logger().error(FAILED_PRE_ "bad format/extension", locator_sv);
			return false;
		}
		size_t block_size = vk::blockSize(vk::Format(fmt));

		posixfio::File file;
		try {
			file = posixfio::File::open(locator, O_RDONLY);
		} catch(posixfio::Errcode& ex) {
			e.logger().error(FAILED_PRE_ "errno {}", locator_sv, ex.errcode);
			return false;
		}

		auto file_len = std::make_unsigned_t<posixfio::off_t>(file.lseek(0, SEEK_END));
		auto mmap     = file.mmap(file_len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
		void* ptr     = 2 + mmap.get<size_t>();
		auto& w       = mmap.get<size_t>()[0];
		auto& h       = mmap.get<size_t>()[1];
		auto  pixel_n = w * h;
		file_len     -= 2 * sizeof(w);

		if(pixel_n * block_size > file_len) {
			e.logger().error(FAILED_PRE_ "bad image size ({}x{} > {})", locator_sv, w, h, file_len / block_size);
			return false;
		}

		create_texture_from_pixels(e, dst, ptr, fmt, w, h);
		*dst_width  = w;
		*dst_height = h;
		return true;

		#undef FAILED_PRE_
	}

}
