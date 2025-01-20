#include "world_renderer.hpp"

#include <engine/engine.hpp>
#include <engine/shader-compiler/shcmp.hpp>

#include <fmamdl/fmamdl.hpp>

#include <vk-util/error.hpp>

#include <cassert>



namespace SKENGINE_NAME_NS::world {

	constexpr ShaderRequirement pipeline_shreq_default = { .name = "default", .pipelineLayout = PipelineLayoutId::e3d };


	namespace {

		struct PipelineConstants {
			uint32_t localWorkgroupSizes[3];
		};



		// https://vkguide.dev/docs/gpudriven/compute_culling/
		constexpr std::string_view cullCompShader =
			"#version 460\n"
			"\n"
			"layout(constant_id = 0) const uint LOCAL_SIZE_X = 16;\n"
			"layout(constant_id = 1) const uint LOCAL_SIZE_Y = 1;\n"
			"layout(constant_id = 2) const uint LOCAL_SIZE_Z = 1;\n"
			"\n"
			"layout(\n"
				"local_size_x_id = 0,\n"
				"local_size_y_id = 1,\n"
				"local_size_z_id = 2\n"
			") in;\n"
			"\n"
			"layout(push_constant) uniform constants {\n"
				"uint objCount;\n"
			"} pc;\n"
			"\n"
			"struct Object {"
				"mat4  model_transf;"
				"vec4  color_mul;"
				"float rnd;"
				"uint  draw_batch_idx;"
				"bool  visible;"
				"uint  unused1;"
			"};"
			"struct DrawBatch {\n"
				"uint indexCount;\n"
				"uint instanceCount;\n"
				"uint firstIndex;\n"
				"int  vertexOffset;\n"
				"uint firstInstance;\n"
			"};\n"
			"\n"
			"layout(std430, set = 0, binding = 0) /*readonly*/ buffer ObjectBuffer {\n"
				"Object p[];\n"
			"} obj_buffer;\n"
			"layout(std430, set = 0, binding = 1) writeonly buffer ObjectIdBuffer {\n"
				"uint p[];\n"
			"} obj_id_buffer;\n"
			"layout(std430, set = 0, binding = 2) buffer DrawBatchBuffer {\n"
				"DrawBatch p[];\n"
			"} draw_batch_buffer;\n"
			"\n"
			"bool isVisible(uint idx) {\n"
				"return obj_buffer.p[idx].visible;\n"
			"}\n"
			"\n"
			"void main() {\n"
				"uint invocId = gl_GlobalInvocationID.x;\n"
				"if(invocId < pc.objCount) {\n"
					"uint objIdx = invocId;\n"
					"bool visible = isVisible(objIdx);\n"
					"if(visible) {\n"
						"uint batchIdx = obj_buffer.p[invocId].draw_batch_idx;\n"
						"uint insertAt = atomicAdd(draw_batch_buffer.p[batchIdx].instanceCount, 1);\n"
						"uint instIdx = draw_batch_buffer.p[batchIdx].firstInstance + insertAt;\n"
						"obj_id_buffer.p[instIdx] = objIdx;\n"
					"}\n"
				"}\n"
			"}\n";

	}



	void computeCullWorkgroupSizes(uint32_t dst[3], const VkPhysicalDeviceProperties& props) {
		dst[0] = std::min(props.limits.maxComputeWorkGroupInvocations, props.limits.maxComputeWorkGroupSize[0]);
		dst[1] = 1;
		dst[2] = 1;
		assert(dst[0] * dst[1] * dst[2] <= props.limits.maxComputeWorkGroupInvocations);
	}


