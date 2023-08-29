#include "engine.hpp"

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include <posixfio.hpp>

#include <vk-util/error.hpp>

#include <vulkan/vulkan_format_traits.hpp>



namespace SKENGINE_NAME_NS {

	namespace {

		VkFormat format_from_locator(std::string_view locator, bool use_signed_fmt) {
			#define SIGN_(F_) (use_signed_fmt? F_ ## _SNORM : F_ ## _UNORM)
			size_t sz = locator.size();
			if(0 == locator.compare(sz-9,  9,  ".fmat.r8u"))     return SIGN_(VK_FORMAT_R8);
			if(0 == locator.compare(sz-10, 10, ".fmat.ra8u"))    return SIGN_(VK_FORMAT_R8G8);
			if(0 == locator.compare(sz-11, 11, ".fmat.rgb8u"))   return SIGN_(VK_FORMAT_R8G8B8);
			if(0 == locator.compare(sz-12, 12, ".fmat.rgba8u"))  return SIGN_(VK_FORMAT_R8G8B8A8);
			if(0 == locator.compare(sz-13, 13, ".fmat.rgba16u")) return SIGN_(VK_FORMAT_R16G16B16A16);
			if(0 == locator.compare(sz-13, 13, ".fmat.rgba16f")) return VK_FORMAT_R16G16B16A16_SFLOAT;
			if(0 == locator.compare(sz-13, 13, ".fmat.rgba32u")) return VK_FORMAT_R32G32B32A32_SFLOAT;
			#undef SIGN_
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
				#warning "TODO: try and remove the fences, barriers should synchronize anyway"
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


		auto create_texture_from_file(
				Engine& e,
				Material::Texture* dst,
				bool               use_signed_fmt,
				const char*        locator
		) {
			using posixfio::MemProtFlags;
			using posixfio::MemMapFlags;
			#define FAILED_PRE_ "Failed to load texture \"{}\": "

			struct Return {
				bool success;
				size_t width;
				size_t height;
			};
			constexpr auto fail = Return { false, 0, 0 };

			std::string_view locator_sv = locator;

			VkFormat fmt = format_from_locator(locator_sv, use_signed_fmt);
			if(fmt == VK_FORMAT_UNDEFINED) {
				e.logger().error(FAILED_PRE_ "bad format/extension", locator_sv);
				return fail;
			}
			size_t block_size = vk::blockSize(vk::Format(fmt));

			posixfio::File file;
			try {
				file = posixfio::File::open(locator, O_RDONLY);
			} catch(posixfio::Errcode& ex) {
				e.logger().error(FAILED_PRE_ "errno {}", locator_sv, ex.errcode);
				return fail;
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
				return fail;
			}

			create_texture_from_pixels(e, dst, ptr, fmt, w, h);
			return Return { true, w, h };

			#undef FAILED_PRE_
		}


		void destroy_material(VkDevice dev, VmaAllocator vma, Material& mat) {
			auto destroy_texture = [&](Material::Texture& tex) {
				if(! tex.is_copy) {
					vkDestroySampler(dev, tex.sampler, nullptr);
					vkDestroyImageView(dev, tex.image_view, nullptr);
					vkutil::ManagedImage::destroy(vma, tex.image);
				}
			};
			destroy_texture(mat.texture_diffuse);
			destroy_texture(mat.texture_normal);
			destroy_texture(mat.texture_specular);
			destroy_texture(mat.texture_emissive);
		}


		size_t texture_size_bytes(const Material::Texture& tex) {
			if(tex.is_copy) return 0;
			auto& info = tex.image.info();
			return info.extent.width * info.extent.height * info.extent.depth * vk::blockSize(vk::Format(info.format));
		}


		void create_fallback_mat(Engine& e, Material* dst) {
			// -- 0- +-
			// -0 00 +0
			// -+ 0+ ++
			constexpr uint8_t nrm0 = 0x7f-0x70;
			constexpr uint8_t nrm1 = 0x7f;
			constexpr uint8_t nrm2 = 0x7f+0x70;

			uint8_t texels_col[4][4] = {
				{ 0xff, 0x00, 0x4c, 0xff },
				{ 0x10, 0x13, 0x13, 0xff },
				{ 0x10, 0x13, 0x13, 0xff },
				{ 0xff, 0x00, 0x4c, 0xff } };
			uint8_t texels_nrm[9][4] = {
				{ nrm0, nrm0, 0xfe, 0xff }, { nrm1, nrm0, 0xfe, 0xff }, { nrm2, nrm0, 0xfe, 0xff },
				{ nrm0, nrm1, 0xfe, 0xff }, { nrm1, nrm1, 0xfe, 0xff }, { nrm2, nrm1, 0xfe, 0xff },
				{ nrm0, nrm2, 0xfe, 0xff }, { nrm1, nrm2, 0xfe, 0xff }, { nrm2, nrm2, 0xfe, 0xff } };
			uint8_t texels_spc[4] = { 0xff, 0xff, 0xff, 0x00 };
			uint8_t texels_emi[4] = { 0xff, 0xff, 0xff, 0x02 };

			create_texture_from_pixels(e, &dst->texture_diffuse,  texels_col, VK_FORMAT_R8G8B8A8_UNORM, 2, 2);
			create_texture_from_pixels(e, &dst->texture_normal,   texels_nrm, VK_FORMAT_R8G8B8A8_UNORM, 3, 3);
			create_texture_from_pixels(e, &dst->texture_specular, texels_spc, VK_FORMAT_R8G8B8A8_UNORM, 1, 1);
			create_texture_from_pixels(e, &dst->texture_emissive, texels_emi, VK_FORMAT_R8G8B8A8_UNORM, 1, 1);
		}

	}


