#include "engine.hpp"

#include <fmamdl/fmamdl.hpp>

#include <vk-util/error.hpp>



namespace SKENGINE_NAME_NS::world {

	constexpr ShaderRequirement pipeline_shreq_default = { .name = "default", .pipelineLayout = PipelineLayoutId::e3d };


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

		VkVertexInputAttributeDescription vtx_attr[11];
		VkVertexInputBindingDescription   vtx_bind[2];
		{ // Hard-coded input descriptions and bindings
			constexpr size_t V_POS =  0;
			constexpr size_t V_TEX =  1;
			constexpr size_t V_NRM =  2;
			constexpr size_t V_TNU =  3;
			constexpr size_t V_TNV =  4;
			constexpr size_t I_TR0 =  5;
			constexpr size_t I_TR1 =  6;
			constexpr size_t I_TR2 =  7;
			constexpr size_t I_TR3 =  8;
			constexpr size_t I_COL =  9;
			constexpr size_t I_RND = 10;
			constexpr auto vec4_sz = sizeof(glm::vec4);
			#define ATTRIB_(I_, B_, F_, L_, O_) { \
				static_assert(I_ < std::size(vtx_attr)); \
				vtx_attr[I_].binding  = B_; \
				vtx_attr[I_].format   = F_; \
				vtx_attr[I_].location = L_; \
				vtx_attr[I_].offset   = O_; }
			ATTRIB_(V_POS, 0, VK_FORMAT_R32G32B32_SFLOAT,     0, offsetof(fmamdl::Vertex, position))
			ATTRIB_(V_TEX, 0, VK_FORMAT_R32G32_SFLOAT,        1, offsetof(fmamdl::Vertex, texture))
			ATTRIB_(V_NRM, 0, VK_FORMAT_R32G32B32_SFLOAT,     2, offsetof(fmamdl::Vertex, normal))
			ATTRIB_(V_TNU, 0, VK_FORMAT_R32G32B32_SFLOAT,     3, offsetof(fmamdl::Vertex, tangent))
			ATTRIB_(V_TNV, 0, VK_FORMAT_R32G32B32_SFLOAT,     4, offsetof(fmamdl::Vertex, bitangent))
			ATTRIB_(I_TR0, 1, VK_FORMAT_R32G32B32A32_SFLOAT,  5, (0 * vec4_sz) + offsetof(dev::Instance, model_transf))
			ATTRIB_(I_TR1, 1, VK_FORMAT_R32G32B32A32_SFLOAT,  6, (1 * vec4_sz) + offsetof(dev::Instance, model_transf))
			ATTRIB_(I_TR2, 1, VK_FORMAT_R32G32B32A32_SFLOAT,  7, (2 * vec4_sz) + offsetof(dev::Instance, model_transf))
			ATTRIB_(I_TR3, 1, VK_FORMAT_R32G32B32A32_SFLOAT,  8, (3 * vec4_sz) + offsetof(dev::Instance, model_transf))
			ATTRIB_(I_COL, 1, VK_FORMAT_R32G32B32A32_SFLOAT,  9, offsetof(dev::Instance, color_mul))
			ATTRIB_(I_RND, 1, VK_FORMAT_R32_SFLOAT,          10, offsetof(dev::Instance, rnd))
			#undef ATTRIB_
			vtx_bind[0].binding   = 0;
			vtx_bind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			vtx_bind[0].stride    = sizeof(fmamdl::Vertex);
			vtx_bind[1].binding   = 1;
			vtx_bind[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
			vtx_bind[1].stride    = sizeof(dev::Instance);
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

}
