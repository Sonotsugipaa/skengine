#include "world_renderer.hpp"

#include "engine.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <tuple>
#include <type_traits>
#include <random>

#include <sys-resources.hpp>

#include "atomic_id_gen.inl.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <vk-util/error.hpp>



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace SKENGINE_NAME_NS {

	namespace {

		constexpr size_t OBJECT_MAP_INITIAL_CAPACITY = 4;
		constexpr float  OBJECT_MAP_MAX_LOAD_FACTOR  = OBJECT_MAP_INITIAL_CAPACITY;
		constexpr size_t BATCH_MAP_INITIAL_CAPACITY  = 16;
		constexpr float  BATCH_MAP_MAX_LOAD_FACTOR   = 1.0;


		constexpr auto light_storage_create_info(size_t light_count) {
			vkutil::BufferCreateInfo r = { };
			r.size  = light_count * sizeof(dev::Light);
			r.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			return r;
		};

		constexpr auto light_storage_allocate_info = []() {
			vkutil::AllocationCreateInfo r = { };
			r.requiredMemFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			r.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			r.vmaUsage = vkutil::VmaAutoMemoryUsage::eAutoPreferHost;
			return r;
		} ();


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
				WorldRenderer::MaterialData* mat
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
				constexpr auto& DFS = Engine::DIFFUSE_TEX_BINDING;
				constexpr auto& NRM = Engine::NORMAL_TEX_BINDING;
				constexpr auto& SPC = Engine::SPECULAR_TEX_BINDING;
				constexpr auto& EMI = Engine::EMISSIVE_TEX_BINDING;
				constexpr auto& UBO = Engine::MATERIAL_UBO_BINDING;
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
				WorldRenderer::MaterialMap& materials,
				size_t* size,
				size_t* capacity,
				WorldRenderer::MaterialData* dst
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


		vkutil::BufferDuplex create_object_buffer(VmaAllocator vma, size_t count) {
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = std::bit_ceil(count) * sizeof(dev::Instance);
			bc_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			vkutil::AllocationCreateInfo ac_info = { };
			ac_info.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			ac_info.vmaFlags          = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
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


		void commit_draw_batches(
				VmaAllocator vma,
				VkCommandBuffer cmd,
				const WorldRenderer::BatchList& batches,
				vkutil::BufferDuplex& buffer
		) {
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


		bool erase_objects_with_model(
				WorldRenderer::Objects& objects,
				WorldRenderer::ObjectUpdates& object_updates,
				spdlog::logger& log,
				ModelId id,
				std::string_view locator
		) {
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
			for(auto obj : rm_objects) {
				objects.erase(obj);
				object_updates.erase(obj);
			}
			return ! rm_objects.empty();
		}


		uint32_t set_light_buffer_capacity(VmaAllocator vma, WorldRenderer::LightStorage* dst, uint32_t desired) {
			static_assert(std::bit_ceil(0u) == 1u);
			desired = std::bit_ceil(desired);
			if(desired != dst->bufferCapacity) {
				if(dst->bufferCapacity > 0) {
					dst->buffer.unmap(vma);
					vkutil::ManagedBuffer::destroy(vma, dst->buffer);
				}

				auto  bc_info = light_storage_create_info(desired);
				auto& ac_info = light_storage_allocate_info;
				dst->buffer    = vkutil::ManagedBuffer::create(vma, bc_info, ac_info);
				dst->mappedPtr = dst->buffer.map<dev::Light>(vma);

				dst->bufferCapacity = desired;
			}
			return desired;
		}


		void matrixWorkerFn(WorldRendererBase::MatrixAssembler* ma, size_t thread_index) {
			auto& worker = ma->workers[thread_index];
			auto  lock   = std::unique_lock(worker.cond->mutex);

			begin:
			if(worker.queue.empty()) [[unlikely]] {
				lock.unlock();
				return;
			}

			for(auto& job : worker.queue) {
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
				*job.dst = identity;
				translate (job.dst, job.position.object);
				translate (job.dst, job.position.bone);
				translate (job.dst, job.position.bone_instance);
				rotate    (job.dst, job.direction.object);
				rotate    (job.dst, job.direction.bone);
				rotate    (job.dst, job.direction.bone_instance);
				scale     (job.dst, job.scale.object);
				scale     (job.dst, job.scale.bone);
				scale     (job.dst, job.scale.bone_instance);
			}
			worker.queue.clear();

			worker.cond->consume_cond.notify_one();
			worker.cond->produce_cond.wait(lock);

			goto begin;
		}


		void updateLightStorageDset(VkDevice dev, VkBuffer buffer, size_t lightCount, VkDescriptorSet dset) {
			VkDescriptorBufferInfo db_info = { };
			db_info.buffer = buffer;
			db_info.range  = lightCount * sizeof(dev::Light);
			VkWriteDescriptorSet wr = { };
			wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			wr.descriptorCount = 1;
			wr.dstSet          = dset;
			wr.dstBinding      = Engine::LIGHT_STORAGE_BINDING;
			wr.pBufferInfo     = &db_info;
			vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
		}

	}



	WorldRendererBase WorldRendererBase::create(
			std::shared_ptr<spdlog::logger> logger,
			VmaAllocator vma,
			DsetLayout dset_layout,
			AssetSupplier& asset_supplier
	) {
		VmaAllocatorInfo vma_info;
		vmaGetAllocatorInfo(vma, &vma_info);
		WorldRendererBase r;
		r.mDevice = vma_info.device;
		r.mVma    = vma;
		r.mLogger = std::move(logger);
		r.mAssetSupplier = &asset_supplier;
		r.mObjects             = decltype(mObjects)            (OBJECT_MAP_INITIAL_CAPACITY);
		r.mModelLocators       = decltype(mModelLocators)      (OBJECT_MAP_INITIAL_CAPACITY);
		r.mUnboundDrawBatches  = decltype(mUnboundDrawBatches) (OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects.           max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mUnboundDrawBatches.max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mDsetLayout    = dset_layout;
		r.mDpoolCapacity = 0;
		r.mDpoolSize     = 0;
		r.mBatchesNeedUpdate  = true;
		r.mObjectsNeedRebuild = true;
		r.mObjectsNeedFlush   = true;
		r.mObjectBuffer = create_object_buffer(r.mVma, OBJECT_MAP_INITIAL_CAPACITY);
		r.mBatchBuffer  = create_draw_buffer(r.mVma, BATCH_MAP_INITIAL_CAPACITY);

		{ // Initialize the matrix assembler
			constexpr auto maxWorkerCount = 4; // This heuristic value seems to outperform other bottlenecks
			auto workerCount = std::min<size_t>(maxWorkerCount, sysres::optimalWorkerCount());
			r.mLogger->info("WorldRendererBase: using {} workers", workerCount);
			r.mMatrixAssembler = std::make_shared<MatrixAssembler>();
			r.mMatrixAssembler->workers.reserve(workerCount);
			for(size_t i = 0; i < workerCount; ++i) {
				r.mMatrixAssembler->workers.emplace_back();
				r.mMatrixAssembler->workers.back().cond->mutex.lock();
				r.mMatrixAssembler->workers.back().thread = std::thread(matrixWorkerFn, r.mMatrixAssembler.get(), i);
			}
		}

		return r;
	}


	void WorldRendererBase::destroy(WorldRendererBase& r) {
		assert(r.mDevice != nullptr);

		r.clearObjects();
		vkutil::BufferDuplex::destroy(r.mVma, r.mBatchBuffer);
		vkutil::BufferDuplex::destroy(r.mVma, r.mObjectBuffer);

		vkDestroyDescriptorPool(r.mDevice, r.mDpool, nullptr);

		{
			for(auto& worker : r.mMatrixAssembler->workers) {
				assert(worker.queue.empty());
				worker.cond->produce_cond.notify_one();
				worker.cond->mutex.unlock();
			}
			for(auto& worker : r.mMatrixAssembler->workers) worker.thread.join();
			r.mMatrixAssembler = { };
		}

		r.mDevice = nullptr;
	}


	ObjectId WorldRendererBase::createObject(const NewObject& ins) {
		assert(mDevice != nullptr);

		auto new_obj_id = id_generator<ObjectId>.generate();
		auto model_id   = getModelId(ins.model_locator);
		auto model      = assert_not_nullptr_(getModel(model_id));

		mLogger->trace("Generated Object ID {:x}", object_id_e(new_obj_id));

		++ mModelDepCounters[model_id];

		Object new_obj = {
			.model_id = model_id,
			.position_xyz  = ins.position_xyz,
			.direction_ypr = ins.direction_ypr,
			.scale_xyz     = ins.scale_xyz,
			.hidden        = ins.hidden };

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
		mObjectUpdates.insert(new_obj_id);
		mBatchesNeedUpdate  = true;
		mObjectsNeedRebuild = true;
		mObjectsNeedFlush   = true;
		return new_obj_id;
	}


	void WorldRendererBase::removeObject(ObjectId id) noexcept {
		assert(mDevice != nullptr);

		auto  obj_iter = assert_not_end_(mObjects, id);
		auto  obj      = std::move(obj_iter->second);
		auto& model    = assert_not_end_(mModels, obj.first.model_id)->second;

		auto model_dep_counter_iter = assert_not_end_(mModelDepCounters, obj.first.model_id);
		assert(model_dep_counter_iter->second > 0);
		-- model_dep_counter_iter->second;
		mObjects.erase(obj_iter);
		mObjectUpdates.erase(id);
		id_generator<ObjectId>.recycle(id);

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

		mBatchesNeedUpdate  = true;
		mObjectsNeedRebuild = true;
		mObjectsNeedFlush   = true;
	}


	void WorldRendererBase::clearObjects() noexcept {
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
		for(ObjectId id  : ids)      removeObject(id);
	}


	std::optional<const Object*> WorldRendererBase::getObject(ObjectId id) const noexcept {
		auto found = mObjects.find(id);
		if(found != mObjects.end()) return &found->second.first;
		return { };
	}


	std::optional<WorldRendererBase::ModifiableObject> WorldRendererBase::modifyObject(ObjectId id) noexcept {
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


	ModelId WorldRendererBase::getModelId(std::string_view locator) {
		auto found_locator = mModelLocators.find(locator);

		if(found_locator != mModelLocators.end()) {
			return found_locator->second;
		} else {
			auto r = setModel(locator, mAssetSupplier->requestModel(locator));
			return r;
		}
	}


	const WorldRendererBase::ModelData* WorldRendererBase::getModel(ModelId id) const noexcept {
		auto found = mModels.find(id);
		if(found == mModels.end()) return nullptr;
		return &found->second;
	}


	ModelId WorldRendererBase::setModel(std::string_view locator, DevModel model) {
		auto    found_locator = mModelLocators.find(locator);
		ModelId id;

		// Generate a new ID, or remove the existing one and reassign it
		if(found_locator != mModelLocators.end()) { // Remove the existing ID and reassign it
			id = found_locator->second;
			mLogger->debug("WorldRendererBase: reassigning model \"{}\" with ID {}", locator, model_id_e(id));

			std::string locator_s = std::string(locator); // Dangling `locator` string_view
			eraseModel(found_locator->second);
			auto ins = mModels.insert_or_assign(id, ModelData(model, locator_s));
			locator = ins.first->second.locator; // Again, `locator` was dangling
		} else {
			id = id_generator<ModelId>.generate();
			mLogger->trace("WorldRendererBase: associating model \"{}\" with ID {}", locator, model_id_e(id));
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


	void WorldRendererBase::eraseModel(ModelId id) noexcept {
		auto& model_data = assert_not_end_(mModels, id)->second;
		if(erase_objects_with_model(mObjects, mObjectUpdates, *mLogger, id, model_data.locator)) {
			mBatchesNeedUpdate = true;
		}
		eraseModelNoObjectCheck(id, model_data);
	}


	void WorldRendererBase::eraseModelNoObjectCheck(ModelId id, ModelData& model_data) noexcept {
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
				mLogger->trace("WorldRendererBase: removed unused material {}, \"{}\"", material_id_e(material.first), material.second);
				eraseMaterial(material.first);
			}
		}

		mModelLocators.erase(model_locator);

		mUnboundDrawBatches.erase(id);
		mAssetSupplier->releaseModel(model_locator);
		mLogger->trace("WorldRendererBase: removed model \"{}\"", model_locator);
		mModels.erase(id); // Moving this line upward has already caused me some dangling string problems, I'll just leave this warning here
		id_generator<ModelId>.recycle(id);
	}


	MaterialId WorldRendererBase::getMaterialId(std::string_view locator) {
		auto found_locator = mMaterialLocators.find(locator);

		if(found_locator != mMaterialLocators.end()) {
			return found_locator->second;
		} else {
			return setMaterial(locator, mAssetSupplier->requestMaterial(locator));
		}
	}


	const WorldRendererBase::MaterialData* WorldRendererBase::getMaterial(MaterialId id) const noexcept {
		auto found = mMaterials.find(id);
		if(found == mMaterials.end()) return nullptr;
		return &found->second;
	}


	MaterialId WorldRendererBase::setMaterial(std::string_view locator, Material material) {
		auto found_locator = mMaterialLocators.find(locator);
		MaterialId id;

		MaterialMap::iterator material_ins;

		// Generate a new ID, or remove the existing one and reassign it
		if(found_locator != mMaterialLocators.end()) {
			id = found_locator->second;
			mLogger->debug("WorldRendererBase: reassigning material \"{}\" with ID {}", locator, material_id_e(id));

			std::string locator_s = std::string(locator); // Dangling `locator` string_view
			eraseMaterial(found_locator->second);
			material_ins = mMaterials.insert(MaterialMap::value_type(id, MaterialData(material, { }, locator_s))).first;
			locator = material_ins->second.locator; // Again, `locator` was dangling
		} else {
			id = id_generator<MaterialId>.generate();
			mLogger->trace("WorldRendererBase: associating material \"{}\" with ID {}", locator, material_id_e(id));
			material_ins = mMaterials.insert(MaterialMap::value_type(id, { material, { }, std::string(locator) })).first;
		}

		// Insert the material data (with the backing string) first
		auto& locator_r = material_ins->second.locator;
		create_mat_dset(mDevice, &mDpool, mDsetLayout, mMaterials, &mDpoolSize, &mDpoolCapacity, &material_ins->second);

		mMaterialLocators.insert(MaterialLookup::value_type(locator_r, id));

		return id;
	}


	void WorldRendererBase::eraseMaterial(MaterialId id) noexcept {
		auto& mat_data = assert_not_end_(mMaterials, id)->second;

		#ifndef NDEBUG
		// Assert that no object is using the material: this function is only called internally when this is the case
		for(auto& obj  : mObjects)
		for(auto& bone : obj.second.second) {
			assert(bone.material_id != id);
		}
		#endif

		VK_CHECK(vkFreeDescriptorSets, mDevice, mDpool, 1, &mat_data.dset);

		mMaterialLocators.erase(mat_data.locator);

		mAssetSupplier->releaseMaterial(mat_data.locator);
		mMaterials.erase(id);
		id_generator<MaterialId>.recycle(id);
	}


	bool WorldRendererBase::commitObjects(VkCommandBuffer cmd) {
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

		std::size_t    new_size   = new_instance_count * sizeof(dev::Instance);
		constexpr auto shrink_fac = OBJECT_MAP_INITIAL_CAPACITY;

		{ // Ensure the object buffer is big enough
			bool size_too_small = (new_size > mObjectBuffer.size());
			bool size_too_big   = (new_size < mObjectBuffer.size() / shrink_fac);
			if(size_too_small || size_too_big) {
				mObjectsNeedRebuild = true;
				mObjectsNeedFlush   = true;
				vkutil::BufferDuplex::destroy(mVma, mObjectBuffer);
				mObjectBuffer = create_object_buffer(mVma, std::bit_ceil(new_instance_count));
			}
		}

		std::minstd_rand rng;
		auto             dist = std::uniform_real_distribution<float>(0.0f, 1.0f);
		auto* objects = mObjectBuffer.mappedPtr<dev::Instance>();
		mDrawBatchList.clear();

		std::vector<VkBufferCopy> copies;
		if(mObjectsNeedFlush) {
			copies.reserve(mObjectUpdates.size());
		}

		auto set_object = [&](
				const Objects::iterator& obj_iter, ObjectId  obj_id,
				const Bone&              bone,     bone_id_e bone_idx,
				uint32_t obj_buffer_index
		) {
			auto& src_obj = obj_iter->second;

			if(src_obj.first.hidden) return false;

			if(! mObjectUpdates.contains(obj_id)) {
				if(! mObjectsNeedRebuild) return true;
			} else {
				VkDeviceSize offset = VkDeviceSize(obj_buffer_index) * sizeof(dev::Instance);
				copies.push_back(VkBufferCopy {
					.srcOffset = offset, .dstOffset = offset,
					.size = sizeof(dev::Instance) });
			}

			auto& bone_instance = src_obj.second[bone_idx];
			constexpr auto bone_id_digits = std::numeric_limits<bone_id_e>::digits;
			rng.seed(object_id_e(obj_id) ^ std::rotl(bone_idx, bone_id_digits / 2));

			auto& obj = objects[obj_buffer_index];
			obj.rnd       = dist(rng);
			obj.color_mul = bone_instance.color_rgba;

			{ // Enqueue a matrix assembly job
				MatrixAssembler::Job job;
				job.position  = { src_obj.first.position_xyz,  bone.position_xyz,  bone_instance.position_xyz };
				job.direction = { src_obj.first.direction_ypr, bone.direction_ypr, bone_instance.direction_ypr };
				job.scale     = { src_obj.first.scale_xyz,     bone.scale_xyz,     bone_instance.scale_xyz };
				job.dst = &obj.model_transf;
				auto& worker = mMatrixAssembler->workers[object_id_e(obj_id) % mMatrixAssembler->workers.size()];
				worker.queue.push_back(job);
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
					mLogger->error("Renderer: trying to enqueue non-existent object {}", object_id_e(obj_ref));
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
							.instance_count = object_set_count.visible_count,
							.first_instance = first_object });
						first_object += object_set_count.insert_count;
					}
				}
			}

			{ // Wait for the matrix assembler
				auto& runningWorkers = mMatrixAssemblerRunningWorkers;
				assert(runningWorkers.empty());
				runningWorkers.reserve(mMatrixAssembler->workers.size());
				for(size_t i = 0; i < mMatrixAssembler->workers.size(); ++i) {
					auto& worker = mMatrixAssembler->workers[i];
					if(! worker.queue.empty()) {
						runningWorkers.push_back(i);
						worker.cond->produce_cond.notify_one();
						worker.cond->mutex.unlock();
					}
				}
			}

			mObjectsNeedRebuild = false;
			commit_draw_batches(mVma, cmd, mDrawBatchList, mBatchBuffer);
			mBatchesNeedUpdate = false;
			mObjectUpdates.clear();
		}

		if(mObjectsNeedFlush) {
			mObjectBuffer.flush(cmd, mVma, std::span(copies));
		}
		mObjectsNeedFlush = false;

		return true;
	}


	void WorldRendererBase::waitUntilReady() {
		auto& runningWorkers = mMatrixAssemblerRunningWorkers;
		if(runningWorkers.empty()) [[unlikely]] return;
		for(size_t i = 0; i < runningWorkers.size(); ++i) {
			auto& worker = mMatrixAssembler->workers[runningWorkers[i]];
			auto lock = std::unique_lock(worker.cond->mutex);
			if(! worker.queue.empty() /* `consume_cond` may have already been notified */) {
				worker.cond->consume_cond.wait(lock);
			}
			lock.release(); // The consumer thread always holds the mutex, unless waiting
		}
		runningWorkers.clear();
	}

}



