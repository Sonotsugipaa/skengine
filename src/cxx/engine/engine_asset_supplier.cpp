#include "engine.hpp"

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include <posixfio.hpp>

#include <vk-util/error.hpp>

#include <vulkan/vulkan_format_traits.hpp>



namespace SKENGINE_NAME_NS {

	// These functions are defined in a similarly named translation unit,
	// but not exposed through any header.
	void create_fallback_mat(Engine&, Material*);
	void destroy_material(VkDevice, VmaAllocator, Material&);



	AssetSupplier::AssetSupplier(Engine& e, std::string_view filename_prefix, float max_inactive_ratio):
			as_engine(&e),
			as_filenamePrefix(filename_prefix),
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
			MV_(as_filenamePrefix),
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
		auto vma = as_engine->getVmaAllocator();

		std::string locator_s;
		locator_s.reserve(as_filenamePrefix.size() + locator.size());
		locator_s.append(as_filenamePrefix);
		locator_s.append(locator);

		auto existing = as_activeModels.find(locator_s);
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
		auto vma = as_engine->getVmaAllocator();

		std::string locator_s;
		locator_s.reserve(as_filenamePrefix.size() + locator.size());
		locator_s.append(as_filenamePrefix);
		locator_s.append(locator);

		auto existing = as_activeModels.find(locator_s);
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

}
