#pragma once

#include "shader_cache.hpp"

#include <misc-util.tpp>



namespace SKENGINE_NAME_NS {

	class ConcurrentAccess;


	/// \brief A Renderer describes all the procedures that need to be called
	///        when running one or more consequent subpasses, and the ones needed
	///        to set up and manage their contexts.
	///
	/// ```
	/// vkAcquireNextImageKHR()
	/// -- beforePreRender
	/// loop_async_preRender()
	/// vkBeginCommandBuffer(cmd_prepare, cmd_draw)
	/// -- duringPrepareStage
	/// vkEndCommandBuffer(cmd_prepare)
	///
	/// vkCmdBeginRenderPass(1)
	/// -- duringDrawStage, only for rpass 1
	/// vkCmdEndRenderPass(1)
	///
	/// vkCmdBeginRenderPass(2)
	/// -- duringDrawStage, only for rpass 2
	/// vkCmdEndRenderPass(2)
	///
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
			using DsetLayoutBindings = util::TransientPtrRange<const VkDescriptorSetLayoutBinding>;
			using ShaderRequirements = util::TransientPtrRange<const ShaderRequirement>;
			DsetLayoutBindings dsetLayoutBindings;
			ShaderRequirements shaderRequirements;
			RenderPass rpass;
		};

		Renderer(const Info& sInfo): r_pipelineInfo(sInfo) { r_consolidatePipelineInfo(); }
		Renderer(Info&& sInfo): r_pipelineInfo(std::move(sInfo)) { r_consolidatePipelineInfo(); }
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

		void r_consolidatePipelineInfo() {
			#define CONSOLIDATE_(TPR_) if(! r_pipelineInfo.TPR_.ownsMemory()) r_pipelineInfo.TPR_ = r_pipelineInfo.TPR_.copy();
			CONSOLIDATE_(dsetLayoutBindings)
			CONSOLIDATE_(shaderRequirements)
			#undef CONSOLIDATE_
		}
	};

}
