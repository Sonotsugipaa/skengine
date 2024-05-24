#pragma once

#include "types.hpp"
#include "renderer.hpp"
#include "gui.hpp"
#include "draw-geometry/core.hpp"

#include <vk-util/memory.hpp>

#include <spdlog/logger.h>



namespace SKENGINE_NAME_NS {

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


	/// \brief Data shared between all the UI renderers.
	///
	struct UiStorage {
		std::unique_ptr<ui::Canvas> canvas;
		std::unordered_map<unsigned short, TextCache> textCaches;
		geom::PipelineSet geomPipelines;
		VmaAllocator vma;
		FT_Library freetype;
		const char* fontFilePath;

		FontFace createFontFace();
		TextCache& getTextCache(Engine&, unsigned short size);
	};


	/// \brief A Renderer for basic UI elements.
	///
	class UiRenderer : public Renderer {
	public:
		struct GframeData {
			std::unordered_map<FontRequirement, vkutil::ManagedImage, FontRequirement::Hash> fontImages;
		};

		UiRenderer();
		UiRenderer(UiRenderer&&);
		~UiRenderer();

		static UiRenderer create(
			std::shared_ptr<spdlog::logger>,
			std::shared_ptr<UiStorage> );

		static void destroy(UiRenderer&);

		std::string_view name() const noexcept override { return "ui"; }
		void afterSwapchainCreation(ConcurrentAccess&, unsigned) override;
		void duringPrepareStage(ConcurrentAccess&, unsigned, VkCommandBuffer) override;
		void duringDrawStage(ConcurrentAccess&, unsigned, VkCommandBuffer) override;

	private:
		struct {
			std::shared_ptr<spdlog::logger> logger;
			std::shared_ptr<UiStorage> uiStorage;
			std::vector<GframeData> gframes;
			bool initialized : 1;
		} mState;
	};

}