namespace SKENGINE_NAME_NS {

	constexpr auto world_renderer_subpass_info = Renderer::Info {
		.shaderReq = { .name = "default", .pipelineLayout = PipelineLayoutId::e3d },
		.rpass = Renderer::RenderPass::eWorld };


	WorldRenderer::WorldRenderer(): Renderer(world_renderer_subpass_info) { mState.initialized = false; }

	WorldRenderer::WorldRenderer(WorldRenderer&& mv):
		WorldRendererBase(std::move(mv)),
		Renderer(world_renderer_subpass_info),
		mState(std::move(mv.mState))
	{
		mv.mState.initialized = false;
	}

	WorldRenderer::~WorldRenderer() {
		if(mState.initialized) {
			WorldRenderer::destroy(*this);
			mState.initialized = false;
		}
	}


	WorldRenderer WorldRenderer::create(
			std::shared_ptr<spdlog::logger> logger,
			VmaAllocator vma,
			DsetLayout dset_layout,
			AssetSupplier& asset_supplier
	) {
		WorldRenderer r = WorldRendererBase::create(std::move(logger), vma, dset_layout, asset_supplier);
		r.mState.viewPosXyz = { };
		r.mState.viewDirYpr = { };
		r.mState.ambientLight = { };
		r.mState.lightStorage = { };
		r.mState.lightStorageOod = true;
		r.mState.lightStorageDsetOod = true;
		r.mState.viewTransfCacheOod = true;
		r.mState.initialized = true;
		set_light_buffer_capacity(r.mVma, &r.mState.lightStorage, 0);
		return r;
	}


