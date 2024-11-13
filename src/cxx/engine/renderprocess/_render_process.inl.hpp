#pragma once

#include "render_process.hpp"



namespace SKENGINE_NAME_NS {

	namespace {

		template <idgen::ScopedEnum T>
		constexpr auto idFromIndex(T idx) noexcept {
			return T(std::underlying_type_t<T>(idx) + idgen::baseId<std::underlying_type_t<T>>());
		}

		template <idgen::ScopedEnum T>
		constexpr auto idFromIndex(std::underlying_type_t<T> idx) noexcept {
			return T(idx + idgen::baseId<std::underlying_type_t<T>>());
		}

		template <idgen::ScopedEnum T>
		constexpr auto idToIndex(T id) noexcept {
			return std::underlying_type_t<T>(id) - idgen::baseId<std::underlying_type_t<T>>();
		}


		constexpr auto rtargetNegIdFromIndex(render_target_id_e idx) noexcept {
			using Id = RenderTargetId;
			using id_e = render_target_id_e;
			return Id(- (idx + id_e(1)));
		}

		constexpr auto rtargetNegIdFromIndex(RenderTargetId idx) noexcept { return rtargetNegIdFromIndex(render_target_id_e(idx)); }

		constexpr auto rtargetNegIdToIndex(RenderTargetId id) noexcept {
			using Id = RenderTargetId;
			using id_e = render_target_id_e;
			return Id(- (id_e(id) - id_e(1)));
		}

	}



	struct RprocRpassCreateVectorCache {
		using ImageViewVec3 = std::vector<std::vector<std::vector<VkImageView>>>;
		using AtchRefIndices = std::tuple<size_t, size_t, size_t>;
		std::vector<VkAttachmentDescription> atchDescs;
		std::vector<VkAttachmentReference>   atchRefs;
		std::vector<AtchRefIndices>          atchRefIndices;
		std::vector<VkSubpassDescription>    subpassDescs;
		std::vector<VkSubpassDependency>     subpassDeps;
		ImageViewVec3                        subpassAtchViews;
		RprocRpassCreateVectorCache(size_t subpassCount = 0, size_t gframeCount = 0) {
			auto atchHeuristic = (subpassCount * size_t(3)) / size_t(2);
			if(atchHeuristic > 0) {
				atchDescs       .reserve(atchHeuristic);
				atchRefs        .reserve(atchHeuristic);
				atchRefIndices  .reserve(atchHeuristic);
				subpassDescs    .reserve(atchHeuristic);
				subpassDeps     .reserve(atchHeuristic);
				subpassAtchViews.reserve(atchHeuristic * std::max(size_t(1), gframeCount));
			}
		}
		void clear() {
			atchDescs       .clear();
			atchRefs        .clear();
			atchRefIndices  .clear();
			subpassDescs    .clear();
			subpassDeps     .clear();
			subpassAtchViews.clear();
		}
	};

	struct RprocRpassCreateInfo {
		Logger& logger;
		VmaAllocator vma;
		size_t gframeCount;
		const RenderTargetStorage& rtargetStorage;
		VkFormat depthImageFormat;
	};

	void createRprocRpass(
		RenderPass* dst,
		size_t rpassIdx,
		const RenderPassDescription*,
		const RprocRpassCreateInfo&,
		const RenderTargetStorage&,
		RprocRpassCreateVectorCache&
	);

	void destroyRprocRpass(RenderPass*, VmaAllocator);

}
