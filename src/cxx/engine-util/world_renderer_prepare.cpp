#include "world_renderer.hpp"

#include <engine/engine.hpp>

#include <random>

#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>



namespace SKENGINE_NAME_NS {

	namespace world { // Used here, defined elsewhere

		void computeCullWorkgroupSizes(uint32_t dst[3], const VkPhysicalDeviceProperties& props);

		uint32_t set_light_buffer_capacity(VmaAllocator, WorldRenderer::LightStorage* dst, uint32_t desired);

		void update_light_storage_dset(VkDevice, VkBuffer, size_t lightCount, VkDescriptorSet);

		std::pair<vkutil::Buffer, size_t> create_obj_buffer(VmaAllocator, size_t count);
		std::pair<vkutil::Buffer, size_t> create_obj_id_buffer(VmaAllocator, size_t count);
		std::pair<vkutil::Buffer, size_t> create_draw_cmd_buffer(VmaAllocator, size_t count);
		void resize_obj_buffer(VmaAllocator, std::pair<vkutil::Buffer, size_t>* dst, size_t requiredCmdCount);
		void resize_obj_id_buffer(VmaAllocator, std::pair<vkutil::Buffer, size_t>* dst, size_t requiredCmdCount);
		void resize_draw_cmd_buffer(VmaAllocator, std::pair<vkutil::Buffer, size_t>* dst, size_t requiredCmdCount);

	}



	void WorldRenderer::duringPrepareStage(ConcurrentAccess& ca, const DrawInfo& drawInfo, VkCommandBuffer cmd) {
		auto& e   = ca.engine();
		auto& egf = ca.getGframeData(drawInfo.gframeIndex);
		auto& wgf = mState.gframes[drawInfo.gframeIndex];
		auto& ls  = lightStorage();
		auto& al  = getAmbientLight();
		auto& ubo = * wgf.frameUbo.mappedPtr<dev::FrameUniform>();
		auto& renderExtent = ca.engine().getRenderExtent();
		auto& objStorages = *mState.objectStorages;
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
			mState.lightStorageDsetsOod = true;
			mState.lightStorageOod = false;
		}

		auto rng  = std::minstd_rand(std::chrono::steady_clock::now().time_since_epoch().count());
		auto dist = std::uniform_real_distribution(0.0f, 1.0f);
		ubo.shade_step_count       = mState.params.shadeStepCount;
		ubo.shade_step_smooth      = mState.params.shadeStepSmoothness;
		ubo.shade_step_exp         = mState.params.shadeStepExponent;
		ubo.dithering_steps        = mState.params.ditheringSteps;
		ubo.rnd                    = dist(rng);
		ubo.time_delta             = std::float32_t(egf.frame_delta);
		ubo.p_light_dist_threshold = mState.params.pointLightDistanceThreshold;
		ubo.flags                  = dev::FrameUniformFlags(dev::FRAME_UNI_ZERO);
		ubo.ambient_lighting       = glm::vec4((all > 0)? glm::normalize(al) : al, all);
		ubo.view_transf            = getViewTransf();
		ubo.view_pos               = glm::inverse(ubo.view_transf) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		ubo.projview_transf        = ubo.proj_transf * ubo.view_transf;
		ubo.ray_light_count        = ls.rayCount;
		ubo.point_light_count      = ls.pointCount;
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


