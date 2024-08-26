#pragma once

#include "../types.hpp"
#include "../renderer.hpp"

#include <idgen.hpp>

#include <type_traits>
#include <limits>
#include <bit>
#include <set>
#include <map>
#include <stdexcept>
#include <memory>

#include <vk-util/memory.hpp>

#include <misc-util.tpp>

#include <spdlog/logger.h>



namespace SKENGINE_NAME_NS {

	struct RenderTargetDescription {
		VkExtent2D extent;
		VkImageUsageFlags usage;
		VkFormat format;
		bool hostReadable         : 1;
		bool hostWriteable        : 1;
		bool hostAccessSequential : 1;
	};


	struct RenderTarget {
		RenderTargetDescription description;
		vkutil::ManagedImage  devImage;
		vkutil::ManagedBuffer hostBuffer;
		operator bool() const noexcept { return devImage.value != nullptr; }
	};


	struct RenderPassDescription {
		#define TPR_ util::TransientPtrRange
		struct Subpass {
			struct Dependency {
				uint32_t srcSubpass;
				VkPipelineStageFlags srcStageMask;
				VkPipelineStageFlags dstStageMask;
				VkAccessFlags        srcAccessMask;
				VkAccessFlags        dstAccessMask;
				VkDependencyFlags    dependencyFlags;
			};
			struct Attachment {
				RenderTargetId rtarget;
				VkImageLayout initialLayout;
				VkImageLayout finalLayout;
				VkAttachmentLoadOp  loadOp;
				VkAttachmentStoreOp storeOp;
			};
			TPR_<Attachment> inputAttachments;
			TPR_<Attachment> colorAttachments;
			TPR_<Dependency> subpassDependencies;
			VkAttachmentLoadOp  depthLoadOp;
			VkAttachmentStoreOp depthStoreOp;
			bool requiresDepthAttachments : 1;
		};
		std::vector<Subpass> subpasses;
		#undef TPR_
	};


	struct RenderPass {
		RenderPassDescription description;
		VkRenderPass handle;
		operator bool() const noexcept { return handle != nullptr; }
	};


	class RenderProcess {
	public:
		#define DECL_SCOPED_ENUM_(ENUM_, ALIAS_, UNDERLYING_) using ALIAS_ = UNDERLYING_; enum class ENUM_ : ALIAS_ { };
		DECL_SCOPED_ENUM_(StepId,        step_id_e, unsigned)
		DECL_SCOPED_ENUM_(SequenceIndex, seq_idx_e, std::make_unsigned_t<step_id_e>)
		#undef DECL_SCOPED_ENUM_

		class UnsatisfiableDependencyError;
		class DependencyGraph;

		struct VulkanState {
			VmaAllocator vma;
			VkFormat depthImageFormat;
			VkDevice device() noexcept { VmaAllocatorInfo i; vmaGetAllocatorInfo(vma, &i); return i.device; }
		};

		struct Step {
			SequenceIndex seqIndex;
			RenderPassId rpass;
			RendererId renderer;
			uint32_t depthImageCount;
			VkDeviceSize depthImageSize;
		};

		struct DepthImageSet {
			std::vector<vkutil::Image> image;
			VkDeviceSize size;
		};

		class WaveIterator {
		public:
			WaveIterator(): wi_rp(nullptr), wi_seqIdx(SequenceIndex(~ seq_idx_e(0))), wi_firstStep(StepId(0)), wi_stepCount(0) { }
			std::strong_ordering operator<=>(const WaveIterator&) const noexcept;
			bool operator!=(const WaveIterator& r) const noexcept { return operator<=>(r) != std::strong_ordering::equal; };
			WaveIterator& operator++();
			std::span<std::pair<StepId, Step>> operator*();
			std::span<std::pair<StepId, Step>> operator->() { return operator*(); }

