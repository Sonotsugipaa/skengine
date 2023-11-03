#include <vulkan/vulkan.h> // This *needs* to be included before "core.hpp" because of `VK_NO_PROTOTYPES`
#include "core.hpp"

#include <vk-util/error.hpp>

#include <shader-compiler/shcmp.hpp>

#include <string_view>
#include <span>
#include <vector>
#include <stdexcept>

#include "shader-literals.inl.cpp"



namespace SKENGINE_NAME_NS {
inline namespace geom {

	struct PipelineCreateInfo : public PipelineSetCreateInfo {
		VkPipelineLayout    pipelineLayout;
		VkPrimitiveTopology topology;
		VkPolygonMode       polyMode;
		VkShaderModule      vertexShader;
		VkShaderModule      fragmentShader;
		VkPipeline       basePipeline;
	};


	VkPipeline createPipeline(VkDevice dev, const PipelineCreateInfo& pci) {
		VkPipeline pipeline;

		VkVertexInputAttributeDescription vtx_attr[6];
		VkVertexInputBindingDescription   vtx_bind[2];
		{ // Hard-coded input descriptions and bindings
			constexpr size_t V_POS  = 0;
			constexpr size_t I_COL  = 1;
			constexpr size_t I_TRNX = 2;
			constexpr size_t I_TRNY = 3;
			constexpr size_t I_TRNZ = 4;
			constexpr size_t I_TRNW = 5;
			#define ATTRIB_(I_, B_, F_, L_, O_) { \
				static_assert(I_ < std::size(vtx_attr)); \
				vtx_attr[I_].binding  = B_; \
				vtx_attr[I_].format   = F_; \
				vtx_attr[I_].location = L_; \
				vtx_attr[I_].offset   = O_; }
			ATTRIB_(V_POS,  0, VK_FORMAT_R32G32B32_SFLOAT,    V_POS,  offsetof(PolyVertex,   position))
			ATTRIB_(I_COL,  1, VK_FORMAT_R32G32B32A32_SFLOAT, I_COL,  offsetof(PolyInstance, color))
			ATTRIB_(I_TRNX, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNX, offsetof(PolyInstance, transform) + (0 * sizeof(glm::vec4)))
			ATTRIB_(I_TRNY, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNY, offsetof(PolyInstance, transform) + (1 * sizeof(glm::vec4)))
			ATTRIB_(I_TRNZ, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNZ, offsetof(PolyInstance, transform) + (2 * sizeof(glm::vec4)))
			ATTRIB_(I_TRNW, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNW, offsetof(PolyInstance, transform) + (3 * sizeof(glm::vec4)))
			#undef ATTRIB_
			vtx_bind[0].binding   = 0;
			vtx_bind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			vtx_bind[0].stride    = sizeof(PolyVertex);
			vtx_bind[1].binding   = 1;
			vtx_bind[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
			vtx_bind[1].stride    = sizeof(PolyInstance);
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
			ia.primitiveRestartEnable = false;
			ia.topology               = pci.topology;
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
			r.cullMode    = VK_CULL_MODE_NONE;
			r.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			r.polygonMode = pci.polyMode;
			r.lineWidth   = 1.0f;
			r.rasterizerDiscardEnable = false;
		}

		VkPipelineMultisampleStateCreateInfo m = { }; {
			m.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			m.minSampleShading = 1.0f;
			m.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			m.sampleShadingEnable  = false;
		}

		VkPipelineDepthStencilStateCreateInfo ds = { }; {
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable  = false;
			ds.depthWriteEnable = false;
			ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;
		}

		VkPipelineColorBlendAttachmentState atch_color[1]; {
			*atch_color = { };
			atch_color->blendEnable = true;
			atch_color->colorBlendOp = VK_BLEND_OP_ADD;
			atch_color->alphaBlendOp = VK_BLEND_OP_ADD;
			atch_color->srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			atch_color->dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			atch_color->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			atch_color->dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
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
			stages[VTX] = { };
			stages[VTX].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[VTX].pName  = "main";
			stages[VTX].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[VTX].module = pci.vertexShader;
			stages[FRG] = stages[VTX];
			stages[FRG].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[FRG].module = pci.fragmentShader;
		}

		VkGraphicsPipelineCreateInfo gpc_info = { };
		gpc_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gpc_info.renderPass = pci.renderPass;
		gpc_info.layout     = pci.pipelineLayout;
		gpc_info.subpass    = pci.subpass;
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

		// This entire sequence could have been a try/finally, but whatever
		VK_CHECK(vkCreateGraphicsPipelines, dev, pci.pipelineCache, 1, &gpc_info, nullptr, &pipeline);

		return pipeline;
	}


	PipelineSet PipelineSet::create(
			VkDevice dev,
			std::span<VkDescriptorSetLayout> dsetLayouts,
			const PipelineSetCreateInfo&     psci
	) {
		PipelineSet r;

		{ // Pipeline layout
			VkPipelineLayoutCreateInfo plcInfo = { };
			plcInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plcInfo.setLayoutCount = dsetLayouts.size();
			plcInfo.pSetLayouts    = dsetLayouts.data();
			VK_CHECK(vkCreatePipelineLayout, dev, &plcInfo, nullptr, &r.layout);
		}

		// Create the shader modules
		#define COMPILE_(NAME_, SRC_, KIND_) ShaderCompiler::glslSourceToModule(dev, NAME_, SRC_, shaderc_ ## KIND_ ## _shader)
		auto polyVtxModule = COMPILE_("geom:poly.vtx", polyVtxSrc, vertex);
		auto polyFrgModule = COMPILE_("geom:poly.frg", polyFrgSrc, fragment);
		#undef COMPILE_
		auto destroyShaderModules = [&]() {
			vkDestroyShaderModule(dev, polyVtxModule, nullptr);
			vkDestroyShaderModule(dev, polyFrgModule, nullptr);
		};

		std::vector<VkPipeline> pipelines;
		try { // Pipelines or bust
			PipelineCreateInfo pci = { };
			pci.renderPass     = psci.renderPass;
			pci.subpass        = psci.subpass;
			pci.pipelineCache  = psci.pipelineCache;
			pci.pipelineLayout = r.layout;

			pci.vertexShader   = polyVtxModule;
			pci.fragmentShader = polyFrgModule;
			pci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			pci.polyMode = VK_POLYGON_MODE_FILL;
			pipelines.push_back(r.polyFill = createPipeline(dev, pci));
			pci.basePipeline = r.polyFill;
			pci.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			pci.polyMode = VK_POLYGON_MODE_LINE;
			pipelines.push_back(r.polyLine = createPipeline(dev, pci));
		} catch(...) {
			// Destroy the now useless layout and the successfully created pipelines, then resume the downwards spiral
			destroyShaderModules();
			vkDestroyPipelineLayout(dev, r.layout, nullptr);
			std::rethrow_exception(std::current_exception());
		}
		destroyShaderModules();

		return r;
	}


	void PipelineSet::destroy(VkDevice dev, PipelineSet& ps) noexcept {
		vkDestroyPipeline(dev, ps.polyLine, nullptr);
		vkDestroyPipeline(dev, ps.polyFill, nullptr);
		vkDestroyPipelineLayout(dev, ps.layout, nullptr);
	}

}}
