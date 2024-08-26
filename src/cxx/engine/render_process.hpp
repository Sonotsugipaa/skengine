#pragma once

#include "types.hpp"
#include "renderer.hpp"

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
		bool hostReadable  : 1;
		bool hostWriteable : 1;
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
			TPR_<RenderTargetId> input;
			TPR_<RenderTargetId> output;
			VkAttachmentLoadOp  colorLoadOp;
			VkAttachmentLoadOp  depthLoadOp;
			VkAttachmentStoreOp colorStoreOp;
			VkAttachmentStoreOp depthStoreOp;
			bool requiresDepthBuffer : 1;
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
		DECL_SCOPED_ENUM_(StepId,        step_id_e, int)
		DECL_SCOPED_ENUM_(SequenceIndex, seq_idx_e, std::make_unsigned_t<step_id_e>)
		#undef DECL_SCOPED_ENUM_

		class UnsatisfiableDependencyError;
		class DependencyGraph;
		struct VulkanState { VmaAllocator vma; VkPhysicalDevice physDevice; VkDevice device() noexcept; };
		struct Step { SequenceIndex seqIndex; RenderPassId rpass; RendererId renderer; };

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
		};

		struct SequenceDescription {
			std::vector<Step> steps;
			std::vector<RenderTargetDescription> rtargets;
			std::vector<RenderPassDescription> rpasses;
			std::vector<std::weak_ptr<Renderer>> renderers;
		};

		RenderProcess(std::shared_ptr<spdlog::logger> logger): rp_initialized(false), rp_logger(std::move(logger)) { }
		RenderProcess(const RenderProcess&) = delete;
		RenderProcess(RenderProcess&&) = delete;
		#ifndef NDEBUG
			~RenderProcess();
		#endif

		void setup(VmaAllocator vma, VkPhysicalDevice physDev, const DependencyGraph&);
		void setup(VmaAllocator vma, VkPhysicalDevice physDev, DependencyGraph&&);
		void setup(VmaAllocator vma, VkPhysicalDevice physDev, const SequenceDescription&);
		void destroy();

		void setRtargetExtent(RenderTargetId, VkExtent2D);

		VulkanState getVulkanState() const noexcept { return rp_vkState; }

		WaveIterator begin();
		WaveIterator end() { return WaveIterator(); }

	private:
		bool rp_initialized;
		std::shared_ptr<spdlog::logger> rp_logger;
		VulkanState rp_vkState;
		std::vector<std::pair<StepId, Step>> rp_steps;
		std::vector<RenderTarget> rp_rtargets;
		std::vector<RenderPass> rp_rpasses;
		std::vector<std::shared_ptr<Renderer>> rp_renderers;
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
		Steps       dg_steps;
		Rtargets    dg_rtargets;
		Rpasses     dg_rpasses;
		Renderers   dg_renderers;
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



