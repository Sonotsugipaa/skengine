#include "object_storage.hpp"

#include <engine/engine.hpp>
#include <engine/debug.inl.hpp>

#include "world_renderer.hpp"

#include <random>

#include <vk-util/error.hpp>

#include "atomic_id_gen.inl.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <sys-resources.hpp>



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace SKENGINE_NAME_NS {

	namespace {

		constexpr size_t OBJECT_MAP_INITIAL_CAPACITY_KB = 32;
		constexpr float  OBJECT_MAP_MAX_LOAD_FACTOR     = 2;
		constexpr size_t BATCH_MAP_INITIAL_CAPACITY_KB  = 16;
		constexpr float  BATCH_MAP_MAX_LOAD_FACTOR      = 0.8;
		constexpr size_t UNBOUND_BATCH_LEVEL_1_INIT_CAP = 16;
		constexpr float  UNBOUND_BATCH_LEVEL_1_LOAD_FAC = 0.8;
		constexpr size_t UNBOUND_BATCH_LEVEL_2_INIT_CAP = 2;
		constexpr float  UNBOUND_BATCH_LEVEL_2_LOAD_FAC = 4.0;
		constexpr float  UNBOUND_DRAW_BATCH_LOAD_FAC    = 8.0;


		[[nodiscard]]
		size_t reserve_mat_dpool(
				VkDevice dev,
				VkDescriptorPool* dst,
				size_t            req_cap,
				size_t            cur_cap
		) {
			assert(req_cap > 0);

			req_cap = std::bit_ceil(req_cap);

			if(req_cap != cur_cap) {
				if(cur_cap > 0) {
					assert(*dst != nullptr);
					vkDestroyDescriptorPool(dev, *dst, nullptr);
				}
				VkDescriptorPoolSize sizes[] = {
					{
						.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
						.descriptorCount = uint32_t(4 * req_cap) },
					{
						.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
						.descriptorCount = uint32_t(1 * req_cap) } };
				VkDescriptorPoolCreateInfo dpc_info = { };
				dpc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
				dpc_info.maxSets = req_cap;
				dpc_info.flags   = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
				dpc_info.poolSizeCount = std::size(sizes);
				dpc_info.pPoolSizes    = sizes;
				VK_CHECK(vkCreateDescriptorPool, dev, &dpc_info, nullptr, dst);
				return req_cap;
			}

			return cur_cap;
		}


		void update_mat_dset(
				VkDevice dev, VkDescriptorPool dpool, VkDescriptorSetLayout layout,
				bool do_allocate,
				ObjectStorage::MaterialData* mat
		) {
			if(do_allocate) { // Create the dset
				VkDescriptorSetAllocateInfo dsa_info = { };
				dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				dsa_info.descriptorPool = dpool;
				dsa_info.descriptorSetCount = 1;
				dsa_info.pSetLayouts        = &layout;
				VK_CHECK(vkAllocateDescriptorSets, dev, &dsa_info, &mat->dset);
			}

			{ // Update it
				constexpr auto& DFS = WorldRenderer::RDR_DIFFUSE_TEX_BINDING;
				constexpr auto& NRM = WorldRenderer::RDR_NORMAL_TEX_BINDING;
				constexpr auto& SPC = WorldRenderer::RDR_SPECULAR_TEX_BINDING;
				constexpr auto& EMI = WorldRenderer::RDR_EMISSIVE_TEX_BINDING;
				constexpr auto& UBO = WorldRenderer::RDR_MATERIAL_UBO_BINDING;
				VkDescriptorImageInfo di_info[4] = { };
				di_info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				di_info[0].sampler   = mat->texture_diffuse.sampler;
				di_info[0].imageView = mat->texture_diffuse.image_view;
				di_info[1] = di_info[0];
				di_info[1].sampler   = mat->texture_normal.sampler;
				di_info[1].imageView = mat->texture_normal.image_view;
				di_info[2] = di_info[0];
				di_info[2].sampler   = mat->texture_specular.sampler;
				di_info[2].imageView = mat->texture_specular.image_view;
				di_info[3] = di_info[0];
				di_info[3].sampler   = mat->texture_emissive.sampler;
				di_info[3].imageView = mat->texture_emissive.image_view;
				VkDescriptorBufferInfo db_info[1] = { };
				db_info[0].buffer = mat->mat_uniform;
				db_info[0].range  = sizeof(dev::MaterialUniform);
				VkWriteDescriptorSet wr[5] = { };
				wr[DFS].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				wr[DFS].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				wr[DFS].dstSet          = mat->dset;
				wr[DFS].descriptorCount = 1;
				wr[DFS].dstBinding      = DFS;
				wr[DFS].pImageInfo      = di_info + DFS;
				wr[NRM] = wr[DFS];
				wr[NRM].dstBinding = NRM;
				wr[NRM].pImageInfo = di_info + NRM;
				wr[SPC] = wr[DFS];
				wr[SPC].dstBinding = SPC;
				wr[SPC].pImageInfo = di_info + SPC;
				wr[EMI] = wr[DFS];
				wr[EMI].dstBinding = EMI;
				wr[EMI].pImageInfo = di_info + EMI;
				wr[UBO].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				wr[UBO].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				wr[UBO].dstSet          = mat->dset;
				wr[UBO].descriptorCount = 1;
				wr[UBO].dstBinding      = UBO;
				wr[UBO].pBufferInfo     = db_info;
				vkUpdateDescriptorSets(dev, std::size(wr), wr, 0, nullptr);
			}
		}


		void create_mat_dset(
				VkDevice dev,
				VkDescriptorPool*      dpool,
				VkDescriptorSetLayout  layout,
				size_t* size,
				size_t* capacity,
				ObjectStorage::MaterialMap& materials,
				ObjectStorage::MaterialData* dst
		) {
			++ *size;
			size_t new_cap  = reserve_mat_dpool(dev, dpool, *size, *capacity);
			if(new_cap != *capacity) { // All existing dsets need to be recreated
				*capacity = new_cap;
				for(auto& mat : materials) update_mat_dset(dev, *dpool, layout, true, &mat.second);
			} else {
				assert(*size <= *capacity);
				update_mat_dset(dev, *dpool, layout, true, dst);
			}
		}


		std::pair<vkutil::Buffer, size_t> create_object_buffer(VmaAllocator vma, size_t count) {
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = std::bit_ceil(count) * sizeof(dev::Object);
			bc_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			vkutil::AllocationCreateInfo ac_info = { };
			ac_info.requiredMemFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			ac_info.vmaUsage         = vkutil::VmaAutoMemoryUsage::eAutoPreferHost;
			ac_info.vmaFlags         = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			auto r = vkutil::Buffer::create(vma, bc_info, ac_info);
			debug::createdBuffer(r, "object instances");
			return { std::move(r), bc_info.size };
		}


		std::pair<vkutil::Buffer, size_t> create_draw_cmd_template_buffer(VmaAllocator vma, size_t count) {
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = std::bit_ceil(count) * sizeof(VkDrawIndexedIndirectCommand);
			bc_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			vkutil::AllocationCreateInfo ac_info = { };
			ac_info.requiredMemFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			ac_info.vmaUsage         = vkutil::VmaAutoMemoryUsage::eAutoPreferHost;
			ac_info.vmaFlags         = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			auto r = vkutil::Buffer::create(vma, bc_info, ac_info);
			debug::createdBuffer(r, "indirect draw commands");
			return { std::move(r), bc_info.size };
		}


		void commit_draw_batches(
				VmaAllocator vma,
				const ObjectStorage::BatchList& batches,
				std::pair<vkutil::Buffer, size_t>& buffer
		) {
			using namespace vkutil;

			if(batches.empty()) return;

			if(batches.size() > buffer.second) {
				Buffer::destroy(vma, buffer.first);
				debug::destroyedBuffer(buffer.first, "indirect draw commands");
				buffer = create_draw_cmd_template_buffer(vma, batches.size());
			}

			auto* buffer_batches = buffer.first.map<VkDrawIndexedIndirectCommand>(vma);
			for(size_t i = 0; i < batches.size(); ++i) {
				auto& h_batch = batches[i];
				auto& b_batch = buffer_batches[i];
				b_batch.firstIndex    = h_batch.first_index;
				b_batch.firstInstance = h_batch.first_instance;
				b_batch.indexCount    = h_batch.index_count;
				b_batch.instanceCount = 0;
				b_batch.vertexOffset  = h_batch.vertex_offset;
			}
			buffer.first.unmap(vma);
		}


		bool erase_objects_with_model(
				ObjectStorage::Objects& objects,
				ObjectStorage::ObjectUpdates& object_updates,
				Logger& log,
				ModelId id
		) {
			std::vector<ObjectId> rm_objects;
			for(auto& obj : objects) {
				if(obj.second.first.model_id == id) [[unlikely]] {
					// This is not an error, but ideally this shouldn't happen
					log.warn(
						"Renderer: removing model {}, still in use for object {}",
						model_id_e(id),
						object_id_e(obj.first) );
					rm_objects.push_back(obj.first);
				}
			}
			for(auto obj : rm_objects) {
				objects.erase(obj);
				object_updates.erase(obj);
			}
			return ! rm_objects.empty();
		}


		void matrix_worker_fn(ObjectStorage::MatrixAssembler* ma) {
			auto lock = std::unique_lock(ma->mutex);

			begin:
			if(ma->queue.empty()) [[unlikely]] {
				lock.unlock();
				return;
			}

			for(auto& job : ma->queue) {
				static constexpr glm::vec3 x = { 1.0f, 0.0f, 0.0f };
				static constexpr glm::vec3 y = { 0.0f, 1.0f, 0.0f };
				static constexpr glm::vec3 z = { 0.0f, 0.0f, 1.0f };
				constexpr glm::mat4 identity = glm::mat4(1.0f);
				constexpr auto rotate = [](glm::mat4* dst, const glm::vec3& dir) {
					*dst = glm::rotate(*dst, dir.y, x);
					*dst = glm::rotate(*dst, dir.x, y);
					*dst = glm::rotate(*dst, dir.z, z);
				};
				constexpr auto translate = [](glm::mat4* dst, const glm::vec3& pos) {
					*dst = glm::translate(*dst, pos);
				};
				constexpr auto scale = [](glm::mat4* dst, const glm::vec3& scl) {
					*dst = glm::scale(*dst, scl);
				};
				auto model_transf = identity;
				translate (&model_transf, job.position.object);
				translate (&model_transf, job.position.bone);
				translate (&model_transf, job.position.bone_instance);
				auto scale_mat = identity;
				rotate    (&scale_mat, job.direction.object);
				rotate    (&scale_mat, job.direction.bone);
				rotate    (&scale_mat, job.direction.bone_instance);
				scale     (&scale_mat, job.scale.object);
				scale     (&scale_mat, job.scale.bone);
				scale     (&scale_mat, job.scale.bone_instance);
				model_transf = model_transf * scale_mat;
				auto cull_sphere = glm::vec4(glm::vec3(job.mesh.cull_sphere), 1.0);
				auto scaled_cube = glm::vec3(glm::vec4(1.0, 1.0, 1.0, 1.0) * scale_mat);
				cull_sphere = model_transf * cull_sphere;
				cull_sphere.w = job.mesh.cull_sphere.w * std::max({ scaled_cube.x, scaled_cube.y, scaled_cube.z });
				assert(cull_sphere.w >= 0.0);
				*job.dst.model_transf = model_transf;
				*job.dst.cull_sphere = cull_sphere;
			}
			ma->queue.clear();

			ma->consume_cond.notify_one();
			ma->produce_cond.wait(lock);

			goto begin;
		}

	}



	ObjectStorage ObjectStorage::create(
			Logger logger,
			std::shared_ptr<WorldRendererSharedState> wrSharedState,
			VmaAllocator vma,
			AssetSupplier& asset_supplier
	) {
		ObjectStorage r;
		r.mVma    = vma;
		r.mLogger = std::move(logger);
		r.mWrSharedState = std::move(wrSharedState);
		r.mAssetSupplier = &asset_supplier;
		r.mObjects            = decltype(mObjects)            (1024 * OBJECT_MAP_INITIAL_CAPACITY_KB / sizeof(decltype(mObjects)::value_type));
		r.mUnboundDrawBatches = decltype(mUnboundDrawBatches) (1024 * BATCH_MAP_INITIAL_CAPACITY_KB  / sizeof(decltype(mUnboundDrawBatches)::value_type));
		r.mObjects.           max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mUnboundDrawBatches.max_load_factor(BATCH_MAP_MAX_LOAD_FACTOR);
		r.mMatDpool         = nullptr;
		r.mMatDpoolCapacity = 0;
		r.mMatDpoolSize     = 0;
		r.mDrawCount        = 0;
		r.mMatrixAssemblerRunning = false;
		r.mBatchesNeedUpdate      = true;
		r.mObjectsNeedRebuild     = true;
		r.mObjectsNeedFlush       = true;
		r.mObjectBuffer = create_object_buffer           (r.mVma, 1024 * OBJECT_MAP_INITIAL_CAPACITY_KB / sizeof(dev::Object));
		r.mBatchBuffer  = create_draw_cmd_template_buffer(r.mVma, 1024 * BATCH_MAP_INITIAL_CAPACITY_KB  / sizeof(VkDrawIndexedIndirectCommand));

		{ // Initialize the matrix assembler
			r.mMatrixAssembler = std::make_shared<MatrixAssembler>();
			r.mMatrixAssembler->mutex.lock();
			r.mMatrixAssembler->thread = std::thread(matrix_worker_fn, r.mMatrixAssembler.get());
		}

		return r;
	}


	void ObjectStorage::destroy(TransferContext transfCtx, ObjectStorage& r) {
		assert(r.mVma != nullptr);
		auto dev = vmaGetAllocatorDevice(r.mVma);

		r.clearObjects(transfCtx);
		debug::destroyedBuffer(r.mBatchBuffer.first,  "indirect draw commands"); vkutil::Buffer::destroy(r.mVma, r.mBatchBuffer.first);
		debug::destroyedBuffer(r.mObjectBuffer.first, "object instances");       vkutil::Buffer::destroy(r.mVma, r.mObjectBuffer.first);

		if(r.mMatDpool != nullptr) {
			vkDestroyDescriptorPool(dev, r.mMatDpool, nullptr);
			r.mMatDpool = nullptr;
		}

		{
			assert(r.mMatrixAssembler->queue.empty());
			r.mMatrixAssembler->produce_cond.notify_one();
			r.mMatrixAssembler->mutex.unlock();
			r.mMatrixAssembler->thread.join();
			r.mMatrixAssembler = { };
		}

		r.mVma = nullptr;
	}


	ObjectId ObjectStorage::createObject(TransferContext transfCtx, const NewObject& ins) {
		assert(mVma != nullptr);

		auto new_obj_id = id_generator<ObjectId>.generate();
		auto model = getModel(ins.model_id);
		if(model == nullptr) model = & setModel(ins.model_id, mAssetSupplier->requestModel(ins.model_id, transfCtx));

		// Create needed device materials if they don't exist yet
		for(auto& bone : model->bones)
		if(nullptr == getMaterial(bone.material_id)) {
			setMaterial(bone.material_id, mAssetSupplier->requestMaterial(bone.material_id, transfCtx));
		}

		++ mModelDepCounters[ins.model_id];

		Object new_obj = {
			.model_id = ins.model_id,
			.position_xyz  = ins.position_xyz,
			.direction_ypr = ins.direction_ypr,
			.scale_xyz     = ins.scale_xyz,
			.hidden        = ins.hidden };

		std::vector<BoneInstance> bone_instances;
		bone_instances.reserve(model->bones.size());

		for(bone_id_e i = 0; auto& bone : model->bones) {
			bone_instances.push_back(BoneInstance {
				.model_id    = ins.model_id,
				.material_id = bone.material_id,
				.object_id   = new_obj_id,
				.color_rgba    = { 1.0f, 1.0f, 1.0f, 1.0f },
				.position_xyz  = { 0.0f, 0.0f, 0.0f },
				.direction_ypr = { 0.0f, 0.0f, 0.0f },
				.scale_xyz     = { 1.0f, 1.0f, 1.0f } });

			auto& model_slot          = assert_not_end_(mUnboundDrawBatches, ins.model_id)->second;
			auto& bone_slot           = assert_not_end_(model_slot, i)->second;
			auto  material_batch_iter = bone_slot.find(bone.material_id);
			if(material_batch_iter == bone_slot.end()) {
				auto ins = UnboundDrawBatch {
					.object_refs = decltype(UnboundDrawBatch::object_refs)(8),
					.material_id = bone.material_id,
					.model_bone_index = i };
				ins.object_refs.max_load_factor(UNBOUND_DRAW_BATCH_LOAD_FAC);
				ins.object_refs.insert(new_obj_id);
				bone_slot.insert(UnboundBatchMap::mapped_type::mapped_type::value_type(bone.material_id, ins));
			} else {
				auto& batch = material_batch_iter->second;
				batch.object_refs.insert(new_obj_id);
				assert(batch.material_id == bone.material_id);
			}

			++ i;
		}

		assert(! mObjects.contains(new_obj_id));
		mObjects[new_obj_id] = Objects::mapped_type(std::move(new_obj), std::move(bone_instances));
		mObjectUpdates.insert(new_obj_id);
		mBatchesNeedUpdate  = true;
		mObjectsNeedRebuild = true;
		mObjectsNeedFlush   = true;
		return new_obj_id;
	}


	void ObjectStorage::removeObject(TransferContext transfCtx, ObjectId id) noexcept {
		assert(mVma != nullptr);

		auto  obj_iter = assert_not_end_(mObjects, id);
		auto  obj      = std::move(obj_iter->second);
		auto& model    = assert_not_end_(mModels, obj.first.model_id)->second;

		auto model_dep_counter_iter = assert_not_end_(mModelDepCounters, obj.first.model_id);
		assert(model_dep_counter_iter->second > 0);
		-- model_dep_counter_iter->second;
		mObjects.erase(obj_iter);
		mObjectUpdates.erase(id);

		if(model_dep_counter_iter->second == 0) {
			mModelDepCounters.erase(model_dep_counter_iter);
			assert([&]() {
				// Check whether there is more than one set of batches for this model
				for(auto& bone  : assert_not_end_(mUnboundDrawBatches, obj.first.model_id)->second)
				for(auto& batch : bone.second) {
					if(batch.second.object_refs.size() != 1) return false; }
				return true;
			} ());
			eraseModelNoObjectCheck(transfCtx, obj.first.model_id, model);
		} else {
			// Remove the references from the unbound draw batches
			assert(obj.second.size() == model.bones.size() /* The Nth object bone instance refers to the model's Nth bone */);
			for(bone_id_e i = 0; auto& bone : model.bones) {
				auto  model_slot_iter = assert_not_end_(mUnboundDrawBatches, obj.first.model_id);
				auto  bone_iter       = assert_not_end_(model_slot_iter->second, i);
				auto  batch_iter      = assert_not_end_(bone_iter->second, bone.material_id);
				auto& obj_refs        = batch_iter->second.object_refs;
				[[maybe_unused]]
				std::size_t erased = obj_refs.erase(id);
				assert(erased == 1);
				assert(! obj_refs.empty() /* The model still has objects refering to it, so it *must* have at least one object for this material */);
				++ i;
			}
		}

		mBatchesNeedUpdate  = true;
		mObjectsNeedRebuild = true;
		mObjectsNeedFlush   = true;
	}


	void ObjectStorage::clearObjects(TransferContext transfCtx) noexcept {
		std::vector<ObjectId> ids;
		ids.reserve(mObjects.size());

		assert([&]() { /* Check object ID uniqueness */
			auto uniqueSet = std::unordered_set<ObjectId>(mObjects.size());
			for(auto& obj : mObjects) {
				auto ins = uniqueSet.insert(obj.first);
				if(! ins.second) return false;
			}
			return true;
		} ());

		for(auto&    obj : mObjects) ids.push_back(obj.first);
		for(ObjectId id  : ids)      removeObject(transfCtx, id);
	}


	std::optional<const Object*> ObjectStorage::getObject(ObjectId id) const noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) return &found->second.first;
		return { };
	}


	std::optional<ObjectStorage::ModifiableObject> ObjectStorage::modifyObject(ObjectId id) noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) {
			mObjectUpdates.insert(id);
			mBatchesNeedUpdate = true;
			mObjectsNeedFlush  = true;
			return ModifiableObject {
				.bones = std::span<BoneInstance>(found->second.second),
				.position_xyz  = found->second.first.position_xyz,
				.direction_ypr = found->second.first.direction_ypr,
				.scale_xyz     = found->second.first.scale_xyz,
				.hidden        = found->second.first.hidden };
		}
		return std::nullopt;
	}


	const ObjectStorage::ModelData* ObjectStorage::getModel(ModelId id) const noexcept {
		auto found = mModels.find(id);
		if(found == mModels.end()) return nullptr;
		return &found->second;
	}


	ObjectStorage::ModelData& ObjectStorage::setModel(ModelId id, DevModel model) {
		mLogger.trace("ObjectStorage: creating model device data for ID {}", model_id_e(id));

		// Insert the model data (with the backing string) first
		auto model_ins = mModels.insert(ModelMap::value_type(id, { model, id }));
		auto batch_ins = mUnboundDrawBatches.insert(UnboundBatchMap::value_type(id, UnboundBatchMap::mapped_type(UNBOUND_BATCH_LEVEL_1_INIT_CAP)));
		batch_ins.first->second.max_load_factor(UNBOUND_BATCH_LEVEL_1_LOAD_FAC);

		auto& bones = model_ins.first->second.bones;
		for(bone_id_e i = 0; i < bones.size(); ++i) {
			auto bone_ins = batch_ins.first->second.insert(UnboundBatchMap::mapped_type::value_type(
				i,
				UnboundBatchMap::mapped_type::mapped_type(UNBOUND_BATCH_LEVEL_2_INIT_CAP) ));
			bone_ins.first->second.max_load_factor(UNBOUND_BATCH_LEVEL_2_LOAD_FAC);
		}

		return model_ins.first->second;
	}


	void ObjectStorage::eraseModel(TransferContext transfCtx, ModelId id) noexcept {
		auto& model_data = assert_not_end_(mModels, id)->second;
		if(erase_objects_with_model(mObjects, mObjectUpdates, mLogger, id)) {
			mBatchesNeedUpdate = true;
		}
		eraseModelNoObjectCheck(transfCtx, id, model_data);
	}


	void ObjectStorage::eraseModelNoObjectCheck(TransferContext transfCtx, ModelId id, ModelData& model_data) noexcept {
		{ // Seek materials to release
			auto candidates = std::unordered_set<MaterialId>(model_data.bones.size());
			for(auto& bone : model_data.bones) {
				candidates.insert(decltype(candidates)::value_type(bone.material_id));
			}

			std::vector<decltype(candidates)::value_type> erase_queue;
			erase_queue.reserve(candidates.size());

			auto check_material = [&](const decltype(candidates)::value_type& candidate) {
				for(auto& model : mModels)
				if(model.first != id) [[likely]]
				for(auto& bone : model.second.bones) {
					if(bone.material_id == candidate) return;
				}
				erase_queue.push_back(candidate);
			};

			for(auto& candidate : candidates) check_material(candidate);
			candidates = { };

			for(auto& material : erase_queue) {
				mLogger.trace("ObjectStorage: removed unused material {}", material_id_e(material));
				eraseMaterial(transfCtx, material);
			}
		}

		mUnboundDrawBatches.erase(id);
		mAssetSupplier->releaseModel(id, transfCtx);
		mLogger.trace("ObjectStorage: removed model {}", model_id_e(id));
		mModels.erase(id); // Moving this line upward has already caused me some dangling string problems, I'll just leave this warning here
	}


	const ObjectStorage::MaterialData* ObjectStorage::getMaterial(MaterialId id) const noexcept {
		auto found = mMaterials.find(id);
		if(found == mMaterials.end()) return nullptr;
		return &found->second;
	}


	ObjectStorage::MaterialData& ObjectStorage::setMaterial(MaterialId id, Material material) {
		MaterialMap::iterator material_ins;

		mLogger.trace("ObjectStorage: creating material device data for ID {}", material_id_e(id));
		material_ins = mMaterials.insert(MaterialMap::value_type(id, { material, id, { } })).first;

		create_mat_dset(
			vmaGetAllocatorDevice(mVma),
			&mMatDpool, mWrSharedState->materialDsetLayout, &mMatDpoolSize, &mMatDpoolCapacity,
			mMaterials, &material_ins->second );

		return material_ins->second;
	}


	void ObjectStorage::eraseMaterial(TransferContext transfCtx, MaterialId id) noexcept {
		auto& mat_data = assert_not_end_(mMaterials, id)->second;

		#ifndef NDEBUG
		// Assert that no object is using the material: this function is only called internally when this is the case
		for(auto& obj  : mObjects)
		for(auto& bone : obj.second.second) {
			assert(bone.material_id != id);
		}
		#endif

		VK_CHECK(vkFreeDescriptorSets, vmaGetAllocatorDevice(mVma), mMatDpool, 1, &mat_data.dset);

		mAssetSupplier->releaseMaterial(mat_data.id, transfCtx);
		mMaterials.erase(id);
	}


	bool ObjectStorage::commitObjects(VkCommandBuffer cmd) {
		if(! (mBatchesNeedUpdate || mObjectsNeedRebuild || mObjectsNeedFlush )) {
			return false; }

		assert(mObjectsNeedFlush || ! mObjectsNeedRebuild);

		std::size_t new_instance_count = [&]() {
			std::size_t i = 0;
			for(auto& model_batches    : mUnboundDrawBatches)
			for(auto& bone_batches     : model_batches.second)
			for(auto& material_batches : bone_batches.second) {
				i += material_batches.second.object_refs.size();
			}
			return i;
		} ();

		size_t new_size = new_instance_count * sizeof(dev::Object);
		constexpr size_t shrink_fac = 4;

		{ // Ensure the object buffer is big enough
			bool size_too_small = (new_size > mObjectBuffer.second);
			bool size_too_big   = (new_size < mObjectBuffer.second / shrink_fac);
			if(size_too_small || size_too_big) {
				auto new_instance_count_ceil = std::bit_ceil(new_instance_count);
				mObjectsNeedRebuild = true;
				mObjectsNeedFlush   = true;
				debug::destroyedBuffer(mObjectBuffer.first, "object instances");
				vkutil::Buffer::destroy(mVma, mObjectBuffer.first);
				mObjectBuffer = create_object_buffer(mVma, new_instance_count_ceil);
			}
		}

		std::minstd_rand rng;
		auto             dist = std::uniform_real_distribution<float>(0.0f, 1.0f);
		auto* objects = mObjectBuffer.first.map<dev::Object>(mVma);
		mDrawBatchList.clear();

		auto set_object = [&](
				const Objects::iterator& obj_iter, ObjectId  obj_id,
				const Bone&              bone,     bone_id_e bone_idx,
				uint32_t obj_buffer_index
		) {
			auto& src_obj = obj_iter->second;
			auto& obj = objects[obj_buffer_index];

			obj.visible = ! src_obj.first.hidden;
			if(! obj.visible) return false;

			if(! mObjectUpdates.contains(obj_id)) {
				if(! mObjectsNeedRebuild) return true;
			}

			auto& bone_instance = src_obj.second[bone_idx];
			constexpr auto bone_id_digits = std::numeric_limits<bone_id_e>::digits;
			rng.seed(object_id_e(obj_id) ^ std::rotl(bone_idx, bone_id_digits / 2));

			obj.rnd       = dist(rng);
			obj.color_mul = bone_instance.color_rgba;

			{ // Enqueue a matrix assembly job
				MatrixAssembler::Job job;
				job.position  = { src_obj.first.position_xyz,  bone.position_xyz,  bone_instance.position_xyz };
				job.direction = { src_obj.first.direction_ypr, bone.direction_ypr, bone_instance.direction_ypr };
				job.scale     = { src_obj.first.scale_xyz,     bone.scale_xyz,     bone_instance.scale_xyz };
				job.mesh      = { .cull_sphere = { bone.mesh.cull_sphere_xyzr } };
				job.dst = { &obj.model_transf, &obj.cull_sphere_xyzr };
				mMatrixAssembler->queue.push_back(job);
			}

			return true;
		};

		// Returns the number of objects set
		auto set_objects = [&](UnboundDrawBatch& ubatch, const Bone& bone, uint32_t first_object) {
			struct R {
				uint32_t insert_count  = 0;
				uint32_t visible_count = 0;
			} r;
			for(auto obj_ref : ubatch.object_refs) { // Update/set the instances, while indirectly sorting the buffer
				auto src_obj_iter = mObjects.find(obj_ref);
				if(src_obj_iter == mObjects.end()) {
					mLogger.error("Renderer: trying to enqueue non-existent object {}", object_id_e(obj_ref));
					continue;
				}

				if(set_object(src_obj_iter, obj_ref, bone, ubatch.model_bone_index, first_object + r.visible_count)) {
					++ r.visible_count;
				}

				++ r.insert_count;
			}

			return r;
		};

		if(mBatchesNeedUpdate || mObjectsNeedFlush) {
			mDrawCount = 0;
			for(uint32_t first_object = 0; auto& model_batches : mUnboundDrawBatches) {
				auto& model = assert_not_end_(mModels, model_batches.first)->second;
				for(auto& bone_batches : model_batches.second) {
					auto& bone = model.bones[bone_batches.first];
					for(auto& ubatch : bone_batches.second) { // Create the (bound) draw batch
						auto object_set_count = set_objects(ubatch.second, bone, first_object);
						auto batch_idx = mDrawBatchList.size();
						mDrawBatchList.push_back(DrawBatch {
							.model_id       = model_batches.first,
							.material_id    = ubatch.first,
							.vertex_offset  = 0,
							.index_count    = bone.mesh.index_count,
							.first_index    = bone.mesh.first_index,
							.instance_count = object_set_count.insert_count,
							.first_instance = first_object });
						auto& back = mDrawBatchList.back();
						for(uint32_t i = 0; i < back.instance_count; ++i) {
							objects[back.first_instance + i].draw_batch_idx = batch_idx;
						}
						first_object += object_set_count.insert_count;
						mDrawCount   += object_set_count.insert_count;
					}
				}
			}

			{ // Wait for the matrix assembler
				assert(! mMatrixAssemblerRunning);
				if(! mMatrixAssembler->queue.empty()) {
					mMatrixAssemblerRunning = true;
					mMatrixAssembler->produce_cond.notify_one();
					mMatrixAssembler->mutex.unlock();
				}
			}

			mObjectsNeedRebuild = false;
			commit_draw_batches(mVma, mDrawBatchList, mBatchBuffer);

			{ // Barrier the buffer for outgoing transfer
				VkBufferMemoryBarrier2 bar = { };
				bar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
				bar.buffer = mBatchBuffer.first;
				bar.size = mDrawBatchList.size() * sizeof(VkDrawIndexedIndirectCommand);
				bar.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; bar.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				bar.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; bar.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
				VkDependencyInfo depInfo = { };
				depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				depInfo.bufferMemoryBarrierCount = 1;
				depInfo.pBufferMemoryBarriers = &bar;
				vkCmdPipelineBarrier2(cmd, &depInfo);
			}

			mBatchesNeedUpdate = false;
			mObjectUpdates.clear();
		}

		mObjectBuffer.first.unmap(mVma);

		return true;
	}


	void ObjectStorage::waitUntilReady() {
		if(! mMatrixAssemblerRunning) return;
		auto lock = std::unique_lock(mMatrixAssembler->mutex);
		if(! mMatrixAssembler->queue.empty() /* `consume_cond` may have already been notified */) {
			mMatrixAssembler->consume_cond.wait(lock);
		}
		lock.release(); // The consumer thread always holds the mutex, unless waiting
		mMatrixAssemblerRunning = false;
	}

}
