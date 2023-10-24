#pragma once

#include <skengine_fwd.hpp>

#define VK_NO_PROTOTYPES // Don't need those in the header
#include <vulkan/vulkan.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <span>



namespace SKENGINE_NAME_NS {
inline namespace geom {

	struct PolyVertex {
		alignas(glm::vec4) glm::vec3 pos;
	};

	struct PolyInstance {
		alignas(glm::vec4) glm::vec4 col;
		alignas(glm::vec4) glm::mat4 transform;
	};


	struct PipelineSetCreateInfo {
		VkRenderPass    renderPass;
		uint32_t        subpass;
		VkPipelineCache pipelineCache;
	};


	struct PipelineSet {
		VkPipelineLayout layout;
		VkPipeline polyLine;
		VkPipeline polyFill;
		//VkPipeline text;

		static PipelineSet create  (VkDevice, std::span<VkDescriptorSetLayout>, const PipelineSetCreateInfo&);
		static void        destroy (VkDevice, PipelineSet&) noexcept;
	};

}}
