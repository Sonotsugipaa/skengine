#include "world_renderer.hpp"

#include <engine/engine.hpp>
#include <engine/shader_cache.hpp>

#include <bit>
#include <cassert>
#include <atomic>
#include <tuple>
#include <concepts>

#include "atomic_id_gen.inl.hpp"

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

	namespace world { // Used here, defined elsewhere

		void computeCullWorkgroupSizes(uint32_t dst[3], const VkPhysicalDeviceProperties& props);

		VkPipeline create3dPipeline(
			VkDevice,
			ShaderCacheInterface&,
			const WorldRenderer::PipelineParameters& plParams,
			VkRenderPass,
			VkPipelineCache,
			VkPipelineLayout,
			uint32_t subpass );

		VkPipeline createCullPipeline(
			VkDevice dev,
			VkPipelineCache plCache,
			VkPipelineLayout plLayout,
			const VkPhysicalDeviceProperties& phDevProps );

	}



	namespace world { // Defined here, used elsewhere

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


		void update_light_storage_dset(VkDevice dev, VkBuffer buffer, size_t lightCount, VkDescriptorSet dset) {
			VkDescriptorBufferInfo db_info = { };
			db_info.buffer = buffer;
			db_info.range  = lightCount * sizeof(dev::Light);
			VkWriteDescriptorSet wr = { };
			wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			wr.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			wr.descriptorCount = 1;
			wr.dstSet          = dset;
			wr.dstBinding      = WorldRenderer::RDR_LIGHT_STORAGE_BINDING;
			wr.pBufferInfo     = &db_info;
			vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
		}


		std::pair<vkutil::Buffer, size_t> create_obj_buffer(VmaAllocator vma, size_t count) {
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = std::bit_ceil(count) * sizeof(dev::Object);
			bc_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			vkutil::AllocationCreateInfo ac_info = { };
			ac_info.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			ac_info.vmaUsage         = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
			auto r = vkutil::Buffer::create(vma, bc_info, ac_info);
			return { std::move(r), count };
		}

		std::pair<vkutil::Buffer, size_t> create_obj_id_buffer(VmaAllocator vma, size_t count) {
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = std::bit_ceil(count) * sizeof(dev::ObjectId);
			bc_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			vkutil::AllocationCreateInfo ac_info = { };
			ac_info.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			ac_info.vmaUsage         = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
			auto r = vkutil::Buffer::create(vma, bc_info, ac_info);
			return { std::move(r), count };
		}

		std::pair<vkutil::Buffer, size_t> create_draw_cmd_buffer(VmaAllocator vma, size_t count) {
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = std::bit_ceil(count) * sizeof(VkDrawIndexedIndirectCommand);
			bc_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			vkutil::AllocationCreateInfo ac_info = { };
			ac_info.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			ac_info.vmaUsage         = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
			auto r = vkutil::Buffer::create(vma, bc_info, ac_info);
			return { std::move(r), count };
		}

		void resize_obj_buffer(VmaAllocator vma, std::pair<vkutil::Buffer, size_t>* dst, size_t requiredCmdCount) {
			if(dst->second < requiredCmdCount) {
				if(dst->first.value != nullptr) vkutil::Buffer::destroy(vma, dst->first);
				*dst = create_obj_buffer(vma, requiredCmdCount);
			}
		}

		void resize_obj_id_buffer(VmaAllocator vma, std::pair<vkutil::Buffer, size_t>* dst, size_t requiredCmdCount) {
			if(dst->second < requiredCmdCount) {
				if(dst->first.value != nullptr) vkutil::Buffer::destroy(vma, dst->first);
				*dst = create_obj_id_buffer(vma, requiredCmdCount);
			}
		}

		void resize_draw_cmd_buffer(VmaAllocator vma, std::pair<vkutil::Buffer, size_t>* dst, size_t requiredCmdCount) {
			if(dst->second < requiredCmdCount) {
				if(dst->first.value != nullptr) vkutil::Buffer::destroy(vma, dst->first);
				*dst = create_draw_cmd_buffer(vma, requiredCmdCount);
			}
		}

	}



	namespace world { namespace {

		#define B_(BINDING_, DSET_N_, DSET_T_, STAGES_) VkDescriptorSetLayoutBinding { .binding = BINDING_, .descriptorType = DSET_T_, .descriptorCount = DSET_N_, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
		constexpr VkDescriptorSetLayoutBinding world_dset_layout_bindings[] = {
		B_(WorldRenderer::RDR_DIFFUSE_TEX_BINDING,  1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::RDR_NORMAL_TEX_BINDING,   1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::RDR_SPECULAR_TEX_BINDING, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::RDR_EMISSIVE_TEX_BINDING, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
		B_(WorldRenderer::RDR_MATERIAL_UBO_BINDING, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT) };
		#undef B_

		#define PI_ Renderer::PipelineInfo
		constexpr auto world_renderer_subpass_info = PI_ {
			.dsetLayoutBindings = PI_::DsetLayoutBindings::referenceTo(world_dset_layout_bindings) };
		#undef PI_


		VkDescriptorPool create_gframe_dpool(VkDevice dev, const std::vector<WorldRenderer::GframeData>& gframes, uint32_t objStgCount) {
			auto gframeCount = uint32_t(gframes.size());

			VkDescriptorPoolSize sizes[] = {
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					(gframeCount * 1)+
					(gframeCount * 1 * objStgCount) },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					(gframeCount * 1) +
					(gframeCount * 3 * objStgCount) } };

			VkDescriptorPoolCreateInfo dpc_info = { };
			dpc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			dpc_info.poolSizeCount = std::size(sizes);
			dpc_info.pPoolSizes    = sizes;
			dpc_info.maxSets = gframeCount + (gframeCount * objStgCount);

			VkDescriptorPool r;
			VK_CHECK(vkCreateDescriptorPool, dev, &dpc_info, nullptr, &r);
			return r;
		}


		void validate_params(WorldRenderer::RdrParams& params) {
			#define MAX_(M_, MAX_) { params.M_ = std::max<decltype(WorldRenderer::RdrParams::M_)>(MAX_, params.M_); }
			MAX_(shadeStepCount, 0)
			MAX_(ditheringSteps,  0)
			#undef MAX_

			#define UL_ [[unlikely]]
			if(params.shadeStepSmoothness < 0.0f) UL_ params.shadeStepSmoothness = -1.0f - (-1.0f / -(-1.0f + params.shadeStepSmoothness)); // Negative values (interval (-1, 0)) behave strangely
			#undef UL_
		}


		vkutil::BufferDuplex create_cull_pass_ubo(VmaAllocator vma) {
			vkutil::BufferCreateInfo bc_info = { };
			bc_info.size  = sizeof(dev::CullPassUbo);
			bc_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			vkutil::AllocationCreateInfo ac_info = { };
			ac_info.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			ac_info.vmaUsage          = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
			auto r = vkutil::BufferDuplex::create(vma, bc_info, ac_info, vkutil::HostAccess::eWr);
			return r;
		}


		auto destroy_gframe_data(VmaAllocator vma, WorldRenderer::GframeData& gframeData) {
			for(auto& b : gframeData.osData) {
				vkutil::BufferDuplex::destroy(vma, b.cullPassUbo);
				vkutil::Buffer::destroy(vma, b.objBfCopy.first);
				vkutil::Buffer::destroy(vma, b.objIdBfCopy.first);
				vkutil::Buffer::destroy(vma, b.drawCmdBfCopy.first);
			}
			vkutil::BufferDuplex::destroy(vma, gframeData.frameUbo);

			if(gframeData.lightStorageCapacity > 0) {
				vkutil::ManagedBuffer::destroy(vma, gframeData.lightStorage);
				gframeData.lightStorageCapacity = 0;
			}
		};

	}}



	const WorldRenderer::RdrParams WorldRenderer::RdrParams::defaultParams = WorldRenderer::RdrParams {
		.fovY                        = glm::radians(110.0f),
		.zNear                       = 1.0f / float(1 << 6),
		.zFar                        = float(1 << 10),
		.shadeStepCount              = 0,
		.pointLightDistanceThreshold = 1.0f / 256.0f /* Good enough for 24-bit colors, I'd hope */,
		.shadeStepSmoothness         = 0.0f,
		.shadeStepExponent           = 1.0f,
		.ditheringSteps              = 256.0f,
		.cullingEnabled              = true
	};


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
			VmaAllocator vma,
			RdrParams rdrParams,
			std::shared_ptr<WorldRendererSharedState> sharedState,
			std::shared_ptr<std::vector<ObjectStorage>> objectStorages,
			const ProjectionInfo& projInfo,
			util::TransientArray<PipelineParameters> plParams
	) {
		WorldRenderer r;
		world::validate_params(rdrParams);
		r.mState = { };
		r.mState.logger = std::move(logger);
		r.mState.vma = vma;
		r.mState.params = std::move(rdrParams);
		r.mState.objectStorages = std::move(objectStorages);
		r.mState.sharedState = std::move(sharedState);
		r.mState.rtargetId = idgen::invalidId<RenderTargetId>();
		r.mState.viewTransfCacheOod = true;
		r.mState.lightStorageOod = true;
		r.mState.lightStorageDsetsOod = true;
		r.mState.initialized = true;

		r.mState.pipelineParams = plParams;
		assert(r.mState.pipelineParams.ownsMemory() /* This SHOULD always be the case for the TransientArray copy-operator, but ynk */);

		r.setProjection(projInfo);

		world::set_light_buffer_capacity(r.vma(), &r.mState.lightStorage, 0);

		return r;
	}


	void WorldRenderer::destroy(WorldRenderer& r) {
		assert(r.mState.initialized);
		auto vma = r.vma();
		auto dev = vmaGetAllocatorDevice(vma);

		for(auto& gf : r.mState.gframes) world::destroy_gframe_data(vma, gf);
		r.mState.gframes.clear();

		vkDestroyDescriptorPool(dev, r.mState.gframeDpool, nullptr);

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

		for(auto pl : r.mState.rdrPipelines) {
			if(pl != nullptr) vkDestroyPipeline(dev, pl, nullptr);
		}
		r.mState.initialized = false;
	}


	void WorldRenderer::initSharedState(VkDevice dev, WorldRendererSharedState& wrss) {
		VkDescriptorSetLayoutBinding dslb[5];
		VkDescriptorSetLayoutCreateInfo dslc_info = { };
		wrss = { }; // Zero-initialization is important in case of failure

		try {
			dslc_info.pBindings = dslb;
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

			dslb[0] = { };
			dslb[0].binding = CULL_OBJ_STG_BINDING;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			dslb[0].descriptorCount = 1;
			dslb[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
			dslb[1] = dslb[0];
			dslb[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			dslb[1].binding = CULL_OBJ_ID_STG_BINDING;
			dslb[2] = dslb[1];
			dslb[2].binding = CULL_CMD_BINDING;
			dslb[3] = dslb[1];
			dslb[3].binding = CULL_UBO_BINDING;
			dslb[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			static_assert(CULL_OBJ_STG_BINDING    == RDR_OBJ_STG_BINDING);    // The same layout is reused between the two stages
			static_assert(CULL_OBJ_ID_STG_BINDING == RDR_OBJ_ID_STG_BINDING); // ^^^

			dslc_info.bindingCount = 4; assert(dslc_info.bindingCount <= std::size(dslb));
			VK_CHECK(vkCreateDescriptorSetLayout, dev, &dslc_info, nullptr, &wrss.objDsetLayout);

			dslb[0] = { };
			dslb[0].binding = RDR_DIFFUSE_TEX_BINDING;
			dslb[0].descriptorCount = 1;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			dslb[1] = dslb[0];
			dslb[1].binding = RDR_NORMAL_TEX_BINDING;
			dslb[2] = dslb[0];
			dslb[2].binding = RDR_SPECULAR_TEX_BINDING;
			dslb[3] = dslb[0];
			dslb[3].binding = RDR_EMISSIVE_TEX_BINDING;
			dslb[4] = dslb[0];
			dslb[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			dslb[4].binding = RDR_MATERIAL_UBO_BINDING;

			dslc_info.bindingCount = 5; assert(dslc_info.bindingCount <= std::size(dslb));
			VK_CHECK(vkCreateDescriptorSetLayout, dev, &dslc_info, nullptr, &wrss.materialDsetLayout);

			dslb[0] = { };
			dslb[0].binding = RDR_FRAME_UBO_BINDING;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			dslb[0].descriptorCount = 1;
			dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			dslb[1] = dslb[0];
			dslb[1].binding = RDR_LIGHT_STORAGE_BINDING;
			dslb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

			dslc_info.bindingCount = 2; assert(dslc_info.bindingCount <= std::size(dslb));
			VK_CHECK(vkCreateDescriptorSetLayout, dev, &dslc_info, nullptr, &wrss.gframeUboDsetLayout);

			{ // Pipeline layout
				VkPipelineLayoutCreateInfo plcInfo = { };
				plcInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				VkPushConstantRange pcRanges[1] = { };
				auto mkLayout = [&]<typename... Dl>(VkPipelineLayout* dst, Dl... dl) {
					VkDescriptorSetLayout layouts[] = { dl... };
					plcInfo.setLayoutCount = std::size(layouts);
					plcInfo.pSetLayouts    = layouts;
					VK_CHECK(vkCreatePipelineLayout, dev, &plcInfo, nullptr, dst);
				};
				mkLayout(&wrss.rdrPipelineLayout, wrss.gframeUboDsetLayout, wrss.materialDsetLayout, wrss.objDsetLayout);
				pcRanges[0] = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) };
				plcInfo.pushConstantRangeCount = 1;
				plcInfo.pPushConstantRanges = pcRanges;
				mkLayout(&wrss.cullPassPipelineLayout, wrss.objDsetLayout);
			}
		} catch(...) {
			destroySharedState(dev, wrss);
			std::rethrow_exception(std::current_exception());
		}
	}


	void WorldRenderer::destroySharedState(VkDevice dev, WorldRendererSharedState& wrss) {
		if(wrss.cullPassPipelineLayout) vkDestroyPipelineLayout(dev, wrss.cullPassPipelineLayout, nullptr);
		if(wrss.rdrPipelineLayout) vkDestroyPipelineLayout(dev, wrss.rdrPipelineLayout, nullptr);
		if(wrss.gframeUboDsetLayout) vkDestroyDescriptorSetLayout(dev, wrss.gframeUboDsetLayout, nullptr);
		if(wrss.materialDsetLayout) vkDestroyDescriptorSetLayout(dev, wrss.materialDsetLayout, nullptr);
		if(wrss.objDsetLayout) vkDestroyDescriptorSetLayout(dev, wrss.objDsetLayout, nullptr);
	}


	void WorldRenderer::prepareSubpasses(const SubpassSetupInfo& ssInfo, VkPipelineCache plCache, ShaderCacheInterface* shCache) {
		assert(mState.rdrPipelines.empty());
		mState.rdrPipelines.reserve(mState.pipelineParams.size() + 1);
		mState.cullPassPipeline = world::createCullPipeline(
			vmaGetAllocatorDevice(vma()),
			plCache, mState.sharedState->cullPassPipelineLayout, *ssInfo.phDevProps );
		for(uint32_t subpassIdx = 0; auto& params : mState.pipelineParams) {
			mState.rdrPipelines.push_back(world::create3dPipeline(
				vmaGetAllocatorDevice(vma()),
				*shCache, params,
				ssInfo.rpass, plCache, mState.sharedState->rdrPipelineLayout, subpassIdx ++ ));
		}
	}


	void WorldRenderer::forgetSubpasses(const SubpassSetupInfo&) {
		auto dev = vmaGetAllocatorDevice(vma());
		vkDestroyPipeline(dev, mState.cullPassPipeline, nullptr);
		for(auto& pl : mState.rdrPipelines) {
			vkDestroyPipeline(dev, pl, nullptr);
			pl = nullptr;
		}
		mState.rdrPipelines.clear();
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
				wgf.lightStorageCapacity = lightCapacity;
				mState.lightStorageDsetsOod = true;
			}

			{ // Create the frame UBO
				vkutil::BufferCreateInfo ubo_bc_info = {
					.size  = sizeof(dev::FrameUniform),
					.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					.qfamSharing = { } };
				wgf.frameUbo = vkutil::BufferDuplex::createUniformBuffer(vma, ubo_bc_info);
			}

			// Create the buffers for draw command copies
			for(size_t i = 0; auto& os : *mState.objectStorages) {
				wgf.osData.push_back({ });
				auto& data = wgf.osData.back();
				world::resize_obj_buffer     (vma, &data.objBfCopy,     os.getDrawCount());
				world::resize_obj_id_buffer  (vma, &data.objIdBfCopy,   os.getDrawCount());
				world::resize_draw_cmd_buffer(vma, &data.drawCmdBfCopy, os.getDrawBatchCount());
				data.cullPassUbo = world::create_cull_pass_ubo(vma);
				++ i;
			}
		};

		if(oldGframeCount != gframeCount) {
			if(oldGframeCount < gframeCount) {
				mState.gframes.resize(gframeCount);
				for(size_t i = oldGframeCount; i < gframeCount; ++i) createGframeData(i);
			} else /* if(oldGframeCount > gframeCount) */ {
				for(size_t i = gframeCount; i < oldGframeCount; ++i) world::destroy_gframe_data(vma, mState.gframes[i]);
				mState.gframes.resize(gframeCount);
			}

			if(oldGframeCount > 0 || gframeCount == 0) {
				vkDestroyDescriptorPool(dev, mState.gframeDpool, nullptr);
			}

			if(gframeCount > 0) { // Create the gframe dpool, then allocate and write dsets
				mState.gframeDpool = world::create_gframe_dpool(dev, mState.gframes, mState.objectStorages->size());
				mState.projTransfOod = true;

				VkDescriptorSetAllocateInfo dsa_info = { };
				dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				dsa_info.descriptorPool     = mState.gframeDpool;
				dsa_info.descriptorSetCount = 1;
				VkWriteDescriptorSet frame_dset_wr = { };
				frame_dset_wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				frame_dset_wr.dstBinding = RDR_FRAME_UBO_BINDING;
				frame_dset_wr.descriptorCount = 1;
				frame_dset_wr.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				VkDescriptorBufferInfo frame_db_info = { nullptr, 0, VK_WHOLE_SIZE };

				for(auto& wgf : mState.gframes) {
					dsa_info.pSetLayouts = &mState.sharedState->gframeUboDsetLayout;
					VK_CHECK(vkAllocateDescriptorSets, dev, &dsa_info, &wgf.frameDset);
					frame_dset_wr.pBufferInfo = &frame_db_info;
					frame_dset_wr.dstSet = wgf.frameDset;
					frame_db_info.buffer = wgf.frameUbo;
					vkUpdateDescriptorSets(dev, 1, &frame_dset_wr, 0, nullptr);
					for(auto& osd : wgf.osData) {
						dsa_info.pSetLayouts = &mState.sharedState->objDsetLayout;
						VK_CHECK(vkAllocateDescriptorSets, dev, &dsa_info, &osd.objDset);
					}
				}
			}
		}
	}


	void WorldRenderer::duringDrawStage(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		assert(drawInfo.gframeIndex < mState.gframes.size());
		auto& objStorages  = * mState.objectStorages.get();
		auto& wgf          = mState.gframes[drawInfo.gframeIndex];
		if(objStorages.empty()) [[unlikely]] return;

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

		VkDescriptorSet dsets[]  = { wgf.frameDset, { } };
		auto draw = [&](uint32_t subpassIdx) {
			for(size_t osIdx = 0; auto& objStorage: objStorages) {
				assert(osIdx < wgf.osData.size());
				auto  batches  = objStorage.getDrawBatches();
				auto& gfOsData = wgf.osData[osIdx];

				if(batches.empty()) continue;

				ModelId    last_mdl = ModelId    (~ model_id_e    (batches.front().model_id));
				MaterialId last_mat = MaterialId (~ material_id_e (batches.front().material_id));
				constexpr VkDeviceSize zero[] = { 0 };
				auto rdrPlLayout = mState.sharedState->rdrPipelineLayout;

				assert(subpassIdx < mState.rdrPipelines.size());
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mState.rdrPipelines[subpassIdx]);
				vkCmdBindVertexBuffers(cmd, 1, 1, &gfOsData.objIdBfCopy.first.value, zero);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rdrPlLayout, RDR_OBJ_DSET_LOC, 1, &gfOsData.objDset, 0, nullptr);
				for(VkDeviceSize batchIdx = 0; const auto& batch : batches) {
					auto* model = objStorage.getModel(batch.model_id);
					assert(model != nullptr);
					if(batch.model_id != last_mdl) {
						vkCmdBindIndexBuffer(cmd, model->indices.value, 0, VK_INDEX_TYPE_UINT32);
						vkCmdBindVertexBuffers(cmd, 0, 1, &model->vertices.value, zero);
					}
					if(batch.material_id != last_mat) {
						auto mat = objStorage.getMaterial(batch.material_id);
						assert(mat != nullptr);
						dsets[RDR_MATERIAL_DSET_LOC] = mat->dset;
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rdrPlLayout, 0, std::size(dsets), dsets, 0, nullptr);
					}
					vkCmdDrawIndexedIndirect(
						cmd, gfOsData.drawCmdBfCopy.first,
						batchIdx * sizeof(VkDrawIndexedIndirectCommand), 1,
						sizeof(VkDrawIndexedIndirectCommand) );
					++ batchIdx;
				}
				++ osIdx;
			}
		};

		draw(0);
		for(uint32_t subpassIdx = 1; subpassIdx < mState.rdrPipelines.size(); ++ subpassIdx) {
			vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
			draw(subpassIdx);
		}

		assert(mState.rdrPipelines.size() == mState.pipelineParams.size());
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
			imb.image = ca.getRenderProcess().getRenderTarget(mState.rtargetId, drawInfo.gframeIndex).devImage;
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
			#ifndef VS_CODE_HEADER_LINTING_WORKAROUND
				constexpr
			#endif
			auto pi2 = 2.0f * std::numbers::pi_v<float>;
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