//	namespace OLD_CODE {
//
//		using render_step_id_e = uint_fast16_t;
//		enum class RenderStepId : render_step_id_e { };
//
//		struct RenderStepInfo {
//			struct Hash;
//			std::vector<RenderStepId> dependencies;
//			RenderTargetId rtargetId;
//			RenderPassId   rpassId;
//			RendererId     renderer;
//			uint32_t       subpass;
//			bool requiresDepthBuffer   : 1;
//			bool requiresStencilBuffer : 1;
//		};
//
//		struct RenderStep {
//			std::vector<RenderStepId> dependencies;
//			RenderTargetId rtargetId;
//			RenderPassId   rpassId;
//			RendererId     renderer;
//			uint32_t       subpass;
//			uint32_t       depthImageIndex;
//			bool requiresDepthBuffer   : 1;
//			bool requiresStencilBuffer : 1;
//		};
//
//		struct RenderProcess { };
//
//
//		struct RenderStepInfo::Hash { constexpr size_t operator()(const RenderStepInfo& step) const noexcept {
//			#define UTYPE_(V_) std::underlying_type_t<decltype(V_)>
//			#define ROT_(V_, SHIFT_) std::rotl<size_t>(UTYPE_(V_)(V_), SHIFT_)
//			constexpr size_t shift = std::numeric_limits<size_t>::digits / 3;
//			size_t rt = size_t(step.rtargetId) + ROT_(step.rtargetId, 1);
//			size_t rp = size_t(step.rpassId)   + ROT_(step.rpassId,   2);
//			size_t rr = size_t(step.renderer)  + ROT_(step.renderer,  3);
//			return rt ^ rp ^ rr;
//			#undef UTYPE_
//			#undef ROT_
//		} };
//
//		constexpr std::strong_ordering operator<=>(const RenderStepInfo& l, const RenderStepInfo& r) noexcept {
//			using rt_e = render_target_e;
//			using rp_e = render_pass_e;
//			using rr_e = renderer_id_e;
//			if(rt_e(l.rtargetId) < rt_e(r.rtargetId)) return std::strong_ordering::less;
//			if(rt_e(l.rtargetId) > rt_e(r.rtargetId)) return std::strong_ordering::greater;
//			if(rp_e  (l.rpassId) < rp_e(r.rpassId)  ) return std::strong_ordering::less;
//			if(rp_e  (l.rpassId) > rp_e(r.rpassId)  ) return std::strong_ordering::greater;
//			if(rp_e (l.renderer) < rp_e(r.renderer) ) return std::strong_ordering::less;
//			if(rp_e (l.renderer) > rp_e(r.renderer) ) return std::strong_ordering::greater;
//			return std::strong_ordering::equal;
//		}
//
//
//		class RenderPass {
//		public:
//			static void init(VmaAllocator, RenderPass& dst, RenderPassDescription);
//			static void destroy(VmaAllocator, RenderPass&);
//
//			auto handle() const noexcept { return rp_rpass; }
//
//		private:
//			struct {
//				std::vector<RenderTargetId> rtargets;
//				size_t firstOutput;
//			} rp_io;
//			VkRenderPass rp_rpass;
//		};
//
//
//		class RenderProcess {
//		public:
//			using StepId = RenderStepId;
//			using Step = RenderStep;
//			using Wave = std::vector<StepId>;
//			using Pipeline = std::vector<Wave>;
//
//			class UnsatisfiableDependencyError;
//			template <typename> class UnregisteredIdError;
//			class NonexistentRenderStepError;
//			class UnregisteredRenderTargerError;
//			class UnregisteredRenderPassError;
//			class UnregisteredRendererError;
//
//			RenderProcess(VmaAllocator, std::shared_ptr<spdlog::logger>);
//			~RenderProcess();
//
//			/// \brief Updates and returns the render pipeline, in order of execution.
//			///
//			const Pipeline& getPipeline() &;
//
//			auto& logger() noexcept { return *rproc_logger; }
//
//			auto vma() const noexcept { return rproc_vma; }
//
//			const Step&         getStep         (StepId id)         const { rproc_steps.at(id); }
//			const RenderTarget& getRenderTarget (RenderTargetId id) const { rproc_rtargets.at(id); }
//			const RenderPass&   getRenderPass   (RenderPassId id)   const { rproc_rpasses.at(id); }
//			const Renderer&     getRenderer     (RendererId id)     const { rproc_renderers.at(id); }
//
//			StepId         setStep            (RenderStepInfo);
//			RenderTargetId createRenderTarget (const RenderTargetDescription&);
//			RenderPassId   registerRenderPass (const RenderPassDescription&);
//			RendererId     registerRenderer   (std::shared_ptr<Renderer>);
//			void unsetStep            (StepId id);
//			void destroyRenderTarget  (RenderTargetId id);
//			void unregisterRenderPass (RenderPassId id);
//			void unregisterRenderer   (RendererId id);
//
//		private:
//			util::Moveable<VmaAllocator> rproc_vma;
//			std::shared_ptr<spdlog::logger> rproc_logger;
//			std::unordered_map<StepId, Step>                          rproc_steps;
//			std::unordered_map<RenderTargetId, RenderTarget>          rproc_rtargets;
//			std::unordered_map<RenderPassId, RenderPass>              rproc_rpasses;
//			std::unordered_map<RendererId, std::shared_ptr<Renderer>> rproc_renderers;
//			std::vector<vkutil::Image> rproc_depthBuffers;
//			idgen::IdGenerator<StepId>         rproc_stepIdGen;
//			idgen::IdGenerator<RenderTargetId> rproc_rtargetIdGen;
//			idgen::IdGenerator<RenderPassId>   rproc_rpassIdGen;
//			idgen::IdGenerator<RendererId>     rproc_rendererIdGen;
//			Pipeline rproc_pipeline;
//			bool rproc_pipelineOod;
//		};
//
//
//		class RenderProcess::UnsatisfiableDependencyError : public std::runtime_error {
//		public:
//			UnsatisfiableDependencyError(std::vector<RenderStepId>);
//			const auto& unsatisfiedDependencies() const noexcept { return dep_err_unsatisfied; }
//
//		private:
//			std::vector<RenderStepId> dep_err_unsatisfied;
//		};
//
//
//		template <typename Id>
//		class RenderProcess::UnregisteredIdError : public std::runtime_error {
//		public:
//			UnregisteredIdError(Id);
//			Id id() const noexcept { return id_err_id; }
//
//		private:
//			Id id_err_id;
//		};
//
//		#warning "TODO: check if these are useless"
//		#define DERIVE_(IDT_, DST_) class RenderProcess::DST_ : public RenderProcess::UnregisteredIdError<IDT_> { public: using UnregisteredIdError::UnregisteredIdError; };
//		DERIVE_(RenderStepId,   NonexistentRenderStepError)
//		DERIVE_(RenderTargetId, UnregisteredRenderTargerError)
//		DERIVE_(RenderPassId,   UnregisteredRenderPassError)
//		DERIVE_(RendererId,     UnregisteredRendererError)
//		#undef DERIVE_
//
//	} // namespace OLD_CODE

}