	AssetSupplier::AssetSupplier(Engine& e, float max_inactive_ratio):
			as_engine(&e),
			as_maxInactiveRatio(max_inactive_ratio)
	{
		create_fallback_mat(e, &as_fallbackMaterial);
	}


	AssetSupplier::AssetSupplier(AssetSupplier&& mv):
			#define MV_(M_) M_(std::move(mv.M_))
			MV_(as_engine),
			MV_(as_activeModels),
			MV_(as_inactiveModels),
			MV_(as_activeMaterials),
			MV_(as_inactiveMaterials),
			MV_(as_fallbackMaterial),
			MV_(as_missingMaterials),
			MV_(as_maxInactiveRatio)
			#undef MV_
	{
		mv.as_engine = nullptr;
	}


	void AssetSupplier::destroy() {
		assert(as_engine != nullptr);
		auto dev = as_engine->getDevice();
		auto vma = as_engine->getVmaAllocator();
		msi_releaseAllModels();
		msi_releaseAllMaterials();

		auto destroy_model = [&](Models::value_type& model) {
			vkutil::BufferDuplex::destroy(vma, model.second.indices);
			vkutil::BufferDuplex::destroy(vma, model.second.vertices);
		};

		for(auto& model : as_inactiveModels) destroy_model(model);
		as_inactiveModels.clear();

		for(auto& mat : as_inactiveMaterials) destroy_material(dev, vma, mat.second);
		as_inactiveMaterials.clear();
		destroy_material(dev, vma, as_fallbackMaterial);

		as_engine = nullptr;
	}


	AssetSupplier::~AssetSupplier() {
		if(as_engine != nullptr) destroy();
	}


	DevModel AssetSupplier::msi_requestModel(std::string_view locator) {
		std::string locator_s = std::string(locator);
		auto        existing  = as_activeModels.find(locator_s);
		auto        vma       = as_engine->getVmaAllocator();

		if(existing != as_activeModels.end()) {
			return existing->second;
		}
		else if((existing = as_inactiveModels.find(locator_s)) != as_inactiveModels.end()) {
			auto ins = as_activeModels.insert(*existing);
			assert(ins.second);
			as_inactiveModels.erase(existing);
			return ins.first->second;
		}
		else {
			using posixfio::MemProtFlags;
			using posixfio::MemMapFlags;

			DevModel r;

			auto file = posixfio::File::open(locator_s.c_str(), O_RDONLY);
			auto len  = file.lseek(0, SEEK_END);
			auto mmap = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
			auto h    = fmamdl::HeaderView { mmap.get<std::byte>(), mmap.size() };
			auto materials = h.materials();
			auto meshes    = h.meshes();
			auto faces     = h.faces();
			auto indices   = h.indices();
			auto vertices  = h.vertices();

			if(meshes.empty()) {
				as_engine->logger().critical(
					"Attempting to load model \"{}\" without meshes; fallback model logic is not implemented yet",
					locator );
				abort();
			}

			{ // Create the vertex input buffers
				vkutil::BufferCreateInfo bc_info = { };
				bc_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
				bc_info.size  = indices.size_bytes();
				r.indices = vkutil::BufferDuplex::createIndexInputBuffer(vma, bc_info);
				bc_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
				bc_info.size  = vertices.size_bytes();
				r.vertices = vkutil::BufferDuplex::createVertexInputBuffer(vma, bc_info);
				r.index_count   = indices.size();
				r.vertex_count  = vertices.size();

				memcpy(r.indices.mappedPtr<void>(),  indices.data(),  indices.size_bytes());
				memcpy(r.vertices.mappedPtr<void>(), vertices.data(), vertices.size_bytes());
				as_engine->pushBuffer(r.indices);
				as_engine->pushBuffer(r.vertices);
			}

			for(auto& mesh : meshes) {
				auto& first_face   = faces[mesh.firstFace];
				auto  material_str = h.getStringView(materials[mesh.materialIndex].name);
				auto  ins = Bone {
					.mesh = Mesh {
						.index_count = uint32_t(mesh.indexCount),
						.first_index = uint32_t(first_face.firstIndex) },
					.material      = std::string(material_str),
					.position_xyz  = { },
					.direction_ypr = { },
					.scale_xyz     = { 1.0f, 1.0f, 1.0f } };
				r.bones.push_back(std::move(ins));
			}

			as_activeModels.insert(Models::value_type(std::move(locator_s), r));
			double size_kib = indices.size_bytes() + vertices.size_bytes();
			as_engine->logger().info("Loaded model \"{}\" ({:.3f} KiB)", locator, size_kib / 1000.0);

			return r;
		}
	}


