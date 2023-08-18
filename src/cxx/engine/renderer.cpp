#include "renderer.hpp"

#include "engine.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <utility>
#include <type_traits>
#include <random>

#include <glm/ext/matrix_transform.hpp>

#include <spdlog/spdlog.h>



namespace {

	constexpr size_t OBJECT_MAP_INITIAL_CAPACITY = 4;
	constexpr float  OBJECT_MAP_MAX_LOAD_FACTOR  = 4.0;
	constexpr size_t BATCH_MAP_INITIAL_CAPACITY = 16;
	constexpr float  BATCH_MAP_MAX_LOAD_FACTOR  = 2.0;


	template <VkBufferUsageFlags usage, vkutil::HostAccess host_access>
	vkutil::BufferDuplex createBuffer(
			VmaAllocator vma,
			size_t size
	) {
		vkutil::BufferCreateInfo bc_info = {
			.size  = size,
			.usage = usage,
			.qfamSharing = { } };
		return vkutil::BufferDuplex::createStorageBuffer(vma, bc_info, host_access);
	}


	vkutil::BufferDuplex createObjectBuffer(
			VmaAllocator vma,
			size_t count
	) {
		return
			createBuffer <
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				vkutil::HostAccess::eWr
			> (vma, count * sizeof(SKENGINE_NAME_NS_SHORT::dev::RenderObject));
	}


	vkutil::BufferDuplex createDrawCommandBuffer(
			VmaAllocator vma,
			size_t count
	) {
		return
			createBuffer <
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				vkutil::HostAccess::eWr
			> (vma, count * sizeof(VkDrawIndexedIndirectCommand));
	}


	template <typename T>
	concept ScopedEnum = requires(T t) {
		requires std::integral<std::underlying_type_t<T>>;
	};


	template <typename T>
	T generateId() {
		using int_t = std::underlying_type_t<T>;
		static std::atomic<int_t> last = 0;
		int_t r = last.fetch_add(1, std::memory_order_relaxed);
		return T(r);
	}


	auto find_mesh_slot(SKENGINE_NAME_NS_SHORT::Renderer::UnboundBatchMap& map, SKENGINE_NAME_NS_SHORT::MeshId mesh) {
		auto mesh_slot = map.find(mesh);
		assert(mesh_slot != map.end() /* Mesh (locator) must be registered inserted beforehand */);
		return mesh_slot;
	}

	auto find_mesh(SKENGINE_NAME_NS_SHORT::Renderer::MeshMap& map, SKENGINE_NAME_NS_SHORT::MeshId mesh) {
		auto mesh_slot = map.find(mesh);
		assert(mesh_slot != map.end() /* Mesh must exist */);
		return mesh_slot;
	}


	void commit_draw_batches(
			VkCommandBuffer                      cmd,
			const skengine::Renderer::BatchList& batches,
			vkutil::BufferDuplex&                buffer
	) {

	}

}



namespace SKENGINE_NAME_NS {

