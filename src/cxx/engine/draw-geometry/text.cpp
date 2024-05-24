#include <vulkan/vulkan.h> // VK_NO_PROTOTYPES, defined in other headers, makes it necessary to include this one first

#include <vk-util/error.hpp>

#include <random>

#include "gui.hpp"

#include <fmt/format.h>



namespace SKENGINE_NAME_NS {
inline namespace geom {

	inline namespace impl {

		constexpr const char* ft_error_string_or_unknown(FT_Error e) {
			auto r = FT_Error_String(e);
			if(r == nullptr) r = "unknown";
			return r;
		}


		constexpr char unknownCharReplacement = '?';

	}


	FontError::FontError(std::string ctx, FT_Error e):
		FontError(fmt::format("{}: {}", ctx, ft_error_string_or_unknown(e)))
	{ }


	FontFace FontFace::fromFile(FT_Library ft, bool useGrayscale, const char* path) {
		FontFace r;
		r.font_useGrayscale = useGrayscale;
		auto error = FT_New_Face(ft, path, 0, &r.font_face.value);
		if(error) throw FontError(fmt::format("failed to load font face \"{}\"", path), error);
		return r;
	}


	FontFace::~FontFace() {
		if(font_face.value != nullptr) {
			auto error = FT_Done_Face(font_face.value);
			if(error) throw FontError(fmt::format("failed to unload a font face"), error); // Implementation defined: may or may not unwind the stack, but will always std::terminate
			font_face = nullptr;
		}
	}


	void FontFace::setPixelSize(unsigned pixelWidth, unsigned pixelHeight) {
		auto error = FT_Set_Pixel_Sizes(font_face.value, pixelWidth, pixelHeight);
		if(error) throw FontError(fmt::format("failed to set font face size"), error);
	}


	std::pair<GlyphBitmap, codepoint_t> FontFace::getGlyphBitmap(codepoint_t c) {
		codepoint_t index = FT_Get_Char_Index(font_face.value, c);
		if(index == 0) [[unlikely]] { index = FT_Get_Char_Index(font_face.value, unknownCharReplacement); }
		if(index == 0) [[unlikely]] { throw std::runtime_error(fmt::format("failed to map required character '{}'", unknownCharReplacement)); }
		return std::pair(getGlyphBitmapByIndex(index), index);
	}


	GlyphBitmap FontFace::getGlyphBitmapByIndex(codepoint_t index) {
		auto error = FT_Load_Glyph(font_face.value, index, FT_LOAD_DEFAULT);
		if(error) throw FontError(fmt::format("failed to load glyph #0x{:x}", uintmax_t(index)), error);
		error = FT_Render_Glyph(font_face.value->glyph, (font_useGrayscale? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO));
		if(error) throw FontError(fmt::format("failed to render glyph #0x{:x}", uintmax_t(index)), error);
		assert(font_face.value->glyph->bitmap.pixel_mode == (font_useGrayscale? FT_PIXEL_MODE_GRAY : FT_PIXEL_MODE_MONO));

		GlyphBitmap ins;
		auto* ftGlyph = font_face.value->glyph;
		assert((ftGlyph->bitmap.pitch >= 0) && "I don't know how to deal with a negative pitch");
		ins.xBaseline = ftGlyph->bitmap_left;
		ins.yBaseline = ftGlyph->bitmap_top;
		ins.xAdvance  = ftGlyph->linearHoriAdvance >> 16;
		ins.yAdvance  = ftGlyph->linearVertAdvance >> 16;
		ins.width     = ftGlyph->bitmap.width;
		ins.height    = ftGlyph->bitmap.rows;
		ins.pitch     = std::abs(ftGlyph->bitmap.pitch);
		ins.isGrayscale = font_useGrayscale;
		auto byteCount = ins.byteCount();
		ins.bytes = std::make_unique_for_overwrite<std::byte[]>(byteCount);
		memcpy(ins.bytes.get(), ftGlyph->bitmap.buffer, byteCount);
		///// This unused code block may be useful once I find out that bitmaps are padded in some way
		// for(unsigned i = 0; i < ins.height; ++i) {
		// 	auto rowCursor = i * ins.width;
		// 	memcpy(
		// 		ins.bytes.get() + rowCursor,
		// 		ftGlyph->bitmap.buffer + rowCursor,
		// 		ins.width );
		// }
		return ins;
	}


	TextCache::TextCache(VkDevice dev, VmaAllocator vma, VkDescriptorSetLayout dsl, std::shared_ptr<FontFace> font, unsigned short pixelHeight):
		txtcache_font(std::move(font)),
		txtcache_dev(dev),
		txtcache_vma(vma),
		txtcache_lock(nullptr),
		txtcache_imageExt({ }),
		txtcache_stagingBufferSize(0),
		txtcache_updateCounter(0),
		txtcache_pixelHeight(pixelHeight),
		txtcache_imageUpToDate(false)
	{
		{ // Descriptor pool
			VkDescriptorPoolCreateInfo dpcInfo = { };
			VkDescriptorPoolSize sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
			dpcInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			dpcInfo.maxSets = 1;
			dpcInfo.poolSizeCount = std::size(sizes);
			dpcInfo.pPoolSizes = sizes;
			VK_CHECK(vkCreateDescriptorPool, txtcache_dev.value, &dpcInfo, nullptr, &txtcache_dpool);
		}

		{ // Descriptor Set
			VkDescriptorSetAllocateInfo dsaInfo = { };
			dsaInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			dsaInfo.descriptorPool = txtcache_dpool;
			dsaInfo.descriptorSetCount = 1;
			dsaInfo.pSetLayouts = &dsl;
			VK_CHECK(vkAllocateDescriptorSets, txtcache_dev.value, &dsaInfo, &txtcache_dset);
		}
	}


	TextCache::~TextCache() {
		if(txtcache_dev.value != nullptr) {
			vkDestroyDescriptorPool(txtcache_dev.value, txtcache_dpool, nullptr);
			if(txtcache_image.value.value != nullptr) {
				vkDestroySampler(txtcache_dev.value, txtcache_sampler, nullptr);
				vkDestroyImageView(txtcache_dev.value, txtcache_imageView, nullptr);
				vkutil::Image::destroy(txtcache_vma, txtcache_image.value);
			}
			if(txtcache_stagingBuffer.value.value != nullptr) {
				vkutil::Buffer::destroy(txtcache_vma, txtcache_stagingBuffer.value);
			}
		}
	}


	void TextCache::syncWithFence(VkFence fence) noexcept {
		assert(fence != nullptr);
		if(txtcache_lock != nullptr) vkWaitForFences(txtcache_dev.value, 1, &txtcache_lock, VK_TRUE, UINT64_MAX);
		txtcache_lock = fence;
	}


	void TextCache::updateImage(VkCommandBuffer cmd) noexcept {
		using InsMap = std::unordered_map<codepoint_t, GlyphBitmap>;
		using InsPair = InsMap::value_type;
		using Layout = std::unordered_map<codepoint_t, std::vector<codepoint_t>>;
		using LayoutPair = Layout::value_type;
		using UnknownCpSet = std::unordered_set<codepoint_t>;
		using coord_t = uint32_t;

		{ // Early return check
			bool updateRequested     = ! (txtcache_charQueue.empty() && txtcache_imageUpToDate);
			bool fallbackCharMissing = txtcache_charMap.empty();
			if(! (updateRequested || fallbackCharMissing)) {
				// Even if the queue is empty, the fallback character is always inserted; this
				// ensures that a valid VkImage always exists upon calling this function.
				// This is why the function should not return immediately when no character
				// is cached.
				txtcache_lock = nullptr;
				return;
			}
		}

		auto queue = std::move(txtcache_charQueue);
		assert(txtcache_charQueue.empty());

		if(! (queue.empty() && txtcache_imageUpToDate)) {
			for(auto& mapping : txtcache_charMap) queue.insert(mapping.first);
		}

		Layout layout;
		InsMap ins;
		UnknownCpSet unknownChars;
		codepoint_t fallbackCp;
		coord_t maxWidth = 0;
		coord_t maxHeight = 0;
		coord_t maxRowWidth = 0;
		codepoint_t charCountSqrt = std::max<codepoint_t>(1, std::sqrt(double(queue.size())));
		ins.max_load_factor(1.0f);
		ins.rehash(std::ceil(float(queue.size()) * 1.3f));

		++ txtcache_updateCounter;
		txtcache_charMap.clear();
		txtcache_charMap.rehash(maxRowWidth * layout.size());

		auto appendToRow = [&](codepoint_t c, const GlyphBitmap& bmp) {
			codepoint_t plane = c / charCountSqrt;
			auto row = layout.find(plane);
			if(row == layout.end()) {
				row = layout.insert(LayoutPair(plane, { })).first;
				row->second.reserve(32);
			}
			row->second.push_back(c);
			maxRowWidth = std::max<coord_t>(row->second.size(), maxRowWidth);
			maxWidth    = std::max<coord_t>(bmp.width,  maxWidth);
			maxHeight   = std::max<coord_t>(bmp.height, maxHeight);
		};

		{ // Fallback character
			auto bmpRes = txtcache_font->getGlyphBitmap(unknownCharReplacement);
			fallbackCp = unknownCharReplacement;
			auto& bmp = ins.insert(InsPair(unknownCharReplacement, std::move(bmpRes.first))).first->second;
			appendToRow(fallbackCp, bmp);
			queue.erase(unknownCharReplacement);
		}

		txtcache_font->setPixelSize(0, txtcache_pixelHeight);

		for(codepoint_t c : queue) {
			auto bmpIdx = FT_Get_Char_Index(*txtcache_font, c);
			if(bmpIdx == 0) {
				unknownChars.insert(c);
			} else {
				auto  bmpRes = txtcache_font->getGlyphBitmapByIndex(bmpIdx);
				auto& bmp = ins.insert(InsPair(c, bmpRes)).first->second;
				appendToRow(c, bmp);
			}
		}

		if(txtcache_lock != nullptr) {
			VK_CHECK(vkWaitForFences, txtcache_dev.value, 1, &txtcache_lock, VK_TRUE, UINT64_MAX);
			txtcache_lock = nullptr;
		}

		{ // If the image already exists, destroy everything
			if(txtcache_image.value.value != nullptr) {
				vkDestroySampler(txtcache_dev.value, txtcache_sampler, nullptr);
				vkDestroyImageView(txtcache_dev.value, txtcache_imageView, nullptr);
				vkutil::Image::destroy(txtcache_vma, txtcache_image.value);
			}
		}

		{ // Create the image
			auto& charWidth  = maxWidth;
			auto& charHeight = maxHeight;
			auto& rowWidth   = maxRowWidth;
			float fCharWidth  = charWidth;
			float fCharHeight = charHeight;

			vkutil::ImageCreateInfo icInfo = { };
			assert(layout.size() < UINT_MAX); // There are less than 1024 Unicode planes, I believe
			icInfo.extent = { charWidth * rowWidth, charHeight * unsigned(layout.size()), 1 };
			txtcache_imageExt = { icInfo.extent.width, icInfo.extent.height };
			VkDeviceSize imageByteSize = icInfo.extent.width * icInfo.extent.height * 4; // The glyph has a 1-byte grayscale texel, the image wants a 4-byte RGBA texel because GLSL said so

			{ // Enlarge the staging buffer, if necessary
				vkutil::BufferCreateInfo bcInfo = { };
				bcInfo.size  = imageByteSize;
				bcInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
				bool bufferExists   = ! (txtcache_stagingBuffer.value.value == nullptr);
				bool bufferTooSmall = txtcache_stagingBufferSize < imageByteSize;
				// Buffer exists and is big enough => don't destroy it, don't create it
				// Buffer exists and is too small  => destroy it; then it needs to be recreated
				// Buffer does not exist           => create it
				if(bufferExists && bufferTooSmall) {
					vkutil::Buffer::destroy(txtcache_vma, txtcache_stagingBuffer.value);
					bufferExists = false;
				}
				if(! bufferExists) {
					txtcache_stagingBuffer = vkutil::ManagedBuffer::createStagingBuffer(txtcache_vma, bcInfo);
					txtcache_stagingBufferSize = bcInfo.size;
				}
			}

			float fImgWidth;
			float fImgHeight;
			{ // Allocate the image
				icInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				icInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
				icInfo.type = VK_IMAGE_TYPE_2D;
				icInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				icInfo.samples = VK_SAMPLE_COUNT_1_BIT;
				icInfo.tiling = VK_IMAGE_TILING_LINEAR;
				icInfo.arrayLayers = 1;
				icInfo.mipLevels = 1;
				fImgWidth  = icInfo.extent.width;
				fImgHeight = icInfo.extent.height;
				vkutil::AllocationCreateInfo acInfo = { };
				acInfo.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
				acInfo.vmaFlags = { };
				acInfo.vmaUsage = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
				txtcache_image = vkutil::Image::create(txtcache_vma, icInfo, acInfo);
			}

			void* devGlyphPtr;
			VK_CHECK(vmaMapMemory, txtcache_vma, txtcache_stagingBuffer.value, &devGlyphPtr);
			size_t slotRowStride = icInfo.extent.width;
			memset(devGlyphPtr, 0, imageByteSize);
			auto pixelOffset = [&](unsigned xSlot, unsigned ySlot, unsigned xPix, unsigned yPix) {
				// 0 - - - 0 - - - 0                       0 - - - 0 - - - 0
				// |       |       |                       |   C...|.......|
				// |   A...|.......|                       |...D   |       |
				// |.......|.......|                       |       |       |
				// 0 - - - 0 - - - 0                       0 - - - 0 - - - 0
				// |.......|.......|                       |       |       |
				// |...B   |       |                       |   E...|...F   |
				// |       |       |                       |       |       |
				// 0 - - - 0 - - - 0                       0 - - - 0 - - - 0
				//
				// ^^^ 1 row = stride * rows per slot * slots;
				//                        1 row = stride * rows per slot ^^^
				//                        1 col = cols per slot
				auto offset =
					+ (4 * ySlot * slotRowStride * charHeight)
					+ (4 * yPix * slotRowStride)
					+ (4 * xSlot * charWidth)
					+ (4 * xPix);
				assert(offset < imageByteSize);
				return offset;
			};
			auto pixelAt = [&](unsigned xSlot, unsigned ySlot, unsigned xPix, unsigned yPix) {
				return reinterpret_cast<uint8_t*>(devGlyphPtr) + pixelOffset(xSlot, ySlot, xPix, yPix);
			};
			auto copyChar = [&](unsigned xSlot, unsigned ySlot, codepoint_t c) {
				#ifndef NDEBUG
					auto& bmp = ins.find(c)->second;
				#else
					auto bmpIter = ins.find(c);
					assert(bmpIter != ins.end());
					auto& bmp = bmpIter->second;
				#endif
				if(txtcache_font->usesGrayscale()) {
					for(size_t yPix = 0; yPix < bmp.height; ++yPix)
					for(size_t xPix = 0; xPix < bmp.width;  ++xPix) {
						auto v = uint8_t(bmp.bytes[(yPix * bmp.width) + xPix]);
						auto pixel = pixelAt(xSlot, ySlot, xPix, yPix);
						pixel[0] = pixel[1] = pixel[2] = 0xff;
						pixel[3] = v;
					}
				} else {
					for(size_t yPix = 0; yPix < bmp.height; ++yPix)
					for(size_t xPix = 0; xPix < bmp.width;  ++xPix) {
						auto i = (yPix * bmp.pitch) + (xPix / 8);
						auto byte = uint8_t(bmp.bytes[i]);
						auto pixel = pixelAt(xSlot, ySlot, xPix, yPix);
						pixel[0] = pixel[1] = pixel[2] = 0xff;
						pixel[3] = ((byte >> (7 - (xPix % 8))) & 1) * 0xff;
					}
				}
				float fBmpSize[2] = { float(bmp.width), float(bmp.height) };
				float topLeft[2] = {
					(float(xSlot) * fCharWidth ) / fImgWidth,
					(float(ySlot) * fCharHeight) / fImgHeight };
				float bottomRight[2] = {
					((float(xSlot) * fCharWidth ) + fBmpSize[0]) / fImgWidth,
					((float(ySlot) * fCharHeight) + fBmpSize[1]) / fImgHeight };
				txtcache_charMap.insert(CharMap::value_type(
					c,
					CharDescriptor {
						.topLeftUv { topLeft[0], topLeft[1] },
						.bottomRightUv { bottomRight[0], bottomRight[1] },
						.size { fBmpSize[0] / float(txtcache_pixelHeight), fBmpSize[1] / float(txtcache_pixelHeight) },
						.baseline {
							float(bmp.xBaseline) / float(txtcache_pixelHeight),
							float(bmp.yBaseline) / float(txtcache_pixelHeight) },
						.advance {
							float(bmp.xAdvance) / float(txtcache_pixelHeight),
							float(bmp.yAdvance) / float(txtcache_pixelHeight) } }));
			};
			for(unsigned y = 0; auto& row : layout) {
				for(unsigned x = 0; codepoint_t c : row.second) {
					copyChar(x, y, c);
					++ x;
				}
				++ y;
			}
			vmaUnmapMemory(txtcache_vma, txtcache_stagingBuffer.value);

			if(! unknownChars.empty()) {
				auto& desc = txtcache_charMap.find(fallbackCp)->second;
				for(codepoint_t c : unknownChars) {
					txtcache_charMap.insert(CharMap::value_type(c, desc));
				}
			}

			{ // Buffer copies and barriers
				VkBufferImageCopy cp = { };
				cp.bufferRowLength   = icInfo.extent.width;
				cp.bufferImageHeight = icInfo.extent.height;
				cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				cp.imageSubresource.layerCount = 1;
				cp.imageExtent = icInfo.extent;
				VkImageMemoryBarrier2 bar0 = { };
				VkImageMemoryBarrier2 bar1;
				bar0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
				bar0.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
				bar0.srcAccessMask = VK_ACCESS_2_NONE;
				bar0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				bar0.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				bar0.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				bar0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				bar0.image = txtcache_image.value;
				bar0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bar0.subresourceRange.levelCount = 1;
				bar0.subresourceRange.layerCount = 1;
				bar1 = bar0;
				bar1.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				bar1.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				bar1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				bar1.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
				bar1.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
				bar1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				VkDependencyInfo depInfo = { };
				depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				depInfo.imageMemoryBarrierCount = 1;
				depInfo.pImageMemoryBarriers = &bar0;
				vkCmdPipelineBarrier2(cmd, &depInfo);
				vkCmdCopyBufferToImage(cmd, txtcache_stagingBuffer.value, txtcache_image.value, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
				depInfo.pImageMemoryBarriers = &bar1;
				vkCmdPipelineBarrier2(cmd, &depInfo);
			}
		}

		{ // Create the image view and sampler
			VkImageViewCreateInfo ivcInfo = { };
			ivcInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ivcInfo.image = txtcache_image.value;
			ivcInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ivcInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			ivcInfo.components = { };
			ivcInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ivcInfo.subresourceRange.levelCount = 1;
			ivcInfo.subresourceRange.layerCount = 1;
			VK_CHECK(vkCreateImageView, txtcache_dev.value, &ivcInfo, nullptr, &txtcache_imageView);
			VkSamplerCreateInfo scInfo = { };
			scInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			scInfo.magFilter = scInfo.minFilter = VK_FILTER_NEAREST;
			scInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			scInfo.addressModeU = scInfo.addressModeV = scInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
			scInfo.anisotropyEnable = true;
			scInfo.maxAnisotropy = 1.0f;
			scInfo.maxLod = 1.0f;
			VK_CHECK(vkCreateSampler, txtcache_dev.value, &scInfo, nullptr, &txtcache_sampler);
		}

		{ // Update the descriptor set
			VkDescriptorImageInfo diInfo = { };
			diInfo.sampler = txtcache_sampler;
			diInfo.imageView = txtcache_imageView;
			diInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkWriteDescriptorSet wDset = { };
			wDset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wDset.dstSet = txtcache_dset;
			wDset.dstBinding = 0;
			wDset.descriptorCount = 1;
			wDset.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			wDset.pImageInfo = &diInfo;
			vkUpdateDescriptorSets(txtcache_dev.value, 1, &wDset, 0, nullptr);
		}

		txtcache_imageUpToDate = true;
	}


	void TextCache::trimChars(codepoint_t maxCharCount) {
		static_assert(std::is_unsigned_v<codepoint_t>);
		if(maxCharCount < 1) {
			txtcache_charMap.clear();
			txtcache_imageUpToDate = false;
		}
		if(txtcache_charMap.size() <= maxCharCount) return;

		auto rng = std::minstd_rand(txtcache_updateCounter);

		auto selectVictim = [&](size_t firstCandidate) {
			firstCandidate = firstCandidate % txtcache_charMap.bucket_count();
			decltype(txtcache_charMap.bucket_size(0)) bucketSize;
			bucketSize = txtcache_charMap.bucket_size(firstCandidate);
			while(bucketSize < 1) {
				firstCandidate = (firstCandidate + 1) % txtcache_charMap.bucket_count();
				bucketSize = txtcache_charMap.bucket_size(firstCandidate);
			}
			return firstCandidate;
		};

		txtcache_imageUpToDate = false;
		size_t victimIdx = rng() - decltype(rng)::min();
		while(txtcache_charMap.size() > maxCharCount) {
			victimIdx = selectVictim(victimIdx);
			auto victim = txtcache_charMap.begin(victimIdx);
			txtcache_charMap.erase(victim->first);
		}
	}

}}