	VkPipeline create3dPipeline(
		VkDevice dev,
		ShaderCacheInterface& shCache,
		const WorldRenderer::PipelineParameters& plParams,
		VkRenderPass rpass,
		VkPipelineCache plCache,
		VkPipelineLayout plLayout,
		uint32_t subpass
	) {
		VkPipeline pipeline;
		ShaderModuleSet shModules;

		VkVertexInputAttributeDescription vtx_attr[6];
		VkVertexInputBindingDescription   vtx_bind[2];
		{ // Hard-coded input descriptions and bindings
			constexpr size_t V_POS = 0;
			constexpr size_t V_TEX = 1;
			constexpr size_t V_NRM = 2;
			constexpr size_t V_TNU = 3;
			constexpr size_t V_TNV = 4;
			constexpr size_t I_OID = 5;
			#define ATTRIB_(I_, B_, F_, L_, O_) { \
				static_assert(I_ < std::size(vtx_attr)); \
				vtx_attr[I_].binding  = B_; \
				vtx_attr[I_].format   = F_; \
				vtx_attr[I_].location = L_; \
				vtx_attr[I_].offset   = O_; }
			ATTRIB_(V_POS, 0, VK_FORMAT_R32G32B32_SFLOAT, 0, offsetof(fmamdl::Vertex, position))
			ATTRIB_(V_TEX, 0, VK_FORMAT_R32G32_SFLOAT,    1, offsetof(fmamdl::Vertex, texture))
			ATTRIB_(V_NRM, 0, VK_FORMAT_R32G32B32_SFLOAT, 2, offsetof(fmamdl::Vertex, normal))
			ATTRIB_(V_TNU, 0, VK_FORMAT_R32G32B32_SFLOAT, 3, offsetof(fmamdl::Vertex, tangent))
			ATTRIB_(V_TNV, 0, VK_FORMAT_R32G32B32_SFLOAT, 4, offsetof(fmamdl::Vertex, bitangent))
			ATTRIB_(I_OID, 1, VK_FORMAT_R32_UINT,         5, offsetof(dev::ObjectId,  id))
			#undef ATTRIB_
			vtx_bind[0].binding   = 0;
			vtx_bind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			vtx_bind[0].stride    = sizeof(fmamdl::Vertex);
			vtx_bind[1].binding   = 1;
			vtx_bind[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
			vtx_bind[1].stride    = sizeof(dev::ObjectId);
		}

		VkPipelineVertexInputStateCreateInfo vi = { }; {
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vi.vertexAttributeDescriptionCount = std::size(vtx_attr);
			vi.pVertexAttributeDescriptions    = vtx_attr;
			vi.vertexBindingDescriptionCount = std::size(vtx_bind);
			vi.pVertexBindingDescriptions    = vtx_bind;
		}

		VkPipelineInputAssemblyStateCreateInfo ia = { }; {
			ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.primitiveRestartEnable = true;
			ia.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		}

		VkPipelineTessellationStateCreateInfo t = { }; {
			t.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
		}

		VkPipelineViewportStateCreateInfo v = { }; {
			v.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			v.viewportCount = 1;
			v.scissorCount  = 1;
		}

		VkPipelineRasterizationStateCreateInfo r = { }; {
			r.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			r.cullMode    = plParams.cullMode;
			r.frontFace   = plParams.frontFace;
			r.polygonMode = plParams.polygonMode;
			r.lineWidth   = plParams.lineWidth;
			r.rasterizerDiscardEnable = plParams.rasterizerDiscardEnable;
		}

		VkPipelineMultisampleStateCreateInfo m = { }; {
			m.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			m.minSampleShading = 1.0f;
			m.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			m.sampleShadingEnable  = false;
		}

		VkPipelineDepthStencilStateCreateInfo ds = { }; {
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable  = plParams.depthTestEnable;
			ds.depthWriteEnable = plParams.depthWriteEnable;
			ds.depthCompareOp   = plParams.depthCompareOp;
		}

		VkPipelineColorBlendAttachmentState atch_color[1]; {
			*atch_color = { };
			atch_color->blendEnable = plParams.blendEnable;
			atch_color->srcColorBlendFactor = plParams.srcColorBlendFactor;
			atch_color->dstColorBlendFactor = plParams.dstColorBlendFactor;
			atch_color->colorBlendOp = plParams.colorBlendOp;
			atch_color->srcAlphaBlendFactor = plParams.srcAlphaBlendFactor;
			atch_color->dstAlphaBlendFactor = plParams.dstAlphaBlendFactor;
			atch_color->alphaBlendOp = plParams.alphaBlendOp;
			atch_color->colorWriteMask = /* rgba */ VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		}

		VkPipelineColorBlendStateCreateInfo cb = { }; {
			cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.logicOpEnable   = false;
			cb.attachmentCount = 1;
			cb.pAttachments    = atch_color;
		}

		VkDynamicState states[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo d = { }; {
			d.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			d.dynamicStateCount = std::size(states);
			d.pDynamicStates    = states;
		}

		VkPipelineShaderStageCreateInfo stages[2]; {
			constexpr size_t VTX = 0;
			constexpr size_t FRG = 1;
			shModules = shCache.shader_cache_requestModuleSet(dev, plParams.shaderRequirement);
			stages[VTX] = { };
			stages[VTX].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[VTX].pName  = "main";
			stages[VTX].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[VTX].module = shModules.vertex;
			stages[FRG] = stages[VTX];
			stages[FRG].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[FRG].module = shModules.fragment;
		}

		VkGraphicsPipelineCreateInfo gpc_info = { };
		gpc_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gpc_info.renderPass = rpass;
		gpc_info.layout     = plLayout;
		gpc_info.subpass    = subpass;
		gpc_info.stageCount = std::size(stages);
		gpc_info.pStages    = stages;
		gpc_info.pVertexInputState   = &vi;
		gpc_info.pInputAssemblyState = &ia;
		gpc_info.pTessellationState  = &t;
		gpc_info.pViewportState      = &v;
		gpc_info.pRasterizationState = &r;
		gpc_info.pMultisampleState   = &m;
		gpc_info.pDepthStencilState  = &ds;
		gpc_info.pColorBlendState    = &cb;
		gpc_info.pDynamicState       = &d;

		try {
			VK_CHECK(vkCreateGraphicsPipelines, dev, plCache, 1, &gpc_info, nullptr, &pipeline);
			shCache.shader_cache_releaseModuleSet(dev, shModules);
		} catch(...) {
			shCache.shader_cache_releaseModuleSet(dev, shModules);
			std::rethrow_exception(std::current_exception());
		}

		return pipeline;
	}


	VkPipeline createCullPipeline(
		VkDevice dev,
		VkPipelineCache plCache,
		VkPipelineLayout plLayout,
		const VkPhysicalDeviceProperties& phDevProps
	) {
		VkPipeline pipeline;

		#define SPEC_ENTRY_A_(ID_, MEM_, IDX_) ( \
			VkSpecializationMapEntry { \
				.constantID = ID_, \
				.offset     = offsetof(PipelineConstants, MEM_) + (IDX_ * sizeof(*PipelineConstants::MEM_)), \
				.size       = sizeof(*PipelineConstants::MEM_) } \
		)
		VkSpecializationInfo     sInfo   = { };
		VkSpecializationMapEntry specMapEntries[] = {
			SPEC_ENTRY_A_(0, localWorkgroupSizes, 0),
			SPEC_ENTRY_A_(1, localWorkgroupSizes, 1),
			SPEC_ENTRY_A_(2, localWorkgroupSizes, 2) };
		#undef SPEC_ENTRY_A_

		VkComputePipelineCreateInfo cpcInfo = { };
		cpcInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		cpcInfo.layout = plLayout;

		PipelineConstants plConstants = { };
		computeCullWorkgroupSizes(plConstants.localWorkgroupSizes, phDevProps);

		auto shModule = ShaderCompiler::glslSourceToModule(dev, "wrdr:cull", cullCompShader, shaderc_compute_shader);
		cpcInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		cpcInfo.stage.pName  = "main";
		cpcInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
		cpcInfo.stage.module = shModule;
		cpcInfo.stage.pSpecializationInfo = &sInfo;
		sInfo.pData         = &plConstants;
		sInfo.dataSize      = sizeof(PipelineConstants);
		sInfo.pMapEntries   = specMapEntries;
		sInfo.mapEntryCount = std::size(specMapEntries);

		try {
			VK_CHECK(vkCreateComputePipelines, dev, plCache, 1, &cpcInfo, nullptr, &pipeline);
			vkDestroyShaderModule(dev, shModule, nullptr);
		} catch(...) {
			vkDestroyShaderModule(dev, shModule, nullptr);
			std::rethrow_exception(std::current_exception());
		}

		return pipeline;
	}

}
