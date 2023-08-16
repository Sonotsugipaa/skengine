#include "renderer.hpp"

#include "engine.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <utility>
#include <type_traits>

#include <spdlog/spdlog.h>



namespace {

	constexpr size_t OBJECT_MAP_INITIAL_CAPACITY = 16;
	constexpr float  OBJECT_MAP_MAX_LOAD_FACTOR  = 4.0;
	constexpr size_t OBJECT_MAP_GROW_FACTOR      = 4; // Should be a power of 2
	constexpr size_t OBJECT_MAP_SHRINK_FACTOR    = 16; // Should be a power of 2
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


	void resizeBuffer(
			VmaAllocator vma,
			decltype(createObjectBuffer) createFn,
			vkutil::BufferDuplex& buffer,
			size_t                desired_obj_count
	) {
		desired_obj_count = std::bit_ceil(desired_obj_count);

		vkutil::BufferDuplex::destroy(vma, buffer);
		buffer = createFn(vma, desired_obj_count);
	}


	// Returns whether the buffers have been resized.
	bool guaranteeBufferSize(
			VmaAllocator vma,
			decltype(createObjectBuffer) createFn,
			vkutil::BufferDuplex& buffer,
			size_t                desired_size_bytes
	) {
		using namespace SKENGINE_NAME_NS_SHORT;

		size_t size_bytes = buffer.size();

		auto too_big_threshold = desired_size_bytes / OBJECT_MAP_SHRINK_FACTOR;
		bool too_small = (size_bytes < desired_size_bytes);
		bool too_big   = (size_bytes > desired_size_bytes) && (size_bytes >= too_big_threshold);
		spdlog::trace("Renderer buffer resize: {} < {} <= {}", too_big_threshold, size_bytes, desired_size_bytes);

		if(too_small) [[unlikely]] {
			// The required size may be larger than what the growth factor guesses
			auto size_bytes_grown = (size_bytes == 0)? 1 : size_bytes * OBJECT_MAP_GROW_FACTOR;
			desired_size_bytes = std::max(size_bytes_grown, desired_size_bytes);
			resizeBuffer(vma, createFn, buffer, desired_size_bytes);
			return true;
		} else
		if(too_big) [[unlikely]] {
			assert(too_big);
			resizeBuffer(vma, createFn, buffer, desired_size_bytes);
			return true;
		}

		return false;
	}


	using BatchMap = std::unordered_map<SKENGINE_NAME_NS_SHORT::MeshId, std::unordered_map<SKENGINE_NAME_NS_SHORT::MaterialId, SKENGINE_NAME_NS_SHORT::DrawBatch>>;
	using Msi      = SKENGINE_NAME_NS_SHORT::MeshSupplierInterface;


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


	auto find_mesh_slot(SKENGINE_NAME_NS_SHORT::Renderer::BatchMap& map, SKENGINE_NAME_NS_SHORT::MeshId mesh) {
		auto mesh_slot = map.find(mesh);
		assert(mesh_slot != map.end() /* Mesh (locator) must be registered inserted beforehand */);
		return mesh_slot;
	}

	auto find_mesh(SKENGINE_NAME_NS_SHORT::Renderer::MeshMap& map, SKENGINE_NAME_NS_SHORT::MeshId mesh) {
		auto mesh_slot = map.find(mesh);
		assert(mesh_slot != map.end() /* Mesh must exist */);
		return mesh_slot;
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
		r.mDrawCmdBuffer = createDrawCommandBuffer(vma, OBJECT_MAP_INITIAL_CAPACITY);
		r.mMeshes       = decltype(mMeshes)       (OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects      = decltype(mObjects)      (OBJECT_MAP_INITIAL_CAPACITY);
		r.mMeshLocators = decltype(mMeshLocators) (OBJECT_MAP_INITIAL_CAPACITY);
		r.mDrawBatches  = decltype(mDrawBatches)  (OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects.    max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mDrawBatches.max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mObjectsOod = true;

		return r;
	}


	void Renderer::destroy(Renderer& r) {
		#ifndef NDEBUG
			assert(r.mDevice != nullptr);
		#endif

		r.clearObjects();
		vkutil::BufferDuplex::destroy(r.mVma, r.mDrawCmdBuffer);

		#ifndef NDEBUG
			r.mDevice = nullptr;
		#endif
	}


	RenderObjectId Renderer::createObject(RenderObject o) {
		assert(mDevice != nullptr);

		auto new_id    = generateId<RenderObjectId>();
		auto mesh_slot = find_mesh_slot(mDrawBatches, o.mesh_id);

		auto inserted_o = mesh_slot->second.insert(BatchMap::mapped_type::value_type {
			o.material_id,
			{ new_id, o.mesh_id, o.material_id, 0 } });

		if(! inserted_o.second) {
			auto& batch = inserted_o.first->second;
			assert(batch.mesh_id     == o.mesh_id);
			assert(batch.material_id == o.material_id);
			++ batch.instance_count;
		}

		assert(! mObjects.contains(new_id));
		mObjects[new_id] = std::move(o);

		mObjectsOod = true;
	}


	void Renderer::removeObject(RenderObjectId id) noexcept {
		assert(mDevice != nullptr);

		auto obj = mObjects.find(id);
		assert(obj != mObjects.end());

		auto mesh_slot = find_mesh_slot(mDrawBatches, obj->second.mesh_id);
		auto batch     = mesh_slot->second.find(obj->second.material_id);
		assert(batch != mesh_slot->second.end());
		assert(batch->second.instance_count > 0);
		-- batch->second.instance_count;

		if(batch->second.instance_count == 0) [[unlikely]] {
			// No draws left for this mesh and material: erase the batch
			mesh_slot->second.erase(batch);
			if(mesh_slot->second.empty()) [[unlikely]] {
				// Mesh is now unused: erase it
				auto mesh = find_mesh(mMeshes, mesh_slot->first);
				std::string locator = mesh->second.locator; // A string here ensures no dangling string_view shenanigans happen
				mDrawBatches.erase(mesh_slot);
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
		auto batch_ins = mDrawBatches.insert(BatchMap::value_type(id, BatchMap::mapped_type(BATCH_MAP_INITIAL_CAPACITY)));
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

		mDrawBatches.erase(id);
		mMsi->msi_releaseMesh(mesh_data->second.locator);
		if(! did_remove_objects) spdlog::trace("Renderer: removed mesh \"{}\"", mesh_data->second.locator);
		mMeshes.erase(id);
	}

}
