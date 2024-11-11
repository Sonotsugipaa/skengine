#include "world_renderer.hpp"

#include "engine.hpp"
#include "shader_cache.hpp"
#include "debug.inl.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <tuple>
#include <concepts>
#include <random>

#include "atomic_id_gen.inl.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#include <vk-util/error.hpp>



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace SKENGINE_NAME_NS {

	namespace world {

		// Define elsewhere
		VkPipeline create3dPipeline(
			VkDevice,
			ShaderCacheInterface&,
			const WorldRenderer::PipelineParameters& plParams,
			VkRenderPass,
			VkPipelineCache,
			VkPipelineLayout,
			uint32_t subpass );

	}



	namespace world { namespace {

		#define B_(BINDING_, DSET_N_, DSET_T_, STAGES_) VkDescriptorSetLayoutBinding { .binding = BINDING_, .descriptorType = DSET_T_, .descriptorCount = DSET_N_, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
		constexpr VkDescriptorSetLayoutBinding world_dset_layout_bindings[] = {
		B_(WorldRenderer::DIFFUSE_TEX_BINDING,  1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::NORMAL_TEX_BINDING,   1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::SPECULAR_TEX_BINDING, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::EMISSIVE_TEX_BINDING, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::MATERIAL_UBO_BINDING, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT) };
		#undef B_

		#define PI_ Renderer::PipelineInfo
		constexpr auto world_renderer_subpass_info = PI_ {
			.dsetLayoutBindings = PI_::DsetLayoutBindings::referenceTo(world_dset_layout_bindings) };
		#undef PI_

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


		template <std::unsigned_integral T>
		constexpr T compute_buffer_resize(T current, T desired) noexcept {
			return std::bit_ceil((desired > current? desired : current));
		}


		uint32_t set_light_buffer_capacity(VmaAllocator vma, WorldRenderer::LightStorage* dst, uint32_t desired) {
			desired = compute_buffer_resize(dst->bufferCapacity, desired);
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
			wr.dstBinding      = WorldRenderer::LIGHT_STORAGE_BINDING;
			wr.pBufferInfo     = &db_info;
			vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
		}


		VkDescriptorPool create_gframe_dpool(VkDevice dev, const std::vector<WorldRenderer::GframeData>& gframes) {
			auto gframeCount = uint32_t(gframes.size());

			VkDescriptorPoolSize sizes[] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, gframeCount * 1 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gframeCount * 1 } };

			VkDescriptorPoolCreateInfo dpc_info = { };
			dpc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			dpc_info.poolSizeCount = std::size(sizes);
			dpc_info.pPoolSizes    = sizes;
			dpc_info.maxSets = gframeCount;

			VkDescriptorPool r;
			VK_CHECK(vkCreateDescriptorPool, dev, &dpc_info, nullptr, &r);
			return r;
		}

	}}



	WorldRenderer::WorldRenderer(): Renderer(world::world_renderer_subpass_info), mState{} { }

	WorldRenderer::WorldRenderer(WorldRenderer&& mv):
		Renderer(world::world_renderer_subpass_info),
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
			Logger logger,
			std::shared_ptr<WorldRendererSharedState> sharedState,
			std::shared_ptr<ObjectStorage> objectStorage,
			const ProjectionInfo& projInfo,
			const PipelineParameters& plParams
	) {
		WorldRenderer r;
		r.mState = { };
		r.mState.logger = std::move(logger);
		r.mState.objectStorage = std::move(objectStorage);
		r.mState.sharedState = std::move(sharedState);
		r.mState.pipelineParams = plParams;
		r.mState.pipeline = nullptr;
		r.mState.viewTransfCacheOod = true;
		r.mState.lightStorageOod = true;
		r.mState.lightStorageDsetOod = true;
		r.mState.initialized = true;

		r.setProjection(projInfo);

		world::set_light_buffer_capacity(r.vma(), &r.mState.lightStorage, 0);

		return r;
	}


	void WorldRenderer::destroy(WorldRenderer& r) {
		assert(r.mState.initialized);
		auto vma = r.vma();
		auto dev = vmaGetAllocatorDevice(vma);

		for(auto& gf : r.mState.gframes) {
			debug::destroyedBuffer(gf.frameUbo, "gframe UBO");
			vkutil::BufferDuplex::destroy(vma, gf.frameUbo);
			debug::destroyedBuffer(gf.frameUbo, "device-readable light storage");
			vkutil::ManagedBuffer::destroy(vma, gf.lightStorage);
		}
		r.mState.gframes.clear();

		vkDestroyDescriptorPool(dev, r.mState.gframeDpool, nullptr);

		if(r.mState.lightStorage.bufferCapacity > 0) {
			r.mState.lightStorage.bufferCapacity = 0;
			r.mState.lightStorage.buffer.unmap(vma);
			debug::destroyedBuffer(r.mState.lightStorage.buffer, "host-writeable light storage");
			vkutil::ManagedBuffer::destroy(vma, r.mState.lightStorage.buffer);
		}

		{
			std::vector<ObjectId> removeList;
			removeList.reserve(r.mState.pointLights.size() + r.mState.rayLights.size());
			for(auto& l : r.mState.pointLights) removeList.push_back(l.first);
			for(auto& l : r.mState.rayLights  ) removeList.push_back(l.first);
			for(auto  l : removeList) r.removeLight(l);
		}

		if(r.mState.pipeline != nullptr) vkDestroyPipeline(dev, r.mState.pipeline, nullptr);
		r.mState.initialized = false;
	}


	void WorldRenderer::initSharedState(VkDevice dev, WorldRendererSharedState& wrss) {
		VkDescriptorSetLayoutBinding dslb[5] = { };
		wrss = { }; // Zero-initialization is important in case of failure

		try {
			dslb[0].binding = DIFFUSE_TEX_BINDING;
			dslb[0].descriptorCount = 1;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			dslb[1] = dslb[0];
			dslb[1].binding = NORMAL_TEX_BINDING;
			dslb[2] = dslb[0];
			dslb[2].binding = SPECULAR_TEX_BINDING;
			dslb[3] = dslb[0];
			dslb[3].binding = EMISSIVE_TEX_BINDING;
			dslb[4] = dslb[0];
			dslb[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			dslb[4].binding = MATERIAL_UBO_BINDING;

			VkDescriptorSetLayoutCreateInfo dslc_info = { };
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslc_info.bindingCount = std::size(dslb);
			dslc_info.pBindings = dslb;
			VK_CHECK(vkCreateDescriptorSetLayout, dev, &dslc_info, nullptr, &wrss.materialDsetLayout);

			dslb[0] = { };
			dslb[0].binding = FRAME_UBO_BINDING;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			dslb[0].descriptorCount = 1;
			dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			dslb[1] = dslb[0];
			dslb[1].binding = LIGHT_STORAGE_BINDING;
			dslb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

			dslc_info.bindingCount = 2;
			VK_CHECK(vkCreateDescriptorSetLayout, dev, &dslc_info, nullptr, &wrss.gframeUboDsetLayout);

			{ // Pipeline layout
				VkPipelineLayoutCreateInfo plcInfo = { };
				VkDescriptorSetLayout layouts[] = {
					wrss.gframeUboDsetLayout,
					wrss.materialDsetLayout };
				plcInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				plcInfo.setLayoutCount = std::size(layouts);
				plcInfo.pSetLayouts    = layouts;
				VK_CHECK(vkCreatePipelineLayout, dev, &plcInfo, nullptr, &wrss.pipelineLayout);
			}
		} catch(...) {
			destroySharedState(dev, wrss);
			std::rethrow_exception(std::current_exception());
		}
	}


	void WorldRenderer::destroySharedState(VkDevice dev, WorldRendererSharedState& wrss) {
		if(wrss.pipelineLayout) vkDestroyPipelineLayout(dev, wrss.pipelineLayout, nullptr);
		if(wrss.gframeUboDsetLayout) vkDestroyDescriptorSetLayout(dev, wrss.gframeUboDsetLayout, nullptr);
		if(wrss.materialDsetLayout) vkDestroyDescriptorSetLayout(dev, wrss.materialDsetLayout, nullptr);
	}


	void WorldRenderer::prepareSubpasses(const SubpassSetupInfo& ssInfo, VkPipelineCache plCache, ShaderCacheInterface* shCache) {
		assert(mState.pipeline == nullptr);
		mState.pipeline = world::create3dPipeline(
			vmaGetAllocatorDevice(mState.objectStorage->vma()),
			*shCache, mState.pipelineParams,
			ssInfo.rpass, plCache, mState.sharedState->pipelineLayout, 0 );
	}


	void WorldRenderer::forgetSubpasses(const SubpassSetupInfo&) {
		vkDestroyPipeline(vmaGetAllocatorDevice(mState.objectStorage->vma()), mState.pipeline, nullptr);
		mState.pipeline = nullptr;
	}


	void WorldRenderer::afterSwapchainCreation(ConcurrentAccess& ca, unsigned gframeCount) {
		auto& e   = ca.engine();
		auto  dev = e.getDevice();
		auto  vma = this->vma();

		size_t oldGframeCount = mState.gframes.size();

		auto createGframeData = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];

			uint32_t lightCapacity = mState.lightStorage.bufferCapacity;
			if(lightCapacity > 0) { // Create the light storage
				vkutil::BufferCreateInfo lightStorageBcInfo = {
					.size  = lightCapacity * sizeof(dev::Light),
					.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					.qfamSharing = { } };
				wgf.lightStorage = vkutil::ManagedBuffer::createStorageBuffer(vma, lightStorageBcInfo);
				debug::createdBuffer(wgf.lightStorage, "device-readable light storage");
				wgf.lightStorageCapacity = lightCapacity;
				mState.lightStorageDsetOod = true;
			}

			{ // Create the frame UBO
				vkutil::BufferCreateInfo ubo_bc_info = {
					.size  = sizeof(dev::FrameUniform),
					.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					.qfamSharing = { } };
				wgf.frameUbo = vkutil::BufferDuplex::createUniformBuffer(vma, ubo_bc_info);
				debug::createdBuffer(wgf.frameUbo, "gframe UBO");
			}
		};

		auto destroyGframeData = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];

			debug::destroyedBuffer(wgf.frameUbo, "gframe UBO");
			vkutil::BufferDuplex::destroy(vma, wgf.frameUbo);

			uint32_t lightCapacity = mState.lightStorage.bufferCapacity;
			if(lightCapacity > 0) {
				debug::destroyedBuffer(wgf.lightStorage, "device-readable light storage");
				vkutil::ManagedBuffer::destroy(vma, wgf.lightStorage);
				wgf.lightStorageCapacity = 0;
			}
		};

		if(oldGframeCount != gframeCount) {
			if(oldGframeCount < gframeCount) {
				mState.gframes.resize(gframeCount);
				for(size_t i = oldGframeCount; i < gframeCount; ++i) createGframeData(i);
			} else /* if(oldGframeCount > gframeCount) */ {
				for(size_t i = gframeCount; i < oldGframeCount; ++i) destroyGframeData(i);
				mState.gframes.resize(gframeCount);
			}

			if(oldGframeCount > 0 || gframeCount == 0) {
				vkDestroyDescriptorPool(dev, mState.gframeDpool, nullptr);
			}

			if(gframeCount > 0) { // Create the gframe dpool, then allocate and write dsets
				mState.gframeDpool = world::create_gframe_dpool(dev, mState.gframes);
				mState.projTransfOod = true;

				VkDescriptorSetAllocateInfo dsa_info = { };
				VkDescriptorSetLayout dsetLayouts[] = { mState.sharedState->gframeUboDsetLayout };
				dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				dsa_info.descriptorPool     = mState.gframeDpool;
				dsa_info.descriptorSetCount = std::size(dsetLayouts);
				dsa_info.pSetLayouts        = dsetLayouts;
				VkWriteDescriptorSet frame_dset_wr = { };
				frame_dset_wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				frame_dset_wr.dstBinding = FRAME_UBO_BINDING;
				frame_dset_wr.descriptorCount = 1;
				frame_dset_wr.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				VkDescriptorBufferInfo frame_db_info = { nullptr, 0, VK_WHOLE_SIZE };

				for(auto& wgf : mState.gframes) {
					VK_CHECK(vkAllocateDescriptorSets, dev, &dsa_info, &wgf.frameDset);
					frame_dset_wr.pBufferInfo = &frame_db_info;
					frame_dset_wr.dstSet = wgf.frameDset;
					frame_db_info.buffer = wgf.frameUbo;
					vkUpdateDescriptorSets(dev, 1, &frame_dset_wr, 0, nullptr);
				}
			}
		}
	}


	void WorldRenderer::duringPrepareStage(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		auto& e   = ca.engine();
		auto& egf = ca.getGframeData(drawInfo.gframeIndex);
		auto& wgf = mState.gframes[drawInfo.gframeIndex];
		auto& ls  = lightStorage();
		auto& al  = getAmbientLight();
		auto& ubo = * wgf.frameUbo.mappedPtr<dev::FrameUniform>();
		auto& renderExtent = ca.engine().getRenderExtent();
		auto  dev = e.getDevice();
		auto  vma = e.getVmaAllocator();
		auto  all = glm::length(al);

		if(mState.lightStorageOod) {
			uint32_t new_ls_size = mState.rayLights.size() + mState.pointLights.size();
			world::set_light_buffer_capacity(vma, &mState.lightStorage, new_ls_size);

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

		auto rng  = std::minstd_rand(std::chrono::steady_clock::now().time_since_epoch().count());
		auto dist = std::uniform_real_distribution(0.0f, 1.0f);
		ubo.shade_step_count  = e.getPreferences().shade_step_count;
		ubo.shade_step_smooth = e.getPreferences().shade_step_smoothness;
		ubo.shade_step_exp    = e.getPreferences().shade_step_exponent;
		ubo.dithering_steps   = e.getPreferences().dithering_steps;
		ubo.rnd               = dist(rng);
		ubo.time_delta        = std::float32_t(egf.frame_delta);
		ubo.flags             = dev::FrameUniformFlags(dev::FRAME_UNI_ZERO);
		ubo.ambient_lighting  = glm::vec4((all > 0)? glm::normalize(al) : al, all);
		ubo.view_transf       = getViewTransf();
		ubo.view_pos          = glm::inverse(ubo.view_transf) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		ubo.projview_transf   = ubo.proj_transf * ubo.view_transf;
		ubo.ray_light_count   = ls.rayCount;
		ubo.point_light_count = ls.pointCount;
		if(wgf.lastRenderExtent != renderExtent) [[unlikely]] {
			wgf.lastRenderExtent = renderExtent;
			mState.projTransfOod = true;
		}
		if(mState.projTransfOod) [[unlikely]] {
			float aspectRatio = double(renderExtent.width) / double(renderExtent.height);
			ubo.proj_transf = glm::perspective<float>(mState.projInfo.verticalFov, aspectRatio, mState.projInfo.zNear, mState.projInfo.zFar);
			ubo.proj_transf[1][1] *= -1.0f; // Clip +y is view -y
			ubo.projview_transf = ubo.proj_transf * ubo.view_transf;
		}
		wgf.frameUbo.flush(cmd, vma);

		mState.objectStorage->commitObjects(cmd);

		bool buffer_resized = wgf.lightStorageCapacity != ls.bufferCapacity;
		if(buffer_resized) {
			mState.logger.trace("Resizing light storage: {} -> {}", wgf.lightStorageCapacity, ls.bufferCapacity);
			debug::destroyedBuffer(wgf.lightStorage, "device-readable light storage");
			vkutil::ManagedBuffer::destroy(vma, wgf.lightStorage);

			vkutil::BufferCreateInfo bc_info = { };
			bc_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bc_info.size  = ls.bufferCapacity * sizeof(dev::Light);
			wgf.lightStorage = vkutil::ManagedBuffer::createStorageBuffer(vma, bc_info);
			debug::createdBuffer(wgf.lightStorage, "device-readable light storage");

			mState.lightStorageDsetOod = true;
			wgf.lightStorageCapacity = ls.bufferCapacity;
		}

		if(mState.lightStorageDsetOod) {
			world::update_light_storage_dset(dev, wgf.lightStorage.value, wgf.lightStorageCapacity, wgf.frameDset);
			mState.lightStorageDsetOod = false;
		}

		if(true /* Optimizable, but not worth the effort */) {
			VkBufferCopy cp = { };
			cp.size = (ls.rayCount + ls.pointCount) * sizeof(dev::Light);
			vkCmdCopyBuffer(cmd, ls.buffer.value, wgf.lightStorage, 1, &cp);
		}
	}


	void WorldRenderer::duringDrawStage(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		assert(drawInfo.gframeIndex < mState.gframes.size());
		auto& objStorage     = * mState.objectStorage.get();
		auto& wgf            = mState.gframes[drawInfo.gframeIndex];
		auto batches         = objStorage.getDrawBatches();
		auto instance_buffer = objStorage.getInstanceBuffer();
		auto batch_buffer    = objStorage.getDrawCommandBuffer();

		if(batches.empty()) return;

		auto& renderExtent = ca.engine().getRenderExtent();
		VkViewport viewport = { }; {
			viewport.x      = 0.0f;
			viewport.y      = 0.0f;
			viewport.width  = renderExtent.width;
			viewport.height = renderExtent.height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
		}
		VkRect2D scissor = { }; {
			scissor.offset = { };
			scissor.extent = { renderExtent.width, renderExtent.height };
		}
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		objStorage.waitUntilReady();

		VkDescriptorSet dsets[]  = { wgf.frameDset, { } };
		ModelId         last_mdl = ModelId    (~ model_id_e    (batches.front().model_id));
		MaterialId      last_mat = MaterialId (~ material_id_e (batches.front().material_id));
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mState.pipeline);
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
				dsets[MATERIAL_DSET_LOC] = mat->dset;
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mState.sharedState->pipelineLayout, 0, std::size(dsets), dsets, 0, nullptr);
			}
			vkCmdDrawIndexedIndirect(
				cmd, batch_buffer,
				i * sizeof(VkDrawIndexedIndirectCommand), 1,
				sizeof(VkDrawIndexedIndirectCommand) );
			++ i;
		}
	}


	void WorldRenderer::afterRenderPass(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		{ // Barrier the color attachment image for transfer
			VkImageMemoryBarrier2 imb { };
			imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imb.subresourceRange.layerCount = 1;
			imb.subresourceRange.levelCount = 1;
			imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			imb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			imb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			imb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imb.image = ca.getGframeData(drawInfo.gframeIndex).atch_color;
			VkDependencyInfo imbDep = { };
			imbDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			imbDep.pImageMemoryBarriers = &imb;
			imbDep.imageMemoryBarrierCount = 1;
			vkCmdPipelineBarrier2(cmd, &imbDep);
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
