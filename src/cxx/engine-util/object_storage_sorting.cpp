#include "object_storage.hpp"

#include <engine/debug.inl.hpp>

#include <random>

#include "atomic_id_gen.inl.hpp"



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace SKENGINE_NAME_NS {

	namespace objstg { // Used here, defined elsewhere

		std::pair<vkutil::Buffer, size_t> create_object_buffer(VmaAllocator, size_t count);

		std::pair<vkutil::Buffer, size_t> create_draw_cmd_template_buffer(VmaAllocator, size_t count);

	}



	namespace {

		constexpr float UNBOUND_DRAW_BATCH_LOAD_FAC = 8.0;


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
				buffer = objstg::create_draw_cmd_template_buffer(vma, batches.size());
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


		void enqueue_mtx_assembly_job(
			ObjectStorage::MatrixAssembler& mtxAssembler,
			dev::Object* dst,
			const std::pair<skengine::Object, std::vector<skengine::BoneInstance>>& src_obj,
			const Bone& src_bone,
			const BoneInstance& src_bone_instance
		) {
			ObjectStorage::MatrixAssembler::Job job;
			job.position  = { src_obj.first.position_xyz,  src_bone.position_xyz,  src_bone_instance.position_xyz };
			job.direction = { src_obj.first.direction_ypr, src_bone.direction_ypr, src_bone_instance.direction_ypr };
			job.scale     = { src_obj.first.scale_xyz,     src_bone.scale_xyz,     src_bone_instance.scale_xyz };
			job.mesh      = { .cull_sphere = { src_bone.mesh.cull_sphere_xyzr } };
			job.dst = { &dst->model_transf, &dst->cull_sphere_xyzr };
			mtxAssembler.queue.push_back(job);
		}

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


	void ObjectStorage::removeObject(TransferContext transfCtx, ObjectId id) {
		assert(mVma != nullptr);

		auto  obj_iter = mObjects.find(id); if(obj_iter == mObjects.end()) throw BadId<ObjectId>(id);
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
				mObjectBuffer = objstg::create_object_buffer(mVma, new_instance_count_ceil);
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

			enqueue_mtx_assembly_job(*mMatrixAssembler, &obj, src_obj, bone, bone_instance);

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
