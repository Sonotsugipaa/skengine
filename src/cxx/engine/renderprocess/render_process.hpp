#pragma once

#include "../types.hpp"
#include "../renderer.hpp"

#include <idgen.hpp>

#include <type_traits>
#include <limits>
#include <bit>
#include <set>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <ranges>

#include <vk-util/memory.hpp>

#include <misc-util.tpp>



namespace SKENGINE_NAME_NS {

	struct RenderTargetDescription {
		struct ImageRef { VkImage image; VkImageView imageView; };
		std::shared_ptr<std::vector<ImageRef>> externalImages;
		VkExtent3D extent;
		VkImageUsageFlags usage;
		VkFormat format;
		bool hostReadable         : 1;
		bool hostWriteable        : 1;
		bool hostAccessSequential : 1;
		bool requiresImageView    : 1;
	};


	struct RenderTarget {
		vkutil::ManagedImage  devImage;
		vkutil::ManagedBuffer hostBuffer;
		VkImageView devImageView;
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
			bool requiresDepthAttachments;
		};
		std::vector<Subpass> subpasses;
		VkExtent3D framebufferSize;
		#undef TPR_
	};


	struct RenderPass {
		struct FramebufferData {
			VkFramebuffer handle;
			std::vector<std::pair<vkutil::ManagedImage, VkImageView>> depthImages;
		};
		RenderPassDescription description;
		std::vector<FramebufferData> framebuffers;
		VkRenderPass handle;
		operator bool() const noexcept { return handle != nullptr; }
	};


	template <typename T, typename U>
	concept InputRangeOf = std::ranges::input_range<T> && requires(T t) {
		std::vector<U>().insert(std::vector<U>().end(), t.begin(), t.end());
		requires std::assignable_from<U, decltype(*t.begin())>;
	};


	class RenderTargetStorage {
	public:
		union Entry {
			struct Reference {
				VkImage     image;
				VkImageView imageView;
				VkExtent3D  extent;
				VkFormat    format;
			};
			RenderTarget managed;
			Reference    external;
		};

		struct EntrySet {
			size_t offset     : SIZE_WIDTH-1;
			size_t isExternal : 1;
		};

		struct EntryRange {
			std::ranges::subrange<const Entry*> entries;
			bool isExternal;

			#define CNE_ const noexcept
			auto        getImage(size_t gframe)     CNE_ { auto& e = entries[gframe]; return isExternal? e.external.image     : e.managed.devImage.value; }
			auto        getImageView(size_t gframe) CNE_ { auto& e = entries[gframe]; return isExternal? e.external.imageView : e.managed.devImageView; }
			const auto& getExtent(size_t gframe)    CNE_ { auto& e = entries[gframe]; return isExternal? e.external.extent    : e.managed.devImage.info().extent; }
			const auto& getFormat(size_t gframe)    CNE_ { auto& e = entries[gframe]; return isExternal? e.external.format    : e.managed.devImage.info().format; }
			auto begin () CNE_ { return entries.begin (); }
			auto end   () CNE_ { return entries.end   (); }
			#undef CNE_
		};

		class Factory;

		using Descriptions = std::vector<RenderTargetDescription>;
		using Entries = std::vector<Entry>;
		using Map = std::unordered_map<RenderTargetId, EntrySet>;

		RenderTargetStorage(): rts_vma(nullptr) { }
		RenderTargetStorage(RenderTargetStorage&&);  RenderTargetStorage& operator=(RenderTargetStorage&& mv) { this->~RenderTargetStorage(); return * new (this) RenderTargetStorage(std::move(mv)); }
		~RenderTargetStorage();

		void setRtargetExtent(RenderTargetId, const VkExtent3D&);
		void setGframeCount(size_t);
		void updateRtargetReferences();

		EntryRange getEntrySet(RenderTargetId) const &;
		const RenderTargetDescription& getDescription(RenderTargetId) const &;

		auto gframeCount() const noexcept { return rts_gframeCount; }

		auto getDescriptionsRange () const & noexcept { return std::views::all(rts_descs  ); }
		auto getEntriesRange      () const & noexcept { return std::views::all(rts_entries); }
		auto getEntryMapRange     () const & noexcept { return std::views::all(rts_map    ); }

	private:
		RenderTargetStorage(const RenderTargetStorage&);
		Logger rts_logger;
		VmaAllocator rts_vma;
		Descriptions rts_descs;
		Entries rts_entries;
		Map rts_map; // Maps IDs to (gframeCount-divisible) indices of rts_entries
		size_t rts_gframeCount;
	};


	class RenderProcess {
	public:
		#define DECL_SCOPED_ENUM_(ENUM_, ALIAS_, UNDERLYING_) using ALIAS_ = UNDERLYING_; enum class ENUM_ : ALIAS_ { };
		DECL_SCOPED_ENUM_(StepId,        step_id_e, unsigned)
		DECL_SCOPED_ENUM_(SequenceIndex, seq_idx_e, std::make_unsigned_t<step_id_e>)
		#undef DECL_SCOPED_ENUM_

		using DrawSyncPrimitives = Renderer::DrawSyncPrimitives;

		static constexpr auto nullSequence = idgen::invalidId<SequenceIndex>();

		class UnsatisfiableDependencyError;
		class DependencyGraph;

		struct VulkanState {
			VmaAllocator vma;
			VkFormat depthImageFormat;
			uint32_t queueFamIdx;
			VkDevice device() noexcept { VmaAllocatorInfo i; vmaGetAllocatorInfo(vma, &i); return i.device; }
		};

		struct StepDescription {
			util::TransientPtrRange<VkClearValue> clearColors;
			RenderPassId rpass;
			RendererId renderer;
			VkRect2D renderArea;
		};

		struct Step : StepDescription{
			SequenceIndex seqIndex;
		};

		struct WaveGframeData {
			VkCommandPool cmdPool;
			VkCommandBuffer cmdPrepare;
			VkCommandBuffer cmdDraw;
		};

		class WaveIterator {
		public:
			WaveIterator(): wi_rp(nullptr), wi_seqIdx(SequenceIndex(~ seq_idx_e(0))), wi_firstStep(StepId(0)), wi_stepCount(0) { }
			std::strong_ordering operator<=>(const WaveIterator&) const noexcept;
			bool operator==(const WaveIterator& r) const noexcept { return operator<=>(r) == std::strong_ordering::equal; };
			bool operator!=(const WaveIterator& r) const noexcept { return ! operator==(r); };
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
			WaveIterator begin() noexcept { return beginIter; }
			WaveIterator end()   noexcept { return endIter; }
			WaveIterator beginIter;
			WaveIterator endIter;
		};

		struct SequenceDescription {
			std::shared_ptr<RenderTargetStorage::Factory> rtsFactory;
			std::vector<Step> steps;
			std::vector<RenderPassDescription> rpasses;
			std::vector<std::weak_ptr<Renderer>> renderers;
		};

		struct ExternalRtarget {
			std::vector<VkImage> images;
			VkExtent3D extent;
		};

		struct RtargetResizeInfo {
			RenderTargetId rtarget;
			VkExtent3D newExtent;
		};

		RenderProcess(): rp_gframeCount(0), rp_waveIterValidity(0), rp_initialized(false) { }
		RenderProcess(const RenderProcess&) = delete;
		RenderProcess(RenderProcess&&) = delete;
		#ifndef NDEBUG
			~RenderProcess();
		#endif

		void setup(VmaAllocator vma, Logger, ConcurrentAccess&, VkFormat depthImageFormat, uint32_t queueFamIdx, unsigned gframeCount, const DependencyGraph&);
		void setup(VmaAllocator vma, Logger, ConcurrentAccess&, VkFormat depthImageFormat, uint32_t queueFamIdx, unsigned gframeCount, DependencyGraph&&);
		void setup(VmaAllocator vma, Logger, ConcurrentAccess&, VkFormat depthImageFormat, uint32_t queueFamIdx, unsigned gframeCount, const SequenceDescription&);
		void reset(ConcurrentAccess&, unsigned newGframeCount, util::TransientPtrRange<RtargetResizeInfo> rtargetResizes);
		void reset(ConcurrentAccess&, unsigned newGframeCount, util::TransientPtrRange<RtargetResizeInfo> rtargetResizes, const SequenceDescription&);
		void destroy(ConcurrentAccess&);

		const Step&       getStep       (StepId id) const & { return const_cast<RenderProcess*>(this)->getStep(id); };
		const RenderPass& getRenderPass (RenderPassId id) const & { return const_cast<RenderProcess*>(this)->getRenderPass(id); };
		const Renderer*   getRenderer   (RendererId id) const & { return const_cast<RenderProcess*>(this)->getRenderer(id); };
		Step&             getStep       (StepId) &;
		RenderPass&       getRenderPass (RenderPassId) &;
		Renderer*         getRenderer   (RendererId) &;

		const RenderTargetDescription& getRenderTargetDescription(RenderTargetId) const &;
		const RenderTarget& getRenderTarget(RenderTargetId, size_t subIndex) const &;

		auto waveCount() const noexcept { return rp_waveCmds.size(); }
		auto gframeCount() const noexcept { return rp_gframeCount; }
		VulkanState getVulkanState() const noexcept { return rp_vkState; }

		const DrawSyncPrimitives& getDrawSyncPrimitives(SequenceIndex wave, size_t gframe) const noexcept;
		const WaveGframeData& getWaveGframeData(SequenceIndex wave, size_t gframe) const noexcept;

		WaveRange waveRange() &;
		auto sortedStepRange(this auto& self) { return std::span(self.rp_steps); }

	private:
		Logger rp_logger;
		VulkanState rp_vkState;
		std::vector<std::pair<StepId, Step>> rp_steps;
		std::vector<RenderPass> rp_rpasses;
		std::vector<std::shared_ptr<Renderer>> rp_renderers;
		std::vector<DrawSyncPrimitives> rp_drawSyncPrimitives;
		std::vector<WaveGframeData> rp_waveCmds;
		RenderTargetStorage rp_rtargetStorage;
		unsigned rp_gframeCount;
		unsigned rp_waveIterValidity;
		bool rp_initialized;
	};


	class RenderProcess::DependencyGraph {
	public:
		using Steps = std::vector<Step>;
		using Rtargets = std::vector<RenderTargetDescription>;
		using Rpasses = std::vector<RenderPassDescription>;
		using Renderers = std::vector<std::weak_ptr<Renderer>>;
		using DependencyMap = std::map<StepId, std::set<StepId>>;

		class Subgraph {
		public:
			Subgraph& /* *this comes... */ before (const Subgraph&);
			Subgraph& /* *this comes... */ after  (const Subgraph&);

			StepId stepId() const noexcept { return sg_step; }

		private:
			friend DependencyGraph;
			DependencyGraph* sg_graph;
			StepId sg_step;
		};

		DependencyGraph(Logger logger, size_t gframeCount):
			dg_rtsFactory(std::make_shared<RenderTargetStorage::Factory>(logger, gframeCount))
		{ }

		RenderTargetId addRtarget (RenderTargetDescription);
		RenderPassId   addRpass   (RenderPassDescription);
		RendererId     addRenderer(std::weak_ptr<Renderer>);

		Subgraph addDummyStep();
		Subgraph addStep(StepDescription);

		SequenceDescription assembleSequence() const;

	private:
		friend RenderProcess;
		std::shared_ptr<RenderTargetStorage::Factory> dg_rtsFactory;
		Steps       dg_steps;
		Rpasses     dg_rpasses;
		Renderers   dg_renderers;
		DependencyMap dg_dependencies_fwd; // Key comes before Values
		DependencyMap dg_dependencies_bwd; // Key depends on Values
		void dg_addRtargetReferenceImage(const vkutil::Image&, const VkExtent3D&);
	};


	class RenderTargetStorage::Factory {
	private:
		friend RenderProcess::DependencyGraph;
		RenderTargetStorage dst;

	public:
		Factory(Logger, size_t gframeCount);

		RenderTargetId setRenderTarget(RenderTargetDescription&&);

		size_t entrySetCount() const noexcept { return dst.rts_descs.size(); };

		RenderTargetStorage finalize(VmaAllocator) &;
		RenderTargetStorage finalize(VmaAllocator) &&;
	};


	class RenderProcess::UnsatisfiableDependencyError : public std::runtime_error {
	public:
		UnsatisfiableDependencyError(std::vector<RenderProcess::StepId> dependencyChain);
		const auto& dependencyChain() const noexcept { return dep_err_depChain; }

	private:
		std::vector<RenderProcess::StepId> dep_err_depChain;
	};

}
