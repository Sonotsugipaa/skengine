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
		VkPipeline basePipeline;
	};


	template <typename T>
	struct ArrayRef {
		size_t size;
		const T* data;

		template <size_t N>
		consteval ArrayRef(const std::array<T, N>& src): size(N), data(src.data()) { }
	};


	template <PipelineType pl_type> constexpr ArrayRef<VkVertexInputAttributeDescription> vtxAttribs;

	#define ATTRIB_(DST_, I_, B_, F_, L_, O_) { \
		static_assert(I_ < std::size(DST_)); \
		DST_[I_].binding  = B_; \
		DST_[I_].format   = F_; \
		DST_[I_].location = L_; \
		DST_[I_].offset   = O_; }

	template <> constexpr ArrayRef<VkVertexInputAttributeDescription> vtxAttribs<PipelineType::ePoly> = []() {
		using Array = std::array<VkVertexInputAttributeDescription, 6>;
		static constexpr Array r = []() {
			Array r = { };
			constexpr size_t V_POS  = 0;
			constexpr size_t I_COL  = 1;
			constexpr size_t I_TRNX = 2;
			constexpr size_t I_TRNY = 3;
			constexpr size_t I_TRNZ = 4;
			constexpr size_t I_TRNW = 5;
			ATTRIB_(r, V_POS,  0, VK_FORMAT_R32G32B32_SFLOAT,    V_POS,  offsetof(PolyVertex,     position))
			ATTRIB_(r, I_COL,  1, VK_FORMAT_R32G32B32A32_SFLOAT, I_COL,  offsetof(geom::Instance, color))
			ATTRIB_(r, I_TRNX, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNX, offsetof(geom::Instance, transform) + (0 * sizeof(glm::vec4)))
			ATTRIB_(r, I_TRNY, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNY, offsetof(geom::Instance, transform) + (1 * sizeof(glm::vec4)))
			ATTRIB_(r, I_TRNZ, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNZ, offsetof(geom::Instance, transform) + (2 * sizeof(glm::vec4)))
			ATTRIB_(r, I_TRNW, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNW, offsetof(geom::Instance, transform) + (3 * sizeof(glm::vec4)))
			return r;
		} ();
		return ArrayRef<VkVertexInputAttributeDescription>(r);
	} ();

	template <> constexpr ArrayRef<VkVertexInputAttributeDescription> vtxAttribs<PipelineType::eText> = []() {
		using Array = std::array<VkVertexInputAttributeDescription, 7>;
		static constexpr Array r = []() {
			Array r = { };
			constexpr size_t V_POS  = 0;
			constexpr size_t V_TEX  = 1;
			constexpr size_t I_COL  = 2;
			constexpr size_t I_TRNX = 3;
			constexpr size_t I_TRNY = 4;
			constexpr size_t I_TRNZ = 5;
			constexpr size_t I_TRNW = 6;
			ATTRIB_(r, V_POS,  0, VK_FORMAT_R32G32B32_SFLOAT,    V_POS,  offsetof(TextVertex,     position))
			ATTRIB_(r, V_TEX,  0, VK_FORMAT_R32G32_SFLOAT,       V_TEX,  offsetof(TextVertex,     uv))
			ATTRIB_(r, I_COL,  1, VK_FORMAT_R32G32B32A32_SFLOAT, I_COL,  offsetof(geom::Instance, color))
			ATTRIB_(r, I_TRNX, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNX, offsetof(geom::Instance, transform) + (0 * sizeof(glm::vec4)))
			ATTRIB_(r, I_TRNY, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNY, offsetof(geom::Instance, transform) + (1 * sizeof(glm::vec4)))
			ATTRIB_(r, I_TRNZ, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNZ, offsetof(geom::Instance, transform) + (2 * sizeof(glm::vec4)))
			ATTRIB_(r, I_TRNW, 1, VK_FORMAT_R32G32B32A32_SFLOAT, I_TRNW, offsetof(geom::Instance, transform) + (3 * sizeof(glm::vec4)))
			return r;
		} ();
		return ArrayRef<VkVertexInputAttributeDescription>(r);
	} ();

	#undef ATTRIB_


	template <PipelineType pl_type>
	VkPipeline createPipeline(VkDevice dev, const PipelineCreateInfo& pci) {
		VkPipeline pipeline;

		constexpr ArrayRef vtx_attr = vtxAttribs<pl_type>;

		VkVertexInputBindingDescription   vtx_bind[2]; {
			vtx_bind[0].binding   = 0;
			vtx_bind[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			vtx_bind[0].stride    = sizeof(geom::Vertex);
			vtx_bind[1].binding   = 1;
			vtx_bind[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
			vtx_bind[1].stride    = sizeof(geom::Instance);
		}

		VkPipelineVertexInputStateCreateInfo vi = { }; {
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vi.vertexAttributeDescriptionCount = vtx_attr.size;
			vi.pVertexAttributeDescriptions    = vtx_attr.data;
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
			const PipelineSetCreateInfo& psci
	) {
		PipelineSet r;

		assert(psci.polyDsetLayout == nullptr && "polygon pipelines do not use descriptors (yet?)");

		{ // Pipeline layout
			VkPipelineLayoutCreateInfo plcInfo = { };
			VkPushConstantRange pcRanges[1] = { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(geom::PushConstant) } };
			plcInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plcInfo.setLayoutCount = 1;
			plcInfo.pSetLayouts    = &psci.textDsetLayout;
			plcInfo.pushConstantRangeCount = std::size(pcRanges);
			plcInfo.pPushConstantRanges = pcRanges;
			VK_CHECK(vkCreatePipelineLayout, dev, &plcInfo, nullptr, &r.layout);
		}

		// Create the shader modules
		#define COMPILE_(NAME_, SRC_, KIND_) ShaderCompiler::glslSourceToModule(dev, NAME_, SRC_, shaderc_ ## KIND_ ## _shader)
		auto polyVtxModule = COMPILE_("geom:poly.vtx", polyVtxSrc, vertex);
		auto polyFrgModule = COMPILE_("geom:poly.frg", polyFrgSrc, fragment);
		auto textVtxModule = COMPILE_("geom:text.vtx", textVtxSrc, vertex);
		auto textFrgModule = COMPILE_("geom:text.frg", textFrgSrc, fragment);
		#undef COMPILE_
		auto destroyShaderModules = [&]() {
			vkDestroyShaderModule(dev, polyVtxModule, nullptr);
			vkDestroyShaderModule(dev, polyFrgModule, nullptr);
			vkDestroyShaderModule(dev, textVtxModule, nullptr);
			vkDestroyShaderModule(dev, textFrgModule, nullptr);
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
			pipelines.push_back(r.polyFill = createPipeline<PipelineType::ePoly>(dev, pci));
			pci.basePipeline = r.polyFill;
			pci.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			pci.polyMode = VK_POLYGON_MODE_LINE;
			pipelines.push_back(r.polyLine = createPipeline<PipelineType::ePoly>(dev, pci));
			pci.basePipeline = r.polyFill;
			pci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
			pci.polyMode = VK_POLYGON_MODE_FILL;
			pci.vertexShader   = textVtxModule;
			pci.fragmentShader = textFrgModule;
			pipelines.push_back(r.text = createPipeline<PipelineType::eText>(dev, pci));
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
		vkDestroyPipeline(dev, ps.text, nullptr);
		vkDestroyPipelineLayout(dev, ps.layout, nullptr);
	}

}}