	void WorldRenderer::destroy(WorldRenderer& r) {
		assert(r.mState.initialized);

		for(auto& gf : r.mState.gframes) {
			vkutil::ManagedBuffer::destroy(r.mVma, gf.lightStorage);
		}
		r.mState.gframes.clear();

		if(r.mState.lightStorage.bufferCapacity > 0) {
			r.mState.lightStorage.bufferCapacity = 0;
			r.mState.lightStorage.buffer.unmap(r.mVma);
			vkutil::ManagedBuffer::destroy(r.mVma, r.mState.lightStorage.buffer);
		}

		{
			std::vector<ObjectId> removeList;
			removeList.reserve(r.mState.pointLights.size() + r.mState.rayLights.size());
			for(auto& l : r.mState.pointLights) removeList.push_back(l.first);
			for(auto& l : r.mState.rayLights  ) removeList.push_back(l.first);
			for(auto  l : removeList) r.removeLight(l);
		}

		r.mState.initialized = false;

		WorldRendererBase::destroy(r);
	}


	void WorldRenderer::afterSwapchainCreation(ConcurrentAccess&, unsigned gframeCount) {
		uint32_t lightCapacity = mState.lightStorage.bufferCapacity;

		size_t oldGframeCount = mState.gframes.size();

		vkutil::BufferCreateInfo lightStorageBcInfo = {
			.size  = lightCapacity * sizeof(dev::Light),
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.qfamSharing = { } };

		auto setFrameLightStorage = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];
			wgf.lightStorage = vkutil::ManagedBuffer::createStorageBuffer(mVma, lightStorageBcInfo);
			wgf.lightStorageCapacity = lightCapacity;
			mState.lightStorageDsetOod = true;
		};

		auto unsetFrameLightStorage = [&](unsigned gfIndex) {
			GframeData& gf = mState.gframes[gfIndex];
			vkutil::ManagedBuffer::destroy(mVma, gf.lightStorage);
			gf.lightStorageCapacity = 0;
		};

		if(oldGframeCount < gframeCount) {
			#define FOR_EACH_ for(size_t i = oldGframeCount; i < gframeCount; ++i)
			mState.gframes.resize(gframeCount);
			if(lightCapacity != 0) FOR_EACH_ setFrameLightStorage(i);
			#undef FOR_EACH_
		} else
		if(oldGframeCount > gframeCount) {
			#define FOR_EACH_ for(size_t i = gframeCount; i < oldGframeCount; ++i)
			if(lightCapacity != 0) FOR_EACH_ unsetFrameLightStorage(i);
			mState.gframes.resize(gframeCount);
			#undef FOR_EACH_
		}
	}


	void WorldRenderer::beforePreRender(ConcurrentAccess&, unsigned) { }


	void WorldRenderer::duringPrepareStage(ConcurrentAccess& ca, unsigned gframeIndex, VkCommandBuffer cmd) {
		auto& e   = ca.engine();
		auto& egf = ca.getGframeData(gframeIndex);
		auto& wgf = mState.gframes[gframeIndex];
		auto& ls  = lightStorage();
		auto& al  = getAmbientLight();
		auto& ubo = * egf.frame_ubo.mappedPtr<dev::FrameUniform>();
		auto  dev = e.getDevice();
		auto  vma = e.getVmaAllocator();
		auto  all  = glm::length(al);

		if(mState.lightStorageOod) {
			uint32_t new_ls_size = mState.rayLights.size() + mState.pointLights.size();
			set_light_buffer_capacity(mVma, &mState.lightStorage, new_ls_size);

			mState.lightStorage.rayCount   = mState.rayLights.size();
			mState.lightStorage.pointCount = mState.pointLights.size();
			auto& ray_count = mState.lightStorage.rayCount;
			for(uint32_t i = 0; auto& rl : mState.rayLights) {
				auto& dst = *reinterpret_cast<dev::RayLight*>(mState.lightStorage.mappedPtr + i);
				dst.direction     = glm::vec4(- glm::normalize(rl.second.direction), 1.0f);
				dst.color         = glm::vec4(glm::normalize(rl.second.color), rl.second.intensity);
				dst.aoa_threshold = rl.second.aoa_threshold;
				++ i;
			}
			for(uint32_t i = ray_count; auto& pl : mState.pointLights) {
				auto& dst = *reinterpret_cast<dev::PointLight*>(mState.lightStorage.mappedPtr + i);
				dst.position    = glm::vec4(pl.second.position, 1.0f);
				dst.color       = glm::vec4(glm::normalize(pl.second.color), pl.second.intensity);
				dst.falloff_exp = pl.second.falloff_exp;
				++ i;
			}

			mState.lightStorage.buffer.flush(mVma);
			mState.lightStorageDsetOod = true;
			mState.lightStorageOod = false;
		}

		ubo.ambient_lighting  = glm::vec4((all > 0)? glm::normalize(al) : al, all);
		ubo.view_transf       = getViewTransf();
		ubo.view_pos          = glm::inverse(ubo.view_transf) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		ubo.projview_transf   = ubo.proj_transf * ubo.view_transf;
		ubo.ray_light_count   = ls.rayCount;
		ubo.point_light_count = ls.pointCount;

		WorldRendererBase::commitObjects(cmd);

		bool buffer_resized = wgf.lightStorageCapacity != ls.bufferCapacity;
		if(buffer_resized) {
			mLogger->trace("Resizing light storage: {} -> {}", wgf.lightStorageCapacity, ls.bufferCapacity);
			vkutil::ManagedBuffer::destroy(vma, wgf.lightStorage);

			vkutil::BufferCreateInfo bc_info = { };
			bc_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bc_info.size  = ls.bufferCapacity * sizeof(dev::Light);
			wgf.lightStorage = vkutil::ManagedBuffer::createStorageBuffer(vma, bc_info);

			#warning "TODO: Renderer-owned descriptor sets, or something"

			mState.lightStorageDsetOod = true;
			wgf.lightStorageCapacity = ls.bufferCapacity;
		}

		if(mState.lightStorageDsetOod) {
			updateLightStorageDset(dev, wgf.lightStorage.value, wgf.lightStorageCapacity, egf.frame_dset);
			mState.lightStorageDsetOod = false;
		}

		if(true /* Optimizable, but not worth the effort */) {
			VkBufferCopy cp = { };
			cp.size = (ls.rayCount + ls.pointCount) * sizeof(dev::Light);
			vkCmdCopyBuffer(cmd, ls.buffer.value, wgf.lightStorage, 1, &cp);
		}
	}


	void WorldRenderer::duringDrawStage(ConcurrentAccess& ca, unsigned gfIndex, VkCommandBuffer cmd) {
		SKENGINE_NAME_NS_SHORT::GframeData& egf = ca.getGframeData(gfIndex);
		auto& e = ca.engine();
		auto batches         = getDrawBatches();
		auto instance_buffer = getInstanceBuffer();
		auto batch_buffer    = getDrawCommandBuffer();

		if(batches.empty()) return;

		WorldRendererBase::waitUntilReady();

		VkDescriptorSet dsets[]  = { egf.frame_dset, { } };
		ModelId         last_mdl = ModelId    (~ model_id_e    (batches.front().model_id));
		MaterialId      last_mat = MaterialId (~ material_id_e (batches.front().material_id));
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ca.getPipeline(pipelineInfo().shaderReq));
		for(VkDeviceSize i = 0; const auto& batch : batches) {
			auto* model = getModel(batch.model_id);
			assert(model != nullptr);
			if(batch.model_id != last_mdl) {
				VkBuffer vtx_buffers[] = { model->vertices.value, instance_buffer };
				constexpr VkDeviceSize offsets[] = { 0, 0 };
				vkCmdBindIndexBuffer(cmd, model->indices.value, 0, VK_INDEX_TYPE_UINT32);
				vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
			}
			if(batch.material_id != last_mat) {
				auto mat = getMaterial(batch.material_id);
				assert(mat != nullptr);
				dsets[Engine::MATERIAL_DSET_LOC] = mat->dset;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e.getPipelineLayout3d(), 0, std::size(dsets), dsets, 0, nullptr);
			}
			vkCmdDrawIndexedIndirect(
				cmd, batch_buffer,
				i * sizeof(VkDrawIndexedIndirectCommand), 1,
				sizeof(VkDrawIndexedIndirectCommand) );
			++ i;
		}
	}


	const glm::mat4& WorldRenderer::getViewTransf() noexcept {
		if(mState.viewTransfCacheOod) {
			constexpr glm::vec3 x = { 1.0f, 0.0f, 0.0f };
			constexpr glm::vec3 y = { 0.0f, 1.0f, 0.0f };
			constexpr glm::vec3 z = { 0.0f, 0.0f, 1.0f };
			constexpr glm::mat4 identity = glm::mat4(1.0f);

			glm::mat4 translate = glm::translate(identity, -mState.viewPosXyz);
			glm::mat4 rot0      = glm::rotate(identity, +mState.viewDirYpr.z, z);
			glm::mat4 rot1      = glm::rotate(identity, -mState.viewDirYpr.x, y); // I have no idea why this has to be negated for the right-hand rule to apply
			glm::mat4 rot2      = glm::rotate(identity, +mState.viewDirYpr.y, x);
			mState.viewTransfCache = rot2 * rot1 * rot0 * translate;
			mState.viewTransfCacheOod = false;
		}

		return mState.viewTransfCache;
	}


	void WorldRenderer::setViewPosition(const glm::vec3& xyz, bool lazy) noexcept {
		mState.viewPosXyz = xyz;
		mState.viewTransfCacheOod = mState.viewTransfCacheOod | ! lazy;
	}


	void WorldRenderer::setViewRotation(const glm::vec3& ypr, bool lazy) noexcept {
		{ // Normalize the direction values
			constexpr auto pi2 = 2.0f * std::numbers::pi_v<float>;
			#define NORMALIZE_(I_) { if(mState.viewDirYpr[I_] >= pi2) [[unlikely]] { \
				mState.viewDirYpr[I_] = std::floor(mState.viewDirYpr[I_] / pi2) * pi2; }}
			NORMALIZE_(0)
			NORMALIZE_(1)
			NORMALIZE_(2)
			#undef NORMALIZE_
		}

		mState.viewDirYpr = ypr;
		mState.viewTransfCacheOod = mState.viewTransfCacheOod | ! lazy;
	}


	void WorldRenderer::setAmbientLight(const glm::vec3& rgb, bool lazy) noexcept {
		mState.ambientLight = rgb;
		mState.viewTransfCacheOod = mState.viewTransfCacheOod | ! lazy;
	}


	void WorldRenderer::setViewDirection(const glm::vec3& xyz, bool lazy) noexcept {
		/*       -x         '  '        +y         '
		'         |         '  '         |         '
		'         |         '  '         |         '
		'  +z ----O---- -z  '  '  +z ----O---- -z  '
		'         |         '  '         |         '
		'         |         '  '         |         '
		'        +x         '  '        -y        */
		glm::vec3 ypr;
		ypr[0] = std::atan2(-xyz.x, -xyz.z);
		ypr[1] = std::atan2(+xyz.y, -xyz.z);
		ypr[2] = 0;
		mState.viewDirYpr = ypr;
		mState.viewTransfCacheOod = mState.viewTransfCacheOod | ! lazy;
	}


	[[nodiscard]]
	ObjectId WorldRenderer::createRayLight(const NewRayLight& nrl) {
		auto r = id_generator<ObjectId>.generate();
		RayLight rl = { };
		rl.direction     = glm::normalize(nrl.direction);
		rl.color         = nrl.color;
		rl.intensity     = std::max(nrl.intensity, 0.0f);
		rl.aoa_threshold = nrl.aoaThreshold;
		mState.rayLights.insert(RayLights::value_type { r, std::move(rl) });
		mState.lightStorageOod = true;
		return r;
	}


	[[nodiscard]]
	ObjectId WorldRenderer::createPointLight(const NewPointLight& npl) {
		auto r = id_generator<ObjectId>.generate();
		PointLight pl = { };
		pl.position    = npl.position;
		pl.color       = npl.color;
		pl.intensity   = std::max(npl.intensity, 0.0f);
		pl.falloff_exp = std::max(npl.falloffExponent, 0.0f);
		mState.pointLights.insert(PointLights::value_type { r, std::move(pl) });
		mState.lightStorageOod = true;
		return r;
	}


	void WorldRenderer::removeLight(ObjectId id) {
		assert(mState.pointLights.contains(id) || mState.rayLights.contains(id));
		mState.lightStorageOod = true;
		if(0 == mState.rayLights.erase(id)) mState.pointLights.erase(id);
		id_generator<ObjectId>.recycle(id);
	}


	const RayLight& WorldRenderer::getRayLight(ObjectId id) const {
		return assert_not_end_(mState.rayLights, id)->second;
	}


	const PointLight& WorldRenderer::getPointLight(ObjectId id) const {
		return assert_not_end_(mState.pointLights, id)->second;
	}


	RayLight& WorldRenderer::modifyRayLight(ObjectId id) {
		mState.lightStorageOod = true;
		return assert_not_end_(mState.rayLights, id)->second;
	}


	PointLight& WorldRenderer::modifyPointLight(ObjectId id) {
		mState.lightStorageOod = true;
		return assert_not_end_(mState.pointLights, id)->second;
	}


	WorldRenderer::WorldRenderer(WorldRendererBase&& mv):
		WorldRendererBase::WorldRendererBase(std::move(mv)),
		Renderer(world_renderer_subpass_info)
	{ }

}



#undef assert_not_end_
#undef assert_not_nullptr_
