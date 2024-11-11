#include "engine.hpp"

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include <vk-util/error.hpp>

#include <vulkan/vulkan_format_traits.hpp>



namespace SKENGINE_NAME_NS {

	// These functions are defined in a similarly named translation unit,
	// but not exposed through any header.
	void create_texture_from_pixels(const TransferContext&, Material::Texture*, const void*, float, VkFormat, size_t, size_t);
	bool create_texture_from_file(const TransferContext&, Material::Texture*, size_t*, size_t*, const char*, Logger&, float);
	size_t texture_size_bytes(const Material::Texture&);



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
		vkutil::BufferDuplex::destroy(vma, mat.mat_uniform);
	}


	void create_fallback_mat(const TransferContext& tc, Material* dst, float maxSamplerAnisotropy) {
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

		create_texture_from_pixels(tc, &dst->texture_diffuse,  texels_col, maxSamplerAnisotropy, VK_FORMAT_R8G8B8A8_UNORM, 2, 2);
		create_texture_from_pixels(tc, &dst->texture_normal,   texels_nrm, maxSamplerAnisotropy, VK_FORMAT_R8G8B8A8_UNORM, 3, 3);
		create_texture_from_pixels(tc, &dst->texture_specular, texels_spc, maxSamplerAnisotropy, VK_FORMAT_R8G8B8A8_UNORM, 1, 1);
		create_texture_from_pixels(tc, &dst->texture_emissive, texels_emi, maxSamplerAnisotropy, VK_FORMAT_R8G8B8A8_UNORM, 1, 1);

		{ // Create the material uniform buffer
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = sizeof(dev::MaterialUniform);
			bc_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			dst->mat_uniform = vkutil::BufferDuplex::createUniformBuffer(tc.vma, bc_info);
		}

		[&](dev::MaterialUniform& uni) {
			uni.shininess = 2.0f;
		} (* dst->mat_uniform.mappedPtr<dev::MaterialUniform>());
	}


	Material AssetSupplier::requestMaterial(std::string_view locator) {
		std::string locator_s = std::string(locator);

		auto existing = as_activeMaterials.find(locator_s);
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
			using mf_e  = fmamdl::material_flags_e;
			using mf_ec = fmamdl::MaterialFlags;

			Material r;

			auto src   = as_srcInterface->asi_requestMaterialData(locator);
			auto flags = std::byteswap(mf_e(src.fmaHeader.flags()));

			auto load_texture = [&](
					Material::Texture& dst,
					const Material::Texture& fallback,
					mf_ec        flag,
					fmamdl::u8_t fma_value,
					const char*  name
			) {
				if(flags & mf_e(flag)) {
					using fmamdl::u4_t;
					using fmamdl::u8_t;
					auto fmt    = VK_FORMAT_R8G8B8A8_UNORM;
					auto value4 = // Convert from P1111U1111 to U1111, since type punning is not a valid option here
						((u4_t(fma_value >> u8_t(24)) & u4_t(0xff)) << u4_t(24)) |
						((u4_t(fma_value >> u8_t(16)) & u4_t(0xff)) << u4_t(16)) |
						((u4_t(fma_value >> u8_t( 8)) & u4_t(0xff)) << u4_t( 8)) |
						((u4_t(fma_value >> u8_t( 0)) & u4_t(0xff)) << u4_t( 0));
					create_texture_from_pixels(as_transferContext, &dst, &value4, as_maxSamplerAnisotropy, fmt, 1, 1);
					as_logger.trace(
						"Loaded {} texture as a single texel ({:02x}{:02x}{:02x}{:02x})",
						name,
						(value4 >> u4_t( 0)) & u4_t(0xff),
						(value4 >> u4_t( 8)) & u4_t(0xff),
						(value4 >> u4_t(16)) & u4_t(0xff),
						(value4 >> u4_t(24)) & u4_t(0xff) );
				} else {
					auto texture_name = src.fmaHeader.getStringView(fma_value);
					std::string texture_filename;
					texture_filename.reserve(src.texturePathPrefix.size() + texture_name.size());
					texture_filename.append(src.texturePathPrefix);
					texture_filename.append(texture_name);
					size_t w;
					size_t h;
					auto success = create_texture_from_file(as_transferContext, &dst, &w, &h, texture_filename.c_str(), as_logger, as_maxSamplerAnisotropy);
					if(success) {
						as_logger.trace("Loaded {} texture from \"{}\" ({}x{})", name, texture_name, w, h);
					} else {
						dst = fallback;
						dst.is_copy = true;
						as_logger.warn("Failed to load {} texture \"{}\", using fallback", name, texture_name);
					}
				}
			};
			#define LOAD_(T_, F_) load_texture(r.texture_ ## T_, as_fallbackMaterial.texture_ ## T_, F_, src.fmaHeader.T_ ## Texture(), #T_);
			LOAD_(diffuse,  mf_ec::eDiffuseInlinePixel)
			LOAD_(normal,   mf_ec::eNormalInlinePixel)
			LOAD_(specular, mf_ec::eSpecularInlinePixel)
			LOAD_(emissive, mf_ec::eEmissiveInlinePixel)
			#undef LOAD_

			{ // Create the material uniform buffer
				vkutil::BufferCreateInfo bc_info = { };
				bc_info.size  = sizeof(dev::MaterialUniform);
				bc_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
				r.mat_uniform = vkutil::BufferDuplex::createUniformBuffer(vma(), bc_info);
			}

			[&](dev::MaterialUniform& uni) {
				uni.shininess = src.fmaHeader.specularExponent();
			} (* r.mat_uniform.mappedPtr<dev::MaterialUniform>());

			as_activeMaterials.insert(Materials::value_type(std::move(locator_s), r));
			double sizes_b[4] = {
				double(texture_size_bytes(r.texture_diffuse)),
				double(texture_size_bytes(r.texture_normal)),
				double(texture_size_bytes(r.texture_specular)),
				double(texture_size_bytes(r.texture_emissive)) };
			double size = (sizes_b[0] + sizes_b[1] + sizes_b[2] + sizes_b[3]) / 1024.0;
			std::string_view unit = "KiB";
			if(size > 5'000'000.0) [[likely]] {
				size /= 1024.0*1024.0; unit = "GiB";
				if(size > 5'000.0) [[unlikely]] { size /= 1024.0; unit = "TiB"; }
			}
			as_logger.info(
				"Loaded material \"{}\" ({:.3f} {})",
				locator,
				size, unit );
			return r;
		}
	}


	void AssetSupplier::releaseMaterial(std::string_view locator) noexcept {
		auto missing = decltype(as_missingMaterials)::iterator();
		auto vma     = this->vma();
		auto dev     = vmaGetAllocatorDevice(vma);

		std::string locator_s = std::string(locator);

		auto existing = as_activeMaterials.find(locator_s);
		if(existing != as_activeMaterials.end()) {
			// Move to the inactive map
			as_inactiveMaterials.insert(*existing);
			as_activeMaterials.erase(existing);
			if(as_maxInactiveRatio < float(as_inactiveMaterials.size()) / float(as_activeMaterials.size())) {
				auto victim = as_inactiveMaterials.begin();
				destroy_material(dev, vma, victim->second);
				as_inactiveMaterials.erase(victim);
			}
			as_logger.info("Released material \"{}\"", locator);
		}
		else if(as_missingMaterials.end() != (missing = as_missingMaterials.find(locator_s))) {
			as_logger.trace("Releasing missing material \"{}\"", locator);
			as_missingMaterials.erase(missing);
		}
		else {
			as_logger.debug("Tried to release material \"{}\", but it's not loaded", locator);
		}
	}


	void AssetSupplier::releaseAllMaterials() noexcept {
		std::vector<std::string> queue;
		queue.reserve(as_activeMaterials.size());
		for(auto& mat : as_activeMaterials)  queue.push_back(mat.first);
		for(auto& mat : as_missingMaterials) queue.push_back(mat);
		for(auto& loc : queue)               releaseMaterial(loc);
	}

}
