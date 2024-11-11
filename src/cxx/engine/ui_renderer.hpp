#pragma once

#include "types.hpp"
#include "renderer.hpp"
#include "gui.hpp"
#include "draw-geometry/core.hpp"

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

		struct GframeData {
			std::unordered_map<FontRequirement, vkutil::ManagedImage, FontRequirement::Hash> fontImages;
		};

		UiRenderer();
		UiRenderer(UiRenderer&&);
		~UiRenderer();

		static UiRenderer create(VmaAllocator, Logger, std::string fontFilePath);
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

	private:
		struct {
			Logger logger;
			std::shared_ptr<ShaderCacheInterface> shaderCache;
			std::vector<GframeData> gframes;
			std::unique_ptr<ui::Canvas> canvas;
			std::unordered_map<unsigned short, TextCache> textCaches;
			VmaAllocator vma;
			VkDescriptorSetLayout dsetLayout;
			VkPipelineLayout pipelineLayout;
			geom::PipelineSet pipelines;
			FT_Library freetype;
			std::string fontFilePath;
			bool initialized : 1;
		} mState;
	};

}
