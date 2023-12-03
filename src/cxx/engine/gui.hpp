#pragma once

#include <glm/vec3.hpp>

#include "draw-geometry/core.hpp"

#include <ui/ui.hpp>

#include <vk-util/memory.hpp>

#include <memory>



namespace SKENGINE_NAME_NS {

	// Forward declaration for gui::DrawContext
	class Engine;



	inline namespace gui {

		struct DrawContext {
			static constexpr uint64_t magicNumberValue = 0xff004cff01020304;

			const uint64_t magicNumber;
			Engine* engine;
			VkCommandBuffer drawCmdBuffer;
			geom::PipelineSet* pipelineSet;
		};


		class DrawablePolygon : public virtual ui::Element {
		public:
			DrawablePolygon(bool doFill): dpoly_doFill(doFill) { }

			void ui_elem_prepareForDraw(LotId, Lot&, ui::DrawContext&) override;
			void ui_elem_draw(LotId, Lot&, ui::DrawContext&) override;

			auto& shapes() noexcept { return dpoly_shapeSet; }

		private:
			geom::ShapeSet dpoly_shapeSet;
			bool dpoly_doFill;
		};


		class Crosshair : public DrawablePolygon {
		public:
			Crosshair(VmaAllocator, float strokeLengthRelative, float strokeWidthPixels);
			~Crosshair();

			ComputedBounds ui_elem_getBounds(const Lot&) const noexcept override;
			EventFeedback  ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) override;

		private:
			VmaAllocator ch_vma;
			float ch_strokeLength;
			float ch_strokeWidth;
		};

	}

}
