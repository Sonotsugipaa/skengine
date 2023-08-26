#include "renderer.hpp"

#include "engine.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <utility>
#include <type_traits>
#include <random>

#include <glm/ext/matrix_transform.hpp>



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace {

	constexpr size_t OBJECT_MAP_INITIAL_CAPACITY    = 4;
	constexpr float  OBJECT_MAP_MAX_LOAD_FACTOR     = OBJECT_MAP_INITIAL_CAPACITY;
	constexpr size_t BATCH_MAP_INITIAL_CAPACITY     = 16;
	constexpr float  BATCH_MAP_MAX_LOAD_FACTOR      = 2.0;


	vkutil::BufferDuplex create_object_buffer(VmaAllocator vma, size_t count) {
		vkutil::BufferCreateInfo bc_info = { };
		bc_info.size  = std::bit_ceil(count) * sizeof(SKENGINE_NAME_NS_SHORT::dev::Instance);
		bc_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		vkutil::AllocationCreateInfo ac_info = { };
		ac_info.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		ac_info.vmaUsage          = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
		return vkutil::BufferDuplex::create(vma, bc_info, ac_info, vkutil::HostAccess::eWr);
	}


	vkutil::BufferDuplex create_draw_buffer(VmaAllocator vma, size_t count) {
		vkutil::BufferCreateInfo bc_info = { };
		bc_info.size  = std::bit_ceil(count) * sizeof(VkDrawIndexedIndirectCommand);
		bc_info.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		vkutil::AllocationCreateInfo ac_info = { };
		ac_info.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		ac_info.vmaUsage          = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
		return vkutil::BufferDuplex::create(vma, bc_info, ac_info, vkutil::HostAccess::eWr);
	}


	template <typename T>
	concept ScopedEnum = requires(T t) {
		requires std::integral<std::underlying_type_t<T>>;
	};


	template <typename T>
	T generate_id() {
		using int_t = std::underlying_type_t<T>;
		static std::atomic<int_t> last = 0;
		int_t r = last.fetch_add(1, std::memory_order_relaxed);
		return T(r);
	}


	void commit_draw_batches(
			VmaAllocator vma,
			VkCommandBuffer cmd,
			const SKENGINE_NAME_NS_SHORT::Renderer::BatchList& batches,
			vkutil::BufferDuplex& buffer
	) {
		using namespace SKENGINE_NAME_NS_SHORT;
		using namespace vkutil;

		if(batches.empty()) return;

		if(batches.size() * sizeof(DrawBatch) > buffer.size()) {
			BufferDuplex::destroy(vma, buffer);
			buffer = create_draw_buffer(vma, batches.size());
		}

		auto buffer_batches = buffer.mappedPtr<VkDrawIndexedIndirectCommand>();
		for(size_t i = 0; i < batches.size(); ++i) {
			auto& h_batch = batches[i];
			auto& b_batch = buffer_batches[i];
			b_batch.firstIndex    = h_batch.first_index;
			b_batch.firstInstance = h_batch.first_instance;
			b_batch.indexCount    = h_batch.index_count;
			b_batch.instanceCount = h_batch.instance_count;
			b_batch.vertexOffset  = h_batch.vertex_offset;
		}

		buffer.flush(cmd, vma);
	}


	void erase_objects_with_model(
			SKENGINE_NAME_NS_SHORT::Renderer::Objects& objects,
			spdlog::logger& log,
			SKENGINE_NAME_NS_SHORT::ModelId id,
			std::string_view locator
	) {
		using namespace SKENGINE_NAME_NS_SHORT;
		std::vector<ObjectId> rm_objects;
		for(auto& obj : objects) {
			if(obj.second.first.model_id == id) [[unlikely]] {
				log.warn(
					"Renderer: removing model \"{}\", still in use for object {}",
					locator,
					object_id_e(obj.first) );
				rm_objects.push_back(obj.first);
			}
		}
		for(auto obj : rm_objects) objects.erase(obj);
	}

}



namespace SKENGINE_NAME_NS {

