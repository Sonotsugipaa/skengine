#include "object_storage.hpp"

#include <engine/debug.inl.hpp>

#include "world_renderer.hpp"

#include <vk-util/error.hpp>

#include <glm/ext/matrix_transform.hpp>



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace SKENGINE_NAME_NS {

	namespace objstg { // Defined here, used elsewhere

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

	}



	namespace {

		constexpr size_t OBJECT_MAP_INITIAL_CAPACITY_KB = 32;
		constexpr float  OBJECT_MAP_MAX_LOAD_FACTOR     = 2;
		constexpr size_t BATCH_MAP_INITIAL_CAPACITY_KB  = 16;
		constexpr float  BATCH_MAP_MAX_LOAD_FACTOR      = 0.8;
		constexpr size_t UNBOUND_BATCH_LEVEL_1_INIT_CAP = 16;
		constexpr float  UNBOUND_BATCH_LEVEL_1_LOAD_FAC = 0.8;
		constexpr size_t UNBOUND_BATCH_LEVEL_2_INIT_CAP = 2;
		constexpr float  UNBOUND_BATCH_LEVEL_2_LOAD_FAC = 4.0;


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
		r.mObjectBuffer = objstg::create_object_buffer           (r.mVma, 1024 * OBJECT_MAP_INITIAL_CAPACITY_KB / sizeof(dev::Object));
		r.mBatchBuffer  = objstg::create_draw_cmd_template_buffer(r.mVma, 1024 * BATCH_MAP_INITIAL_CAPACITY_KB  / sizeof(VkDrawIndexedIndirectCommand));

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


	std::optional<const Object*> ObjectStorage::getObject(ObjectId id) const noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) return &found->second.first;
		return { };
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

}
