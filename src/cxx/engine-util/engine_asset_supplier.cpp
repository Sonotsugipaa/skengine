#include <engine/engine.hpp>

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include "object_storage.hpp"

#include <vk-util/error.hpp>

#include <vulkan/vulkan_format_traits.hpp>



namespace SKENGINE_NAME_NS {

	// This function is defined in a similarly named translation unit,
	// but not exposed through any header.
	void destroy_material(VkDevice, VmaAllocator, Material&);



	AssetSupplier::AssetSupplier(Logger logger, std::shared_ptr<AssetCacheInterface> aci, float max_inactive_ratio):
			as_logger(std::move(logger)),
			as_cacheInterface(std::move(aci)),
			as_maxInactiveRatio(max_inactive_ratio),
			as_initialized(true),
			as_fallbackMaterialExists(false)
	{ }


	AssetSupplier::AssetSupplier(AssetSupplier&& mv):
			#define MV_(M_) M_(std::move(mv.M_))
			MV_(as_logger),
			MV_(as_cacheInterface),
			MV_(as_activeModels),
			MV_(as_inactiveModels),
			MV_(as_activeMaterials),
			MV_(as_inactiveMaterials),
			MV_(as_fallbackMaterial),
			MV_(as_missingMaterials),
			MV_(as_maxInactiveRatio),
			MV_(as_initialized),
			MV_(as_fallbackMaterialExists)
			#undef MV_
	{
		mv.as_initialized = false;
	}


	void AssetSupplier::destroy(TransferContext transfCtx) {
		assert(as_initialized);
		auto vma = transfCtx.vma;
		auto dev = vmaGetAllocatorDevice(vma);
		releaseAllModels(transfCtx);
		releaseAllMaterials(transfCtx);

		auto destroy_model = [&](Models::value_type& model) {
			vkutil::BufferDuplex::destroy(vma, model.second.indices);
			vkutil::BufferDuplex::destroy(vma, model.second.vertices);
		};

		for(auto& model : as_inactiveModels) destroy_model(model);
		as_inactiveModels.clear();

		for(auto& mat : as_inactiveMaterials) destroy_material(dev, vma, mat.second);
		as_inactiveMaterials.clear();
		if(as_fallbackMaterialExists) destroy_material(dev, vma, as_fallbackMaterial);

		as_initialized = false;
	}

	#ifndef NDEBUG
		AssetSupplier::~AssetSupplier() { assert(! as_initialized); }
	#endif


	DevModel AssetSupplier::requestModel(ModelId id, TransferContext transfCtx) {
		auto existing = as_activeModels.find(id);
		if(existing != as_activeModels.end()) {
			return existing->second;
		}
		else if((existing = as_inactiveModels.find(id)) != as_inactiveModels.end()) {
			auto ins = as_activeModels.insert(*existing);
			assert(ins.second);
			as_inactiveModels.erase(existing);
			return ins.first->second;
		}
		else {
			DevModel r;
			auto vma = transfCtx.vma;
			auto cache = as_cacheInterface->aci_requestModelData(id);
			auto materials = cache.fmaHeader.materials();
			auto meshes    = cache.fmaHeader.meshes();
			auto bones     = cache.fmaHeader.bones();
			auto faces     = cache.fmaHeader.faces();
			auto indices   = cache.fmaHeader.indices();
			auto vertices  = cache.fmaHeader.vertices();

			TransferCmdBarrier transfCmdBars[2];

			if(meshes.empty()) {
				as_logger.critical(
					"Attempting to load model {} without meshes; panicking",
					model_id_e(id) );
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
				transfCmdBars[0] = Engine::pushBufferAsync(transfCtx, r.indices);
				transfCmdBars[1] = Engine::pushBufferAsync(transfCtx, r.vertices);
			}

			std::vector<Bone> insBones;
			for(auto& bone : bones) {
				auto& mesh          = meshes[bone.meshIndex];
				auto& first_face    = faces[mesh.firstFace];
				auto  material_name = cache.fmaHeader.getStringView(materials[mesh.materialIndex].name);
				auto  material_id   = idgen::invalidId<MaterialId>();
				try {
					material_id = as_cacheInterface->aci_materialIdFromName(material_name); }
				catch(...) {
					as_logger.error("Failed to associate the name \"{}\" to a material ref", material_name); }
				auto ins = Bone {
					.mesh = Mesh {
						.index_count = uint32_t(mesh.indexCount),
						.first_index = uint32_t(first_face.firstIndex) },
					.material_id = material_id,
					.position_xyz  = { bone.relPosition[0], bone.relPosition[1], bone.relPosition[2] },
					.direction_ypr = { bone.relRotation[0], bone.relRotation[1], bone.relRotation[2] },
					.scale_xyz     = { bone.relScale   [0], bone.relScale   [1], bone.relScale   [2] } };
				insBones.push_back(std::move(ins));
			}

			r.bones.insert(r.bones.end(), insBones.begin(), insBones.end());

			as_activeModels.insert(Models::value_type(id, r));
			double size_kib = indices.size_bytes() + vertices.size_bytes();
			as_logger.trace("Loaded model {} ({:.3f} KiB)", model_id_e(id), size_kib / 1000.0);

			as_cacheInterface->aci_releaseModelData(id);
			return r;
		}
	}


	void AssetSupplier::releaseModel(ModelId id, TransferContext transfCtx) noexcept {
		auto vma = transfCtx.vma;

		auto existing = as_activeModels.find(id);
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
			as_logger.trace("Released model {}", model_id_e(id));
		} else {
			as_logger.warn("Tried to release model {}, but it's not loaded", model_id_e(id));
		}
	}


	void AssetSupplier::releaseAllModels(TransferContext transfCtx) noexcept {
		std::vector<ModelId> queue;
		queue.reserve(as_activeModels.size());
		for(auto& model : as_activeModels) queue.push_back(model.first);
		for(auto& id    : queue)           releaseModel(id, transfCtx);
	}

}
