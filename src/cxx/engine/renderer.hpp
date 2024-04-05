#pragma once

#include "shader_cache.hpp"



namespace SKENGINE_NAME_NS {

	class Engine;


	struct SubpassInfo {
		enum class RenderPass : unsigned { eNone = 0, eWorld = 1, eUi = 2 };

		ShaderRequirement shaderReq;
		VkPipelineBindPoint bindPoint;
		VkImageLayout targetFinalLayout;
		RenderPass target;
	};


	class Renderer {
	public:
		Renderer() = default;
		Renderer(nullptr_t): r_subpassInfo({ }), r_buffersOod(false) { }
		Renderer(const SubpassInfo& sInfo): r_subpassInfo(sInfo) { }
		Renderer(SubpassInfo&& sInfo): r_subpassInfo(std::move(sInfo)) { }
		virtual ~Renderer() = default;

		virtual void beforeSwapchainReset(Engine&) { }
		virtual void afterSwapchainReset(Engine&) { }

		virtual void beforePreRender(Engine&) { }
		virtual void duringPrepareStage(Engine&) { }
		virtual void duringDrawStage(Engine&) { }
		virtual void afterPresent(Engine&) { }
		virtual void afterPostRender(Engine&) { }

		void   setBuffersOutOfDate() noexcept { r_buffersOod = true; }
		void resetBuffersOutOfDate() noexcept { r_buffersOod = false; }
		bool   areBuffersOutOfDate() const noexcept { return r_buffersOod; }

		const SubpassInfo& subpassInfo() const noexcept { return r_subpassInfo; }

	private:
		SubpassInfo r_subpassInfo;
		bool r_buffersOod;
	};

}