	Renderer Renderer::create(
			std::shared_ptr<spdlog::logger> logger,
			VmaAllocator vma,
			std::string_view filename_prefix,
			ModelSupplierInterface&    mdl_si,
			MaterialSupplierInterface& mat_si
	) {
		VmaAllocatorInfo vma_info;
		vmaGetAllocatorInfo(vma, &vma_info);
		Renderer r;
		r.mDevice = vma_info.device;
		r.mVma    = vma;
		r.mLogger = std::move(logger);
		r.mModelSupplier    = &mdl_si;
		r.mMaterialSupplier = &mat_si;
		r.mObjects             = decltype(mObjects)            (OBJECT_MAP_INITIAL_CAPACITY);
		r.mModelLocators       = decltype(mModelLocators)      (OBJECT_MAP_INITIAL_CAPACITY);
		r.mUnboundDrawBatches  = decltype(mUnboundDrawBatches) (OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects.           max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mUnboundDrawBatches.max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mObjectsOod = true;
		r.mObjectBuffer = create_object_buffer(r.mVma, OBJECT_MAP_INITIAL_CAPACITY);
		r.mBatchBuffer  = create_draw_buffer(r.mVma, BATCH_MAP_INITIAL_CAPACITY);
		r.mFilenamePrefix = std::string(filename_prefix);
		return r;
	}


	void Renderer::destroy(Renderer& r) {
		#ifndef NDEBUG
			assert(r.mDevice != nullptr);
		#endif

		r.clearObjects();
		vkutil::BufferDuplex::destroy(r.mVma, r.mBatchBuffer);
		vkutil::BufferDuplex::destroy(r.mVma, r.mObjectBuffer);

		#ifndef NDEBUG
			r.mDevice = nullptr;
		#endif
	}


	ObjectId Renderer::createObject(const NewObject& ins) {
		assert(mDevice != nullptr);

		auto new_obj_id = generate_id<ObjectId>();
		auto model_id   = getModelId(ins.model_locator);
		auto model      = assert_not_nullptr_(getModel(model_id));

		++ mModelDepCounters[model_id];

		Object new_obj = {
			.model_id = model_id,
			.position_xyz  = ins.position_xyz,
			.direction_ypr = ins.direction_ypr,
			.scale_xyz     = ins.scale_xyz };

		std::vector<BoneInstance> bone_instances;
		bone_instances.reserve(model->bones.size());

		for(bone_id_e i = 0; auto& bone : model->bones) {
			auto material_id = getMaterialId(bone.material);

			bone_instances.push_back(BoneInstance {
				.model_id    = model_id,
				.material_id = material_id,
				.object_id   = new_obj_id,
				.color_rgba    = { 1.0f, 1.0f, 1.0f, 1.0f },
				.position_xyz  = { 0.0f, 0.0f, 0.0f },
				.direction_ypr = { 0.0f, 0.0f, 0.0f },
				.scale_xyz     = { 1.0f, 1.0f, 1.0f } });

			auto& model_slot          = assert_not_end_(mUnboundDrawBatches, model_id)->second;
			auto& bone_slot           = assert_not_end_(model_slot, i)->second;
			auto  material_batch_iter = bone_slot.find(material_id);
			if(material_batch_iter == bone_slot.end()) {
				auto ins = UnboundDrawBatch {
					.object_refs = { new_obj_id },
					.material_id = material_id,
					.model_bone_index = i };
				bone_slot.insert(UnboundBatchMap::mapped_type::mapped_type::value_type(material_id, ins));
			} else {
				auto& batch = material_batch_iter->second;
				batch.object_refs.push_back(new_obj_id);
				assert(batch.material_id == material_id);
			}

			++ i;
		}

		assert(! mObjects.contains(new_obj_id));
		mObjects[new_obj_id] = Objects::mapped_type(std::move(new_obj), std::move(bone_instances));
		mObjectsOod = true;
		return new_obj_id;
	}


	void Renderer::removeObject(ObjectId id) noexcept {
		assert(mDevice != nullptr);

		auto  obj_iter = assert_not_end_(mObjects, id);
		auto  obj      = std::move(obj_iter->second);
		auto& model    = assert_not_end_(mModels, obj.first.model_id)->second;

		auto model_dep_counter_iter = assert_not_end_(mModelDepCounters, obj.first.model_id);
		assert(model_dep_counter_iter->second > 0);
		-- model_dep_counter_iter->second;
		mObjects.erase(obj_iter);

		if(model_dep_counter_iter->second == 0) {
			mModelDepCounters.erase(model_dep_counter_iter);
			assert([&]() {
				// Check whether there is more than one set of batches for this model
				for(auto& bone  : assert_not_end_(mUnboundDrawBatches, obj.first.model_id)->second)
				for(auto& batch : bone.second) {
					if(batch.second.object_refs.size() != 1) return false; }
				return true;
			} ());
			eraseModelNoObjectCheck(obj.first.model_id, model);
		} else {
			// Remove the references from the unbound draw batches
			assert(obj.second.size() == model.bones.size() /* The Nth object bone instance refers to the model's Nth bone */);
			for(bone_id_e i = 0; auto& bone : model.bones) {
				auto& material_id     = assert_not_end_(mMaterialLocators, bone.material)->second;
				auto  model_slot_iter = assert_not_end_(mUnboundDrawBatches, obj.first.model_id);
				auto  bone_iter       = assert_not_end_(model_slot_iter->second, i);
				auto  batch_iter      = assert_not_end_(bone_iter->second, material_id);
				auto& obj_refs        = batch_iter->second.object_refs;
				[[maybe_unused]]
				std::size_t erased = std::erase(obj_refs, id);
				assert(erased == 1);
				assert(! obj_refs.empty() /* The model still has objects refering to it, so it *must* have at least one object for this material */);
				++ i;
			}
		}

		mObjectsOod = true;
	}


	void Renderer::clearObjects() noexcept {
		std::vector<ObjectId> ids;
		ids.reserve(mObjects.size());

		for(auto&    obj : mObjects) ids.push_back(obj.first);
		for(ObjectId id  : ids)      removeObject(id);
	}


	std::optional<const Object*> Renderer::getObject(ObjectId id) const noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) return &found->second.first;
		return { };
	}


	std::optional<Renderer::ModifiableObject> Renderer::modifyObject(ObjectId id) noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) {
			mObjectsOod = true;
			return ModifiableObject {
				.position_xyz  = found->second.first.position_xyz,
				.direction_ypr = found->second.first.direction_ypr,
				.scale_xyz     = found->second.first.scale_xyz };
		}
		return { };
	}


	ModelId Renderer::getModelId(std::string_view locator) {
		auto found_locator = mModelLocators.find(locator);

		if(found_locator != mModelLocators.end()) {
			return found_locator->second;
		} else {
			std::string filename;
			filename.reserve(mFilenamePrefix.size() + locator.size());
			filename.append(mFilenamePrefix);
			filename.append(locator);
			auto r = setModel(locator, mModelSupplier->msi_requestModel(filename));
			return r;
		}
	}


	const Renderer::ModelData* Renderer::getModel(ModelId id) const noexcept {
		auto found = mModels.find(id);
		if(found == mModels.end()) return nullptr;
		return &found->second;
	}


	ModelId Renderer::setModel(std::string_view locator, DevModel model) {
		auto    found_locator = mModelLocators.find(locator);
		ModelId id;

		// Generate a new ID, or remove the existing one and reassign it
		if(found_locator != mModelLocators.end()) { // Remove the existing ID and reassign it
			id = found_locator->second;
			mLogger->debug("Renderer: reassigning model \"{}\" with ID {}", locator, model_id_e(id));

			std::string locator_s = std::string(locator); // Dangling `locator` string_view
			eraseModel(found_locator->second);
			auto ins = mModels.insert_or_assign(id, ModelData(model, locator_s));
			locator = ins.first->second.locator; // Again, `locator` was dangling
		} else {
			id = generate_id<ModelId>();
			mLogger->trace("Renderer: associating model \"{}\" with ID {}", locator, model_id_e(id));
		}

		// Insert the model data (with the backing string) first
		auto  model_ins = mModels.insert(ModelMap::value_type(id, { model, std::string(locator) }));
		auto& locator_r = model_ins.first->second.locator;
		mModelLocators.insert(ModelLookup::value_type(locator_r, id));
		auto batch_ins = mUnboundDrawBatches.insert(UnboundBatchMap::value_type(id, UnboundBatchMap::mapped_type(BATCH_MAP_INITIAL_CAPACITY)));
		batch_ins.first->second.max_load_factor(BATCH_MAP_MAX_LOAD_FACTOR);

		auto& bones = model_ins.first->second.bones;
		for(bone_id_e i = 0; i < bones.size(); ++i) {
			batch_ins.first->second.insert(UnboundBatchMap::mapped_type::value_type(i, UnboundBatchMap::mapped_type::mapped_type()));
		}

		return id;
	}


	void Renderer::eraseModel(ModelId id) noexcept {
		auto& model_data = assert_not_end_(mModels, id)->second;

		erase_objects_with_model(mObjects, *mLogger, id, model_data.locator);

		eraseModelNoObjectCheck(id, model_data);
	}


	void Renderer::eraseModelNoObjectCheck(ModelId id, ModelData& model_data) noexcept {
		// Move the string out of the models storage, we'll need it later
		auto model_locator = std::move(model_data.locator);

		{ // Seek materials to release
			auto candidates = std::unordered_map<MaterialId, std::string_view>(model_data.bones.size());
			for(auto& bone : model_data.bones) {
				auto material_id = assert_not_end_(mMaterialLocators, bone.material)->second;
				candidates.insert(decltype(candidates)::value_type(material_id, std::string_view(bone.material)));
			}

			std::vector<decltype(candidates)::value_type> erase_queue;
			erase_queue.reserve(candidates.size());

			auto check_material = [&](const decltype(candidates)::value_type& candidate) {
				for(auto& model : mModels)
				if(model.first != id) [[likely]]
				for(auto& bone : model.second.bones) {
					if(bone.material == candidate.second) return;
				}
				erase_queue.push_back(candidate);
			};

			for(auto& candidate : candidates) check_material(candidate);
			candidates = { };

			for(auto& material : erase_queue) {
				mLogger->trace("Renderer: removed unused material {}, \"{}\"", material_id_e(material.first), material.second);
				eraseMaterial(material.first);
			}
		}

		std::string mdl_filename;
		mdl_filename.reserve(mFilenamePrefix.size() + model_locator.size());
		mdl_filename.append(mFilenamePrefix);
		mdl_filename.append(model_locator);

		mModelLocators.erase(model_locator);

		mUnboundDrawBatches.erase(id);
		mModelSupplier->msi_releaseModel(mdl_filename);
		mLogger->trace("Renderer: removed model \"{}\"", model_locator);
		mModels.erase(id); // Moving this line upward has already caused me some dangling string problems, I'll just leave this warning here
	}


	MaterialId Renderer::getMaterialId(std::string_view locator) {
		auto found_locator = mMaterialLocators.find(locator);

		if(found_locator != mMaterialLocators.end()) {
			return found_locator->second;
		} else {
			std::string name;
			name.reserve(mFilenamePrefix.size() + locator.size());
			name.append(mFilenamePrefix);
			name.append(locator);
			return setMaterial(locator, mMaterialSupplier->msi_requestMaterial(name));
		}
	}


	const Material* Renderer::getMaterial(MaterialId id) const noexcept {
		auto found = mMaterials.find(id);
		if(found == mMaterials.end()) return nullptr;
		return &found->second;
	}


	MaterialId Renderer::setMaterial(std::string_view locator, Material material) {
		auto found_locator = mMaterialLocators.find(locator);
		MaterialId id;

		// Generate a new ID, or remove the existing one and reassign it
		if(found_locator != mMaterialLocators.end()) {
			id = found_locator->second;
			mLogger->debug("Renderer: reassigning material \"{}\" with ID {}", locator, material_id_e(id));

			std::string locator_s = std::string(locator); // Dangling `locator` string_view
			eraseMaterial(found_locator->second);
			auto ins = mMaterials.insert(MaterialMap::value_type(id, MaterialData(material, locator_s)));
			locator = ins.first->second.locator; // Again, `locator` was dangling
		} else {
			id = generate_id<MaterialId>();
			mLogger->trace("Renderer: associating material \"{}\" with ID {}", locator, material_id_e(id));
		}

		// Insert the material data (with the backing string) first
		auto  material_ins = mMaterials.insert(MaterialMap::value_type(id, { material, std::string(locator) }));
		auto& locator_r    = material_ins.first->second.locator;
		mMaterialLocators.insert(MaterialLookup::value_type(locator_r, id));

		return id;
	}


	void Renderer::eraseMaterial(MaterialId id) noexcept {
		auto& mat_data = assert_not_end_(mMaterials, id)->second;

		#ifndef NDEBUG
		// Assert that no object is using the material: this function is only called internally when this is the case
		for(auto& obj  : mObjects)
		for(auto& bone : obj.second.second) {
			assert(bone.material_id != id);
		}
		#endif

		mMaterialLocators.erase(mat_data.locator);

		std::string mat_filename;
		mat_filename.reserve(mFilenamePrefix.size() + mat_data.locator.size());
		mat_filename.append(mFilenamePrefix);
		mat_filename.append(mat_data.locator);
		mMaterialSupplier->msi_releaseMaterial(mat_filename);
		mMaterials.erase(id);
	}


	bool Renderer::commitObjects(VkCommandBuffer cmd) {
		if(! mObjectsOod) return false;

		std::size_t new_instance_count = [&]() {
			std::size_t i = 0;
			for(auto& model_batches    : mUnboundDrawBatches)
			for(auto& bone_batches     : model_batches.second)
			for(auto& material_batches : bone_batches.second) {
				i += material_batches.second.object_refs.size();
			}
			return i;
		} ();

		std::size_t    new_size   = new_instance_count * sizeof(dev::Instance);
		constexpr auto shrink_fac = OBJECT_MAP_INITIAL_CAPACITY;

		{ // Ensure the object buffer is big enough
			bool size_too_small = (new_size > mObjectBuffer.size());
			bool size_too_big   = (new_size < mObjectBuffer.size() / shrink_fac);
			if(size_too_small || size_too_big) {
				vkutil::BufferDuplex::destroy(mVma, mObjectBuffer);
				mObjectBuffer = create_object_buffer(mVma, std::bit_ceil(new_instance_count));
			}
		}

		std::mt19937 rng;
		auto         dist = std::uniform_real_distribution<float>(0.0f, 1.0f);
		auto* objects = mObjectBuffer.mappedPtr<dev::Instance>();
		mDrawBatchList.clear();

		// Returns the number of objects set
		auto set_objects = [&](UnboundDrawBatch& ubatch, Bone& bone, uint32_t first_object) {
			uint32_t object_offset = 0;
			for(auto obj_ref : ubatch.object_refs) { // Update/set the instances, while indirectly sorting the buffer
				auto src_obj_iter = mObjects.find(obj_ref);
				if(src_obj_iter == mObjects.end()) {
					mLogger->error("Renderer: trying to enqueue non-existent object {}", object_id_e(obj_ref));
					continue;
				}
				auto& src_obj = src_obj_iter->second;

				auto& bone_instance = src_obj.second[ubatch.model_bone_index];
				constexpr auto bone_id_digits = std::numeric_limits<bone_id_e>::digits;
				rng.seed(object_id_e(obj_ref) ^ std::rotl(ubatch.model_bone_index, bone_id_digits / 2));

				auto& obj = objects[first_object + object_offset];
				auto& mat = obj.model_transf;
				obj.rnd       = dist(rng);
				obj.color_mul = bone_instance.color_rgba;
				constexpr glm::vec3 x = { 1.0f, 0.0f, 0.0f };
				constexpr glm::vec3 y = { 0.0f, 1.0f, 0.0f };
				constexpr glm::vec3 z = { 0.0f, 0.0f, 1.0f };
				constexpr glm::mat4 identity = glm::mat4(1.0f);
				auto mk_transf = [&](const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& scl) {
					glm::mat4 translate = glm::translate (identity, pos);
					glm::mat4 rot0      = glm::rotate    (identity, dir.y, x);
					glm::mat4 rot1      = glm::rotate    (identity, dir.x, y);
					glm::mat4 rot2      = glm::rotate    (identity, dir.z, z);
					glm::mat4 scale     = glm::scale     (identity, scl);
					return translate * rot0 * rot1 * rot2 * scale;
				};
				auto mat0 = mk_transf(src_obj.first.position_xyz, src_obj.first.direction_ypr, src_obj.first.scale_xyz);
				auto mat1 = mk_transf(bone.position_xyz,          bone.direction_ypr,          bone.scale_xyz);
				auto mat2 = mk_transf(bone_instance.position_xyz, bone_instance.direction_ypr, bone_instance.scale_xyz);
				mat = mat2 * mat1 * mat0;
				++ object_offset;
			}

			return object_offset;
		};

		for(uint32_t first_object = 0; auto& model_batches : mUnboundDrawBatches) {
			auto& model = assert_not_end_(mModels, model_batches.first)->second;
			for(auto& bone_batches : model_batches.second) {
				auto& bone = model.bones[bone_batches.first];
				for(auto& ubatch : bone_batches.second) { // Create the (bound) draw batch
					auto object_set_count = set_objects(ubatch.second, bone, first_object);
					mDrawBatchList.push_back(DrawBatch {
						.model_id       = model_batches.first,
						.material_id    = ubatch.first,
						.vertex_offset  = 0,
						.index_count    = bone.mesh.index_count,
						.first_index    = bone.mesh.first_index,
						.instance_count = object_set_count,
						.first_instance = first_object });
					first_object += mDrawBatchList.back().instance_count;
				}
			}
		}

		mObjectBuffer.flush(cmd, mVma);
		commit_draw_batches(mVma, cmd, mDrawBatchList, mBatchBuffer);

		return true;
	}

}



#undef assert_not_end_
#undef assert_not_nullptr_