		private:
			friend RenderProcess;
			RenderProcess* wi_rp;
			SequenceIndex  wi_seqIdx;
			StepId    wi_firstStep;
			step_id_e wi_stepCount;
			unsigned wi_validity;
		};

		class WaveRange {
		public:
			WaveIterator& begin() noexcept { return beginIter; }
			WaveIterator& end()   noexcept { return endIter; }
			WaveIterator beginIter;
			WaveIterator endIter;
		};

		struct SequenceDescription {
			std::vector<Step> steps;
			std::vector<RenderTargetDescription> rtargets;
			std::vector<RenderPassDescription> rpasses;
			std::vector<std::weak_ptr<Renderer>> renderers;
		};

		RenderProcess(): rp_waveIterValidity(0), rp_initialized(false) { }
		RenderProcess(const RenderProcess&) = delete;
		RenderProcess(RenderProcess&&) = delete;
		#ifndef NDEBUG
			~RenderProcess();
		#endif

		void setup(VmaAllocator vma, VkFormat depthImageFormat, const DependencyGraph&);
		void setup(VmaAllocator vma, VkFormat depthImageFormat, DependencyGraph&&);
		void setup(VmaAllocator vma, VkFormat depthImageFormat, const SequenceDescription&);
		void destroy();

		const Step&         getStep         (StepId) const;
		const RenderTarget& getRenderTarget (RenderTargetId) const;
		const RenderPass&   getRenderPass   (RenderPassId) const;
		const Renderer&     getRenderer     (RendererId) const;

		std::span<const vkutil::ManagedImage> getStepDepthImages(StepId) const;

		void setRtargetExtent(RenderTargetId, VkExtent2D);

		VulkanState getVulkanState() const noexcept { return rp_vkState; }

		WaveRange waveRange() &;

	private:
		VulkanState rp_vkState;
		std::vector<std::pair<StepId, Step>> rp_steps;
		std::vector<RenderTarget> rp_rtargets;
		std::vector<RenderPass> rp_rpasses;
		std::vector<std::shared_ptr<Renderer>> rp_renderers;
		std::vector<vkutil::ManagedImage> rp_depthImages;
		unsigned rp_waveIterValidity;
		bool rp_initialized;
	};


	class RenderProcess::DependencyGraph {
	public:
		struct StepDescription { RenderPassId rpass; RendererId renderer; };
		using Steps = std::vector<StepDescription>;
		using Rtargets = std::vector<RenderTargetDescription>;
		using Rpasses = std::vector<RenderPassDescription>;
		using Renderers = std::vector<std::weak_ptr<Renderer>>;
		using DependencyMap = std::map<StepId, std::set<StepId>>;

		class Subgraph {
		public:
			Subgraph& /* *this */ before (const Subgraph&);
			Subgraph& /* *this */ after  (const Subgraph&);

			StepId stepId() const noexcept { return sg_step; }

		private:
			friend DependencyGraph;
			DependencyGraph* sg_graph;
			StepId sg_step;
		};

		RenderTargetId addRtarget  (RenderTargetDescription);
		RenderPassId   addRpass    (RenderPassDescription);
		RendererId     addRenderer (std::weak_ptr<Renderer>);

		Subgraph addDummyStep();
		Subgraph addStep(RenderPassId, RendererId);

		SequenceDescription assembleSequence() const;

	private:
		friend RenderProcess;
		Steps     dg_steps;
		Rtargets  dg_rtargets;
		Rpasses   dg_rpasses;
		Renderers dg_renderers;
		DependencyMap dg_dependencies_fwd; // Key comes before Values
		DependencyMap dg_dependencies_bwd; // Key depends on Values
	};


	class RenderProcess::UnsatisfiableDependencyError : public std::runtime_error {
	public:
		UnsatisfiableDependencyError(std::vector<RenderProcess::StepId> dependencyChain);
		const auto& dependencyChain() const noexcept { return dep_err_depChain; }

	private:
		std::vector<RenderProcess::StepId> dep_err_depChain;
	};

}
