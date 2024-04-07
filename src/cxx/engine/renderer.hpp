#pragma once

#include "shader_cache.hpp"



namespace SKENGINE_NAME_NS {

	class ConcurrentAccess;


	/// ```
	/// vkAcquireNextImageKHR()
	/// -- beforePreRender
	/// loop_async_preRender()
	/// vkBeginCommandBuffer(cmd_prepare, cmd_draw)
	/// -- duringPrepareStage
	/// vkEndCommandBuffer(cmd_prepare)
	/// vkCmdBeginRenderPass()
	/// -- duringDrawStage
	/// vkCmdEndRenderPass()
	/// vkEndCommandBuffer(cmd_draw)
	/// vkQueuePresentKHR()
	/// -- afterPresent
	/// vkWaitForFences(cmd_prepare, last frame if so required)
	/// loop_async_postRender()
	/// -- afterPostRender
	/// ```
	///
	class Renderer {
	public:
		enum class RenderPass : unsigned { eNone = 0, eWorld = 1, eUi = 2 };

		struct Info {
			ShaderRequirement shaderReq;
			RenderPass rpass;
		};

		Renderer(const Info& sInfo): r_pipelineInfo(sInfo) { }
		Renderer(Info&& sInfo): r_pipelineInfo(std::move(sInfo)) { }
		Renderer(const Renderer&) = default; Renderer& operator=(const Renderer&) = default;
		Renderer(Renderer&&) = default;      Renderer& operator=(Renderer&&) = default;
		virtual ~Renderer() = default;

		virtual std::string_view name() const noexcept = 0;

		virtual void afterSwapchainCreation(ConcurrentAccess&, unsigned gframeCount) { (void) gframeCount; }
		virtual void beforeSwapchainDestruction(ConcurrentAccess&) { }

		virtual void beforePreRender(ConcurrentAccess&, unsigned gframeIndex) { (void) gframeIndex; }
		virtual void duringPrepareStage(ConcurrentAccess&, unsigned gframeIndex, VkCommandBuffer) { (void) gframeIndex; }
		virtual void duringDrawStage(ConcurrentAccess&, unsigned gframeIndex, VkCommandBuffer) { (void) gframeIndex; }
		virtual void afterPresent(ConcurrentAccess&, unsigned gframeIndex) { (void) gframeIndex; }
		virtual void afterRenderPass(ConcurrentAccess&, unsigned gframeIndex) { (void) gframeIndex; }
		virtual void beforePostRender(ConcurrentAccess&, unsigned gframeIndex) { (void) gframeIndex; }
		virtual void afterPostRender(ConcurrentAccess&, unsigned gframeIndex) { (void) gframeIndex; }

		void   setBuffersOutOfDate() noexcept { r_buffersOod = true; }
		void resetBuffersOutOfDate() noexcept { r_buffersOod = false; }
		bool   areBuffersOutOfDate() const noexcept { return r_buffersOod; }

		const Info& pipelineInfo() const noexcept { return r_pipelineInfo; }

	private:
		Info r_pipelineInfo;
		bool r_buffersOod;
	};

}