		glm::mat4 proj_transf_transp = glm::transpose(ubo.proj_transf);
		for(size_t osIdx = 0; auto& os : objStorages) {
			auto& osData = wgf.osData[osIdx];
			auto* cullPassUbo = osData.cullPassUbo.mappedPtr<dev::CullPassUbo>();
			os.commitObjects(cmd);

			// Credit for the math: https://github.com/zeux/niagara/blob/master/src/niagara.cpp
			constexpr auto normalizePlane = [](glm::vec4 p) { return p / glm::length(glm::vec3(p)); };
			glm::vec4 frustumX = normalizePlane(proj_transf_transp[3] + proj_transf_transp[0]); // x + w < 0
			glm::vec4 frustumY = normalizePlane(proj_transf_transp[3] + proj_transf_transp[1]); // y + w < 0
			*cullPassUbo = {
				.view_transf = ubo.view_transf,
				.frustum_lrtb = { frustumX.x, frustumX.z, frustumY.y, frustumY.z },
				.z_range = { mState.params.zNear, mState.params.zFar },
				.padding0 = { },
				.frustum_culling_enabled = mState.params.cullingEnabled,
				.padding1 = { } };

			osData.cullPassUbo.flush(cmd, vma);
			if(! osData.cullPassUbo.isHostVisible()) { // Barrier the buffer: (transfer wr) > (shader rd)
				VkBufferMemoryBarrier2 bars[1] = { };
				bars[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
				bars[0].buffer = osData.cullPassUbo; bars[0].size = sizeof(dev::CullPassUbo);
				bars[0].srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				bars[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				bars[0].dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				bars[0].dstAccessMask = VK_ACCESS_2_UNIFORM_READ_BIT;
				VkDependencyInfo depInfo = { };
				depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				depInfo.bufferMemoryBarrierCount = std::size(bars); depInfo.pBufferMemoryBarriers = bars;
				vkCmdPipelineBarrier2(cmd, &depInfo);
			}

			++ osIdx;
		}

		bool light_buffer_resized = wgf.lightStorageCapacity != ls.bufferCapacity;
		if(light_buffer_resized) {
			mState.logger.trace("Resizing light storage: {} -> {}", wgf.lightStorageCapacity, ls.bufferCapacity);
			vkutil::ManagedBuffer::destroy(vma, wgf.lightStorage);

			vkutil::BufferCreateInfo bc_info = { };
			bc_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bc_info.size  = ls.bufferCapacity * sizeof(dev::Light);
			wgf.lightStorage = vkutil::ManagedBuffer::createStorageBuffer(vma, bc_info);

			mState.lightStorageDsetsOod = true;
			wgf.lightStorageCapacity = ls.bufferCapacity;
		}

		if(mState.lightStorageDsetsOod) {
			for(auto& wgf : mState.gframes) wgf.frameDsetOod = true;
			mState.lightStorageDsetsOod = false;
		}

		if(wgf.frameDsetOod) {
			world::update_light_storage_dset(dev, wgf.lightStorage.value, wgf.lightStorageCapacity, wgf.frameDset);
			wgf.frameDsetOod = false;
		}

		if(true /* Optimizable, but not worth the effort */) {
			VkBufferCopy cp = { };
			cp.size = (ls.rayCount + ls.pointCount) * sizeof(dev::Light);
			vkCmdCopyBuffer(cmd, ls.buffer.value, wgf.lightStorage, 1, &cp);
		}

		{ // Prepare the cull pass; populate the draw command buffer copy and write the dset
			assert(wgf.osData.size() == objStorages.size());
			for(size_t osIdx = 0; auto& os : objStorages) {
				auto& gfOsData = wgf.osData[osIdx];
				size_t objBytes   = os.getDrawCount()      * sizeof(dev::Object);
				size_t objIdBytes = os.getDrawCount()      * sizeof(dev::ObjectId);
				size_t cmdBytes   = os.getDrawBatchCount() * sizeof(VkDrawIndexedIndirectCommand);
				world::resize_obj_buffer     (vma, &gfOsData.objBfCopy,     os.getDrawCount());
				world::resize_obj_id_buffer  (vma, &gfOsData.objIdBfCopy,   os.getDrawCount());
				world::resize_draw_cmd_buffer(vma, &gfOsData.drawCmdBfCopy, os.getDrawBatchCount());
				VkBufferMemoryBarrier2 bars[2] = { };
				bars[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
				bars[0].buffer = gfOsData.objBfCopy.first; bars[0].size = objBytes;
				bars[0].srcStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				bars[0].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT   | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
				bars[0].dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				bars[0].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				bars[1] = bars[0];
				bars[1].buffer = gfOsData.drawCmdBfCopy.first; bars[1].size = cmdBytes;
				bars[1].srcStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				bars[1].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT   | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
				bars[1].dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				bars[1].dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				VkDependencyInfo depInfo = { };
				depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
				depInfo.bufferMemoryBarrierCount = std::size(bars); depInfo.pBufferMemoryBarriers = bars;
				vkCmdPipelineBarrier2(cmd, &depInfo);
				auto cpBf = [&](VkBuffer src, std::pair<vkutil::Buffer, size_t>& dst, VkDeviceSize bytes) {
					VkBufferCopy cp = { 0, 0, bytes };
					vkCmdCopyBuffer(cmd, src, dst.first, 1, &cp);
				};
				os.waitUntilReady();
				cpBf(os.getObjectBuffer().value,      gfOsData.objBfCopy,     objBytes);
				cpBf(os.getDrawCommandBuffer().value, gfOsData.drawCmdBfCopy, cmdBytes);
				bars[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;       bars[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				bars[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
				bars[1].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;       bars[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				bars[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[1].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
				vkCmdPipelineBarrier2(cmd, &depInfo);
				VkDescriptorBufferInfo dbInfos[4] = {
					{ gfOsData.objBfCopy.first,     0, objBytes },
					{ gfOsData.objIdBfCopy.first,   0, objIdBytes },
					{ gfOsData.drawCmdBfCopy.first, 0, cmdBytes },
					{ gfOsData.cullPassUbo,         0, sizeof(dev::CullPassUbo) } };
				VkWriteDescriptorSet wr[std::size(dbInfos)];
				wr[0] = { };
				wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				wr[0].dstSet = gfOsData.objDset;
				wr[0].dstBinding = CULL_OBJ_STG_BINDING;
				wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				wr[0].descriptorCount = 1;
				wr[0].pBufferInfo = dbInfos + 0;
				wr[1] = wr[0];
				wr[1].dstBinding = CULL_OBJ_ID_STG_BINDING;
				wr[1].pBufferInfo = dbInfos + 1;
				wr[2] = wr[0];
				wr[2].dstBinding = CULL_CMD_BINDING;
				wr[2].pBufferInfo = dbInfos + 2;
				wr[3] = wr[0];
				wr[3].dstBinding = CULL_UBO_BINDING;
				wr[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				wr[3].pBufferInfo = dbInfos + 3;
				vkUpdateDescriptorSets(dev, std::size(wr), wr, 0, nullptr);
				++ osIdx;
			}
		}

		{ // Run the cull pass
			uint32_t dispatchXyz[3];
			world::computeCullWorkgroupSizes(dispatchXyz, ca.engine().getPhysDeviceProperties());

			for(size_t osIdx = 0; auto& os : objStorages) {
				auto&    gfOsData      = wgf.osData[osIdx];
				uint32_t drawCount     = os.getDrawCount();
				uint32_t groupCountX   = drawCount / dispatchXyz[0];
				if(drawCount % dispatchXyz[0] > 0) ++ groupCountX; // ceil behavior
				if(groupCountX > 0) {
					VkDescriptorSet dsets[] = { gfOsData.objDset };
					vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mState.cullPassPipeline);
					vkCmdBindDescriptorSets(
						cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mState.sharedState->cullPassPipelineLayout,
						0, std::size(dsets), dsets, 0, nullptr );
					vkCmdPushConstants(
						cmd,
						mState.sharedState->cullPassPipelineLayout,
						VK_SHADER_STAGE_COMPUTE_BIT,
						0, sizeof(uint32_t), &drawCount );
					VkBufferMemoryBarrier2 bars[3];
					VkDependencyInfo depInfo = { };
					{ // Barrier boilerplate ( {0,1,2} -> { obj, objidx, drawcmd } )
						depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
						depInfo.bufferMemoryBarrierCount = std::size(bars); depInfo.pBufferMemoryBarriers = bars;
						bars[0] = { };
						bars[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
						bars[0].buffer = gfOsData.objBfCopy.first;   bars[0].size = os.getDrawCount() * sizeof(dev::Object);
						bars[1] = bars[0];
						bars[1].buffer = gfOsData.objIdBfCopy.first; bars[1].size = os.getDrawCount() * sizeof(dev::ObjectId);
						bars[2] = bars[0];
						bars[2].buffer = gfOsData.objBfCopy.first;   bars[2].size = os.getDrawBatchCount() * sizeof(VkDrawIndexedIndirectCommand);
					}
					bars[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;       bars[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
					bars[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
					bars[1].srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;   bars[1].srcAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
					bars[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[1].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
					bars[2].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;       bars[2].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
					bars[2].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[2].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
					vkCmdPipelineBarrier2(cmd, &depInfo);
					vkCmdDispatch(cmd, groupCountX, 1, 1);
					bars[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[0].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
					bars[0].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;  bars[0].dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
					bars[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[1].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
					bars[1].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;   bars[1].dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
					bars[2].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; bars[2].srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
					bars[2].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;  bars[2].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
					vkCmdPipelineBarrier2(cmd, &depInfo);
				}
				++ osIdx;
			}
		}
	}

}
