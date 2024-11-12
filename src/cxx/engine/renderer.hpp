#pragma once

#include "shader_cache.hpp"

#include <transientarray.tpp>



namespace SKENGINE_NAME_NS {

	class ConcurrentAccess;


	/// \brief A Renderer describes all the procedures that need to be called
	///        when running one or more consequent subpasses, and the ones needed
	///        to set up and manage their contexts.
	///
	/// ```
	/// vkCreateRenderPass()
	/// -- prepareSubpass(), for each rpass, for each subpass
	///
	/// vkAcquireNextImageKHR()
	/// -- beforePreRender()
	/// loop_async_preRender()
	/// vkBeginCommandBuffer(cmd_prepare, cmd_draw)
	/// -- duringPrepareStage(cmd_prepare)
	/// vkEndCommandBuffer(cmd_prepare)
	///
	/// vkCmdBeginRenderPass()
	/// -- duringDrawStage(cmd_draw)
	/// vkCmdEndRenderPass()
	/// -- afterRenderPass(cmd_draw)
	///
	/// vkEndCommandBuffer(cmd_draw)
	/// vkQueuePresentKHR()
	/// -- afterPresent()
	/// vkWaitForFences(cmd_prepare, last frame if so required)
	/// loop_async_postRender()
	/// -- afterPostRender()
	///
	/// -- forgetSubpass, for each rpass, for each subpass
	/// vkDestroyRenderPass()
	/// vkCreateRenderPass() ...
	/// ```
	///
	class Renderer {
	public:
		struct PipelineInfo {
			using DsetLayoutBindings = util::TransientArray<const VkDescriptorSetLayoutBinding>;
			DsetLayoutBindings dsetLayoutBindings;
		};

		struct SubpassSetupInfo {
			VkRenderPass rpass;
			RenderPassId rpassId;
		};

		struct DrawSyncPrimitives {
			struct { VkSemaphore prepare, draw; } semaphores;
			struct { VkFence     prepare, draw; } fences;
		};

		struct DrawInfo {
			DrawSyncPrimitives syncPrimitives;
			uint32_t gframeIndex;
		};

		Renderer(const PipelineInfo& pInfo): r_pipelineInfo(pInfo) { r_consolidatePipelineInfo(); }
		Renderer(PipelineInfo&& pInfo): r_pipelineInfo(std::move(pInfo)) { r_consolidatePipelineInfo(); }
		Renderer(const Renderer&) = default; Renderer& operator=(const Renderer&) = default;
		Renderer(Renderer&&) = default;      Renderer& operator=(Renderer&&) = default;
		virtual ~Renderer() = default;

		virtual std::string_view name() const noexcept = 0;

		virtual void prepareSubpasses(const SubpassSetupInfo&, VkPipelineCache, ShaderCacheInterface*) { }
		virtual void forgetSubpasses(const SubpassSetupInfo&) { }

		virtual void afterSwapchainCreation(ConcurrentAccess&, unsigned gframeCount) {
			(void) gframeCount;
		}
		virtual void beforeSwapchainDestruction(ConcurrentAccess&) { }

		virtual void beforePreRender(ConcurrentAccess&, const DrawInfo&) { }
		virtual void duringPrepareStage(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) { }
		virtual void duringDrawStage(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) { }
		virtual void afterRenderPass(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) { }
		virtual void afterPresent(ConcurrentAccess&, const DrawInfo&) { }
		virtual void beforePostRender(ConcurrentAccess&, const DrawInfo&) { }
		virtual void afterPostRender(ConcurrentAccess&, const DrawInfo&) { }

		void   setBuffersOutOfDate() noexcept { r_buffersOod = true; }
		void resetBuffersOutOfDate() noexcept { r_buffersOod = false; }
		bool   areBuffersOutOfDate() const noexcept { return r_buffersOod; }

		const PipelineInfo& pipelineInfo() const noexcept { return r_pipelineInfo; }

	private:
		PipelineInfo r_pipelineInfo;
		bool r_buffersOod;

		void r_consolidatePipelineInfo() {
			r_pipelineInfo.dsetLayoutBindings.consolidate();
		}
	};

}