	void AssetSupplier::msi_releaseModel(std::string_view locator) noexcept {
		std::string locator_s = std::string(locator);
		auto        existing  = as_activeModels.find(locator_s);
		auto        vma       = as_engine->getVmaAllocator();

		if(existing != as_activeModels.end()) {
			// Move to the inactive map
			as_inactiveModels.insert(*existing);
			as_activeModels.erase(existing);
			if(as_maxInactiveRatio < float(as_inactiveModels.size()) / float(as_activeModels.size())) {
				auto victim = as_inactiveModels.begin();
				vkutil::BufferDuplex::destroy(vma, victim->second.indices);
				vkutil::BufferDuplex::destroy(vma, victim->second.vertices);
				as_inactiveModels.erase(victim);
			}
			as_engine->logger().info("Released model \"{}\"", locator);
		} else {
			as_engine->logger().debug("Tried to release model \"{}\", but it's not loaded", locator);
		}
	}


	void AssetSupplier::msi_releaseAllModels() noexcept {
		std::vector<std::string> queue;
		queue.reserve(as_activeModels.size());
		for(auto& model : as_activeModels) queue.push_back(model.first);
		for(auto& loc   : queue)           msi_releaseModel(loc);
	}


	Material AssetSupplier::msi_requestMaterial(std::string_view locator) {
		std::string locator_s = std::string(locator);
		auto        existing  = as_activeMaterials.find(locator_s);

		if(existing != as_activeMaterials.end()) {
			return existing->second;
		}
		else if((existing = as_inactiveMaterials.find(locator_s)) != as_inactiveMaterials.end()) {
			auto ins = as_activeMaterials.insert(*existing);
			assert(ins.second);
			as_inactiveMaterials.erase(existing);
			return ins.first->second;
		}
		else {
			using posixfio::MemProtFlags;
			using posixfio::MemMapFlags;
			using mf_e  = fmamdl::material_flags_e;
			using mf_ec = fmamdl::MaterialFlags;

			Material r;

			auto& log = as_engine->logger();
			posixfio::File file;
			try {
				file = posixfio::File::open(locator_s.c_str(), O_RDONLY);
			} catch(posixfio::Errcode& ex) {
				log.error("Failed to load material \"{}\" (errno {}), using fallback", locator_s, ex.errcode);
				as_missingMaterials.insert(std::move(locator_s));
				return as_fallbackMaterial;
			}
			auto len  = file.lseek(0, SEEK_END);
			auto mmap = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
			auto h    = fmamdl::MaterialView { mmap.get<std::byte>(), mmap.size() };

			auto flags = mf_e(h.flags());

			auto load_texture = [&](
					Material::Texture& dst,
					const Material::Texture& fallback,
					mf_ec        flag,
					bool         use_signed_fmt,
					fmamdl::u8_t fma_value,
					const char*  name
			) {
				if(flags & mf_e(flag)) {
					using fmamdl::u4_t;
					using fmamdl::u8_t;
					auto fmt    = use_signed_fmt? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_UNORM;
					auto value4 = // Convert from P1111U1111 to U1111, since type punning is not a valid option here
						((u4_t(fma_value >> u8_t(24)) & u4_t(0xff)) << u4_t(24)) |
						((u4_t(fma_value >> u8_t(16)) & u4_t(0xff)) << u4_t(16)) |
						((u4_t(fma_value >> u8_t( 8)) & u4_t(0xff)) << u4_t( 8)) |
						((u4_t(fma_value >> u8_t( 0)) & u4_t(0xff)) << u4_t( 0));
					create_texture_from_pixels(*as_engine, &dst, &value4, fmt, 1, 1);
					log.info(
						"Loaded {} texture as a single texel ({:02x}{:02x}{:02x}{:02x})",
						name,
						(value4 >> u4_t( 0)) & u4_t(0xff),
						(value4 >> u4_t( 8)) & u4_t(0xff),
						(value4 >> u4_t(16)) & u4_t(0xff),
						(value4 >> u4_t(24)) & u4_t(0xff) );
				} else {
					auto texture_file = h.getCstring(fma_value);
					auto res = create_texture_from_file(*as_engine, &dst, use_signed_fmt, texture_file);
					if(res.success) {
						log.info("Loaded {} texture from \"{}\" ({}x{})", name, texture_file, res.width, res.height);
					} else {
						dst = fallback;
						dst.is_copy = true;
						log.warn("Failed to load {} texture \"{}\", using fallback", name, texture_file);
					}
				}
			};
			#define LOAD_(T_, F_, S_) load_texture(r.texture_ ## T_, as_fallbackMaterial.texture_ ## T_, F_, S_, h.T_ ## Texture(), #T_);
			LOAD_(diffuse,  mf_ec::eDiffuseInlinePixel,  false)
			LOAD_(normal,   mf_ec::eNormalInlinePixel,   false)
			LOAD_(specular, mf_ec::eSpecularInlinePixel, false)
			LOAD_(emissive, mf_ec::eEmissiveInlinePixel, false)
			#undef LOAD_

			as_activeMaterials.insert(Materials::value_type(std::move(locator_s), r));
			double size_kib[4] = {
				double(texture_size_bytes(r.texture_diffuse))  / 1000.0,
				double(texture_size_bytes(r.texture_normal))   / 1000.0,
				double(texture_size_bytes(r.texture_specular)) / 1000.0,
				double(texture_size_bytes(r.texture_emissive)) / 1000.0 };
			log.info(
				"Loaded material \"{}\" ({:.3f} + {:.3f} + {:.3f} + {:.3f} = {:.3f} KiB)",
				locator,
				size_kib[0],  size_kib[1],  size_kib[2],  size_kib[3],
				size_kib[0] + size_kib[1] + size_kib[2] + size_kib[3] );
			return r;
		}
	}


