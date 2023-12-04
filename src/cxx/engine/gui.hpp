#pragma once

#include <glm/vec3.hpp>

#include "draw-geometry/core.hpp"

#include "ui-structure/ui.hpp"

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

			virtual ComputedBounds ui_elem_getBounds(const Lot&) const noexcept override;
			virtual EventFeedback  ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) override;

			auto& shapes() noexcept { return dpoly_shapeSet; }

		private:
			geom::DrawableShapeSet dpoly_shapeSet;
			bool dpoly_doFill;
		};


		class BasicElement : public DrawablePolygon {
		public:
			BasicElement(VmaAllocator, ShapeSet, bool doFill);
			~BasicElement();

		protected:
			VmaAllocator vma() noexcept { return basic_elem_vma; }

		private:
			VmaAllocator basic_elem_vma;
		};


		class Cross : public BasicElement {
		public:
			Cross(VmaAllocator, float strokeLength, float strokeWidth, const glm::vec4& color);

		private:
			float cross_strokeLength;
			float cross_strokeWidth;
		};


		class Frame : public BasicElement {
		public:
			Frame(VmaAllocator, float strokeWidth, const glm::vec4& color);

		private:
			float frame_strokeWidth;
		};

	}

}
