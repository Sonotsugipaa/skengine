#pragma once

#include <engine/types.hpp>
#include <engine/renderer.hpp>
#include <engine/ui-structure/ui.hpp>
#include <engine/draw-geometry/core.hpp>

#include <vk-util/memory.hpp>



namespace SKENGINE_NAME_NS {

	class GuiManager;


	struct FontRequirement {
		unsigned short size;

		constexpr bool operator< (const FontRequirement& r) const noexcept { return size <  r.size; }
		constexpr bool operator==(const FontRequirement& r) const noexcept { return size == r.size; }

		struct Hash {
			constexpr std::size_t operator()(const FontRequirement& l, const FontRequirement& r) const noexcept {
				return l.size == r.size;
			}
		};
	};


	/// \brief A Renderer for basic UI elements.
	///
	class UiRenderer : public Renderer {
	public:
		friend GuiManager;

		struct RdrParams {
			static const RdrParams defaultParams;
			std::string fontLocation;
			uint32_t fontMaxCacheSize;
		};

		struct GframeData {
			std::unordered_map<FontRequirement, vkutil::ManagedImage, FontRequirement::Hash> fontImages;
		};

		UiRenderer();
		UiRenderer(UiRenderer&&);
		~UiRenderer();

		static UiRenderer create(VmaAllocator, RdrParams, Logger);
		static void destroy(UiRenderer&);

		std::string_view name() const noexcept override { return "ui"; }
		void prepareSubpasses(const SubpassSetupInfo&, VkPipelineCache, ShaderCacheInterface*) override;
		void forgetSubpasses(const SubpassSetupInfo&) override;
		void afterSwapchainCreation(ConcurrentAccess&, unsigned ) override;
		void duringPrepareStage(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) override;
		void duringDrawStage(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) override;
		void afterRenderPass(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) override;
		void afterPostRender(ConcurrentAccess&, const DrawInfo&) override;

		FontFace createFontFace();
		TextCache& getTextCache(unsigned short size);

		void trimTextCaches(codepoint_t maxCharCount);
		void forgetTextCacheFences() noexcept;

		auto getDsetLayout() const noexcept { return mState.dsetLayout; }
		auto& getPipelineSet() const noexcept { return mState.pipelines; }

		/// \brief This function serves a temporary yet important role, that must be restructured-out as soon as possible.
		///
		void setSrcRtargetId_TMP_UGLY_NAME(RenderTargetId id) { mState.srcRtarget = id; }

	private:
		struct {
			Logger logger;
			RdrParams rdrParams;
			std::shared_ptr<ShaderCacheInterface> shaderCache;
			std::vector<GframeData> gframes;
			std::unique_ptr<ui::Canvas> canvas;
			std::unordered_map<unsigned short, TextCache> textCaches;
			VmaAllocator vma;
			VkDescriptorSetLayout dsetLayout;
			VkPipelineLayout pipelineLayout;
			RenderTargetId srcRtarget;
			geom::PipelineSet pipelines;
			FT_Library freetype;
			bool initialized : 1;
		} mState;
	};

}