	void AssetSupplier::msi_releaseMaterial(std::string_view locator) noexcept {
		std::string locator_s = std::string(locator);
		auto        existing  = as_activeMaterials.find(locator_s);
		auto        missing   = decltype(as_missingMaterials)::iterator();
		auto        dev       = as_engine->getDevice();
		auto        vma       = as_engine->getVmaAllocator();

		if(existing != as_activeMaterials.end()) {
			// Move to the inactive map
			as_inactiveMaterials.insert(*existing);
			as_activeMaterials.erase(existing);
			if(as_maxInactiveRatio < float(as_inactiveMaterials.size()) / float(as_activeMaterials.size())) {
				auto victim = as_inactiveMaterials.begin();
				destroy_material(dev, vma, victim->second);
				as_inactiveMaterials.erase(victim);
			}
			as_engine->logger().info("Released material \"{}\"", locator);
		}
		else if(as_missingMaterials.end() != (missing = as_missingMaterials.find(locator_s))) {
			as_engine->logger().trace("Releasing missing material \"{}\"", locator);
			as_missingMaterials.erase(missing);
		}
		else {
			as_engine->logger().debug("Tried to release material \"{}\", but it's not loaded", locator);
		}
	}


	void AssetSupplier::msi_releaseAllMaterials() noexcept {
		std::vector<std::string> queue;
		queue.reserve(as_activeMaterials.size());
		for(auto& mat : as_activeMaterials)  queue.push_back(mat.first);
		for(auto& mat : as_missingMaterials) queue.push_back(mat);
		for(auto& loc : queue)               msi_releaseMaterial(loc);
	}

}