	Renderer Renderer::create(VmaAllocator vma, MeshSupplierInterface& msi) {
		VmaAllocatorInfo vma_info;
		vmaGetAllocatorInfo(vma, &vma_info);
		Renderer r;
		r.mDevice = vma_info.device;
		r.mVma    = vma;
		r.mMsi    = &msi;
		r.mObjectBuffer        = createObjectBuffer(vma, OBJECT_MAP_INITIAL_CAPACITY);
		r.mMeshes              = decltype(mMeshes)             (OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects             = decltype(mObjects)            (OBJECT_MAP_INITIAL_CAPACITY);
		r.mMeshLocators        = decltype(mMeshLocators)       (OBJECT_MAP_INITIAL_CAPACITY);
		r.mUnboundDrawBatches  = decltype(mUnboundDrawBatches) (OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects.           max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mUnboundDrawBatches.max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mObjectsOod = true;
		return r;
	}


	void Renderer::destroy(Renderer& r) {
		#ifndef NDEBUG
			assert(r.mDevice != nullptr);
		#endif

		r.clearObjects();
		vkutil::BufferDuplex::destroy(r.mVma, r.mObjectBuffer);

		#ifndef NDEBUG
			r.mDevice = nullptr;
		#endif
	}


	RenderObjectId Renderer::createObject(RenderObject o) {
		assert(mDevice != nullptr);

		auto new_id    = generateId<RenderObjectId>();
		auto mesh_slot = find_mesh_slot(mUnboundDrawBatches, o.mesh_id);

		auto iter = mesh_slot->second.find(o.material_id);
		if(iter == mesh_slot->second.end()) {
			iter = mesh_slot->second.insert(UnboundBatchMap::mapped_type::value_type {
				o.material_id,
				skengine::UnboundDrawBatch {
					.object_ids    = { new_id },
					.mesh_id       = o.mesh_id,
					.material_id   = o.material_id,
					.vertex_offset = iter->second.vertex_offset,
					.index_count   = iter->second.index_count,
					.first_index   = iter->second.first_index
				} }).first;
		} else {
			auto& batch = iter->second;
			batch.object_ids.insert(new_id);
			assert(batch.mesh_id     == o.mesh_id);
			assert(batch.material_id == o.material_id);
		}

		assert(! mObjects.contains(new_id));
		mObjects[new_id] = std::move(o);

		mObjectsOod = true;
		return new_id;
	}


	void Renderer::removeObject(RenderObjectId id) noexcept {
		assert(mDevice != nullptr);

		auto obj = mObjects.find(id);
		assert(obj != mObjects.end());

		auto mesh_slot = find_mesh_slot(mUnboundDrawBatches, obj->second.mesh_id);
		auto batch     = mesh_slot->second.find(obj->second.material_id);
		assert(batch != mesh_slot->second.end());
		bool erased = (0 < batch->second.object_ids.erase(id));
		assert(erased);

		if(batch->second.object_ids.empty()) [[unlikely]] {
			// No draws left for this mesh and material: erase the batch
			mesh_slot->second.erase(batch);
			if(mesh_slot->second.empty()) [[unlikely]] {
				// Mesh is now unused: erase it
				auto mesh = find_mesh(mMeshes, mesh_slot->first);
				std::string locator = mesh->second.locator; // A string here ensures no dangling string_view shenanigans happen
				mUnboundDrawBatches.erase(mesh_slot);
				mMeshLocators.erase(locator);

				// These two operations need to be the last, since they may
				// destroy strings that string_views point to
				mMsi->msi_releaseMesh(locator);
				mMeshes.erase(mesh);
			}
		}

		mObjectsOod = true;
	}


	void Renderer::clearObjects() noexcept {
		std::vector<RenderObjectId> ids;
		ids.reserve(mObjects.size());

		for(auto&          obj : mObjects) ids.push_back(obj.first);
		for(RenderObjectId id  : ids)      removeObject(id);
	}


	std::optional<const RenderObject*> Renderer::getObject(RenderObjectId id) const noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) return &found->second;
		return { };
	}


