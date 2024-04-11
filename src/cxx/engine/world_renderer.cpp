#include "world_renderer.hpp"

#include "engine.hpp"
#include "debug.inl.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <tuple>
#include <type_traits>

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


		uint32_t set_light_buffer_capacity(VmaAllocator vma, WorldRenderer::LightStorage* dst, uint32_t desired) {
			static_assert(std::bit_ceil(0u) == 1u);
			desired = std::bit_ceil(desired);
			if(desired != dst->bufferCapacity) {
				if(dst->bufferCapacity > 0) {
					dst->buffer.unmap(vma);
					debug::destroyedBuffer(dst->buffer, "host-writeable light storage");
					vkutil::ManagedBuffer::destroy(vma, dst->buffer);
				}

				auto  bc_info = light_storage_create_info(desired);
				auto& ac_info = light_storage_allocate_info;
				dst->buffer    = vkutil::ManagedBuffer::create(vma, bc_info, ac_info);
				dst->mappedPtr = dst->buffer.map<dev::Light>(vma);
				debug::createdBuffer(dst->buffer, "host-writeable light storage");

				dst->bufferCapacity = desired;
			}
			return desired;
		}


		void update_light_storage_dset(VkDevice dev, VkBuffer buffer, size_t lightCount, VkDescriptorSet dset) {
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



	#define B_(BINDING_, DSET_N_, DSET_T_, STAGES_) VkDescriptorSetLayoutBinding { .binding = BINDING_, .descriptorType = DSET_T_, .descriptorCount = DSET_N_, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
	constexpr VkDescriptorSetLayoutBinding dset_layout_bindings[] = {
	B_(Engine::DIFFUSE_TEX_BINDING,  1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
	B_(Engine::NORMAL_TEX_BINDING,   1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
	B_(Engine::SPECULAR_TEX_BINDING, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
	B_(Engine::EMISSIVE_TEX_BINDING, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
	B_(Engine::MATERIAL_UBO_BINDING, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT) };
	#undef B_

	constexpr auto world_renderer_subpass_info = Renderer::Info {
		.dsetLayoutBindings = Renderer::Info::DsetLayoutBindings::referenceTo(dset_layout_bindings),
		.shaderReq = { .name = "default", .pipelineLayout = PipelineLayoutId::e3d },
		.rpass = Renderer::RenderPass::eWorld };


	WorldRenderer::WorldRenderer(): Renderer(world_renderer_subpass_info) { mState.initialized = false; }

	WorldRenderer::WorldRenderer(WorldRenderer&& mv):
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
			std::shared_ptr<ObjectStorage> objectStorage
	) {
		WorldRenderer r;
		r.mState.logger = std::move(logger);
		r.mState.objectStorage = std::move(objectStorage);
		r.mState.viewPosXyz = { };
		r.mState.viewDirYpr = { };
		r.mState.ambientLight = { };
		r.mState.lightStorage = { };
		r.mState.lightStorageOod = true;
		r.mState.lightStorageDsetOod = true;
		r.mState.viewTransfCacheOod = true;
		r.mState.initialized = true;
		set_light_buffer_capacity(r.vma(), &r.mState.lightStorage, 0);
		return r;
	}


	void WorldRenderer::destroy(WorldRenderer& r) {
		assert(r.mState.initialized);
		auto vma = r.vma();

		for(auto& gf : r.mState.gframes) {
			vkutil::ManagedBuffer::destroy(vma, gf.lightStorage);
		}
		r.mState.gframes.clear();

		if(r.mState.lightStorage.bufferCapacity > 0) {
			r.mState.lightStorage.bufferCapacity = 0;
			r.mState.lightStorage.buffer.unmap(vma);
			vkutil::ManagedBuffer::destroy(vma, r.mState.lightStorage.buffer);
		}

		{
			std::vector<ObjectId> removeList;
			removeList.reserve(r.mState.pointLights.size() + r.mState.rayLights.size());
			for(auto& l : r.mState.pointLights) removeList.push_back(l.first);
			for(auto& l : r.mState.rayLights  ) removeList.push_back(l.first);
			for(auto  l : removeList) r.removeLight(l);
		}

		r.mState.initialized = false;
	}


	void WorldRenderer::afterSwapchainCreation(ConcurrentAccess&, unsigned gframeCount) {
		uint32_t lightCapacity = mState.lightStorage.bufferCapacity;
		auto vma = this->vma();

		size_t oldGframeCount = mState.gframes.size();

		vkutil::BufferCreateInfo lightStorageBcInfo = {
			.size  = lightCapacity * sizeof(dev::Light),
			.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.qfamSharing = { } };

		auto setFrameLightStorage = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];
			wgf.lightStorage = vkutil::ManagedBuffer::createStorageBuffer(vma, lightStorageBcInfo);
			debug::createdBuffer(wgf.lightStorage, "device-readable light storage");
			wgf.lightStorageCapacity = lightCapacity;
			mState.lightStorageDsetOod = true;
		};

		auto unsetFrameLightStorage = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];
			debug::destroyedBuffer(wgf.lightStorage, "device-readable light storage");
			vkutil::ManagedBuffer::destroy(vma, wgf.lightStorage);
			wgf.lightStorageCapacity = 0;
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
			set_light_buffer_capacity(vma, &mState.lightStorage, new_ls_size);

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

			mState.lightStorage.buffer.flush(vma);
			mState.lightStorageDsetOod = true;
			mState.lightStorageOod = false;
		}

		ubo.ambient_lighting  = glm::vec4((all > 0)? glm::normalize(al) : al, all);
		ubo.view_transf       = getViewTransf();
		ubo.view_pos          = glm::inverse(ubo.view_transf) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		ubo.projview_transf   = ubo.proj_transf * ubo.view_transf;
		ubo.ray_light_count   = ls.rayCount;
		ubo.point_light_count = ls.pointCount;

		mState.objectStorage->commitObjects(cmd);

		bool buffer_resized = wgf.lightStorageCapacity != ls.bufferCapacity;
		if(buffer_resized) {
			mState.logger->trace("Resizing light storage: {} -> {}", wgf.lightStorageCapacity, ls.bufferCapacity);
			debug::destroyedBuffer(wgf.lightStorage, "device-readable light storage");
			vkutil::ManagedBuffer::destroy(vma, wgf.lightStorage);

			vkutil::BufferCreateInfo bc_info = { };
			bc_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bc_info.size  = ls.bufferCapacity * sizeof(dev::Light);
			wgf.lightStorage = vkutil::ManagedBuffer::createStorageBuffer(vma, bc_info);
			debug::createdBuffer(wgf.lightStorage, "device-readable light storage");

			#warning "TODO: Renderer-owned descriptor sets, or something"

			mState.lightStorageDsetOod = true;
			wgf.lightStorageCapacity = ls.bufferCapacity;
		}

		if(mState.lightStorageDsetOod) {
			update_light_storage_dset(dev, wgf.lightStorage.value, wgf.lightStorageCapacity, egf.frame_dset);
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
		auto& objStorage     = * mState.objectStorage.get();
		auto batches         = objStorage.getDrawBatches();
		auto instance_buffer = objStorage.getInstanceBuffer();
		auto batch_buffer    = objStorage.getDrawCommandBuffer();

		if(batches.empty()) return;

		objStorage.waitUntilReady();

		VkDescriptorSet dsets[]  = { egf.frame_dset, { } };
		ModelId         last_mdl = ModelId    (~ model_id_e    (batches.front().model_id));
		MaterialId      last_mat = MaterialId (~ material_id_e (batches.front().material_id));
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ca.getPipeline(pipelineInfo().shaderReq));
		for(VkDeviceSize i = 0; const auto& batch : batches) {
			auto* model = objStorage.getModel(batch.model_id);
			assert(model != nullptr);
			if(batch.model_id != last_mdl) {
				VkBuffer vtx_buffers[] = { model->vertices.value, instance_buffer };
				constexpr VkDeviceSize offsets[] = { 0, 0 };
				vkCmdBindIndexBuffer(cmd, model->indices.value, 0, VK_INDEX_TYPE_UINT32);
				vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
			}
			if(batch.material_id != last_mat) {
				auto mat = objStorage.getMaterial(batch.material_id);
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

}



#undef assert_not_end_
#undef assert_not_nullptr_