	std::optional<RenderObject*> Renderer::modifyObject(RenderObjectId id) noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) {
			mObjectsOod = true;
			return &found->second;
		}
		return { };
	}


	const DevMesh* Renderer::getMesh(MeshId id) const noexcept {
		auto found = mMeshes.find(id);
		if(found == mMeshes.end()) return nullptr;
		return &found->second.mesh;
	}


	MeshId Renderer::fetchMesh(std::string_view locator) {
		auto found_locator = mMeshLocators.find(locator);

		if(found_locator != mMeshLocators.end()) {
			return found_locator->second;
		} else {
			return setMesh(locator, mMsi->msi_requestMesh(locator));
		}
	}


	MeshId Renderer::setMesh(std::string_view locator, DevMesh mesh) {
		auto   found_locator = mMeshLocators.find(locator);
		MeshId id;

		// Generate a new ID, or remove the existing one and reassign it
		if(found_locator != mMeshLocators.end()) {
			spdlog::debug("Renderer: reassigning mesh \"{}\" with ID {}", locator, mesh_id_e(id));

			id = found_locator->second;

			std::string locator_s = std::string(locator); // Dangling `locator` string_view
			eraseMesh(found_locator->second);
			auto ins = mMeshes.insert(MeshMap::value_type(id, MeshData(mesh, locator_s)));
			locator = ins.first->second.locator; // Again, `locator` was dangling

			#warning "TODO: check if deprecating the objects is superfluous"
			mObjectsOod = false;
		} else {
			spdlog::trace("Renderer: associating mesh \"{}\" with ID {}", locator, mesh_id_e(id));
			id = generateId<MeshId>();
		}

		// Insert the mesh data (with the backing string) first
		auto  mesh_ins  = mMeshes.insert(MeshMap::value_type(id, { mesh, std::string(locator) }));
		auto& locator_r = mesh_ins.first->second.locator;
		mMeshLocators.insert(MeshLookup::value_type(locator_r, id));
		auto batch_ins = mUnboundDrawBatches.insert(UnboundBatchMap::value_type(id, UnboundBatchMap::mapped_type(BATCH_MAP_INITIAL_CAPACITY)));
		batch_ins.first->second.max_load_factor(BATCH_MAP_MAX_LOAD_FACTOR);

		return id;
	}


	void Renderer::eraseMesh(MeshId id) noexcept {
		auto mesh_data = mMeshes.find(id);
		assert(mesh_data != mMeshes.end());

		mMeshLocators.erase(mesh_data->second.locator);

		bool did_remove_objects = false;
		{
			std::vector<RenderObjectId> rm_objects;
			for(auto& obj : mObjects) {
				if(obj.second.mesh_id == id) {
					did_remove_objects = true;
					spdlog::warn(
						"Renderer: removing mesh \"{}\", still in use for object {}",
						mesh_data->second.locator,
						render_object_id_e(obj.first) );
					rm_objects.push_back(obj.first);
				}
			}
			for(auto obj : rm_objects) mObjects.erase(obj);
		}

		mUnboundDrawBatches.erase(id);
		mMsi->msi_releaseMesh(mesh_data->second.locator);
		if(! did_remove_objects) spdlog::trace("Renderer: removed mesh \"{}\"", mesh_data->second.locator);
		mMeshes.erase(id);
	}


	void Renderer::commitObjects(VkCommandBuffer cmd) {
		if(! mObjectsOod) return;

		std::size_t new_instance_count = [&]() {
			std::size_t i = 0;
			for(auto& mesh_batches : mUnboundDrawBatches)
			for(auto& batch        : mesh_batches.second) {
				i += batch.second.object_ids.size();
			}
			return i;
		} ();

		std::size_t    new_size   = new_instance_count * sizeof(dev::RenderObject);
		constexpr auto shrink_fac = OBJECT_MAP_INITIAL_CAPACITY;

		bool size_too_small = (new_size              < mObjectBuffer.size());
		bool size_too_big   = (new_size / shrink_fac > mObjectBuffer.size());
		if(size_too_small || size_too_big) {
			vkutil::BufferDuplex::destroy(mVma, mObjectBuffer);
			mObjectBuffer = createObjectBuffer(mVma, std::bit_ceil(new_instance_count));
		}

		std::mt19937 rng;
		auto         dist = std::uniform_real_distribution<float>(0.0f, 1.0f);
		auto* objects = mObjectBuffer.mappedPtr<dev::RenderObject>();
		for(size_t first_object = 0; auto& mesh_batches : mUnboundDrawBatches) {
			for(size_t object_offset = 0; auto& ubatch : mesh_batches.second) {
				for(auto obj_id : ubatch.second.object_ids) {
					rng.seed(render_object_id_e(obj_id));
					auto src_obj = mObjects.find(obj_id);
					if(src_obj == mObjects.end()) {
						spdlog::error("Trying to enqueue non-existent object {}", render_object_id_e(obj_id));
						continue;
					}
					auto& obj = objects[first_object + object_offset];
					auto& mat = obj.model_transf;
					obj.rnd       = dist(rng);
					obj.color_mul = src_obj->second.color_rgba;
					constexpr glm::vec3 x = { 1.0f, 0.0f, 0.0f };
					constexpr glm::vec3 y = { 0.0f, 1.0f, 0.0f };
					constexpr glm::vec3 z = { 0.0f, 0.0f, 1.0f };
					constexpr glm::mat4 identity = glm::mat4(1.0f);
					glm::mat4 translate = glm::translate(identity, src_obj->second.position_xyz);
					glm::mat4 rot0      = glm::rotate(identity, src_obj->second.direction_ypr.y, x);
					glm::mat4 rot1      = glm::rotate(identity, src_obj->second.direction_ypr.x, y);
					glm::mat4 rot2      = glm::rotate(identity, src_obj->second.direction_ypr.z, z);
					glm::mat4 scale     = glm::scale(identity, src_obj->second.scale_xyz);
					mat = translate * rot0 * rot1 * rot2 * scale;
					++ object_offset;
				}
				DrawBatch batch;
				batch.mesh_id        = ubatch.second.mesh_id;
				batch.material_id    = ubatch.second.material_id;
				batch.vertex_offset  = ubatch.second.vertex_offset;
				batch.first_index    = ubatch.second.first_index;
				batch.index_count    = ubatch.second.index_count;
				batch.first_instance = first_object;
				batch.instance_count = object_offset;
				mDrawBatchList.push_back(batch);
				first_object += object_offset;
			}
		}

		commit_draw_batches(cmd, mDrawBatchList, mBatchBuffer);
	}

}
