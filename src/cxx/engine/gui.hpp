#pragma once

#include <glm/vec3.hpp>

#include "draw-geometry/core.hpp"

#include "ui-structure/ui.hpp"

#include <vk-util/memory.hpp>

#include <memory>
#include <map>
#include <bit>
#include <deque>
#include <string>
#include <cstring>



namespace SKENGINE_NAME_NS {

	// Forward declaration for gui::DrawContext
	class Engine;



	inline namespace gui {

		struct ViewportScissor {
			struct HashCmp;
			VkViewport viewport;
			VkRect2D   scissor;

			bool operator==(const ViewportScissor& r) const noexcept {
				static_assert((  sizeof(ViewportScissor) == sizeof(viewport) + sizeof(VkRect2D::extent) + sizeof(VkRect2D::offset)  ) && "Padding may result in false negatives");
				return 0 == memcmp(this, &r, sizeof(ViewportScissor));
			}
		};

		struct DrawJob {
			VkPipeline         pipeline;
			ViewportScissor    viewportScissor;
			VkDescriptorSet    imageDset;
			DrawableShapeSet*  shapeSet;
			geom::PushConstant transform;
		};

		struct ViewportScissor::HashCmp {
			size_t operator()(const ViewportScissor& vs) const noexcept {
				constexpr auto ftoi = [](float f) -> size_t {
					return f * float(1<<10);
				};
				constexpr size_t sh = SIZE_WIDTH / 10;
				return size_t(0)
					^ std::rotl(ftoi(vs.viewport.x       ),    0*sh)
					^ std::rotl(ftoi(vs.viewport.y       ),    1*sh)
					^ std::rotl(ftoi(vs.viewport.width   ),    2*sh)
					^ std::rotl(ftoi(vs.viewport.height  ),    3*sh)
					^ std::rotl(ftoi(vs.viewport.minDepth),    4*sh)
					^ std::rotl(ftoi(vs.viewport.maxDepth),    5*sh)
					^ std::rotl(vs.scissor.extent.width,     6*sh)
					^ std::rotl(vs.scissor.extent.height,    7*sh)
					^ std::rotl(size_t(vs.scissor.offset.x), 8*sh)
					^ std::rotl(size_t(vs.scissor.offset.y), 9*sh);
			}

			bool operator()(const ViewportScissor& l, const ViewportScissor& r) const noexcept {
				return operator()(l) < operator()(r);
			}
		};

		using DrawJobDsetSet = std::map<VkDescriptorSet, std::deque<DrawJob>>;
		using DrawJobVsSet   = std::map<ViewportScissor, DrawJobDsetSet, ViewportScissor::HashCmp>;
		using DrawJobSet     = std::map<VkPipeline,      DrawJobVsSet>;


		enum class TextAlignment : unsigned short {
			eLeftTop,    eCenterTop,    eRightTop,
			eLeftCenter, eCenter,       eRightCenter,
			eLeftBottom, eCenterBottom, eRightBottom
		};


		struct TextInfo {
			TextAlignment  alignment;
			unsigned short fontSize;
			float          textSize;
		};


		struct DrawContext {
			static constexpr uint64_t magicNumberValue = 0xff004cff01020304;

			struct EngineAccess; // Hack to allow tight coupling, without massive headaches nor hundreds of redundant pointers

			const uint64_t magicNumber;
			Engine* engine;
			VkCommandBuffer prepareCmdBuffer;
			DrawJobSet drawJobs;

			void insertDrawJob(VkPipeline, VkDescriptorSet imageDset, const ViewportScissor&, DrawableShapeSet*);
			void insertDrawJob(VkPipeline, VkDescriptorSet imageDset, const ViewportScissor&, DrawableShapeSet*, glm::vec3 offset, glm::vec3 scale);
		};


		class DrawablePolygon : public virtual ui::Element {
		public:
			DrawablePolygon(bool doFill): dpoly_doFill(doFill) { }

			DrawablePolygon(DrawablePolygon&&) = delete;

			Element::PrepareState ui_elem_prepareForDraw(LotId, Lot&, unsigned, ui::DrawContext&) override;
			void ui_elem_draw(LotId, Lot&, ui::DrawContext&) override;

			virtual ComputedBounds ui_elem_getBounds(const Lot&) const noexcept override;
			virtual EventFeedback  ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) override;

			auto& shapes() noexcept { return dpoly_shapeSet; }

		private:
			geom::DrawableShapeSet dpoly_shapeSet;
			bool dpoly_doFill;
		};


		class BasicPolygon : public DrawablePolygon {
		public:
			BasicPolygon(VmaAllocator, ShapeSet, bool doFill);
			~BasicPolygon();

			void setShapes(ShapeSet);

		protected:
			VmaAllocator vma() noexcept { return basic_elem_vma; }

		private:
			VmaAllocator basic_elem_vma;
		};


		class TextLine : public ui::Element {
		public:
			// DOCUMENTATION HINT: `textSize` is relative to the viewport, not the element's lot
			TextLine(VmaAllocator, float depth, const TextInfo&, std::string_view);
			TextLine(VmaAllocator, float depth, const TextInfo&, std::u32string = { });
			~TextLine();

			TextLine(TextLine&&) = delete;

			Element::PrepareState ui_elem_prepareForDraw(LotId, Lot&, unsigned, ui::DrawContext&) override;
			void ui_elem_draw(LotId, Lot&, ui::DrawContext&) override;

			virtual ComputedBounds ui_elem_getBounds(const Lot&) const noexcept override;
			virtual EventFeedback  ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) override;

			auto& textInfo() const noexcept { return txt_info; }
			void  textInfo(const TextInfo&) noexcept;

			void setAlignment(TextAlignment a) noexcept { TextInfo ti = txt_info; ti.alignment = a; textInfo(ti); }
			void setFontSize(unsigned short s) noexcept { TextInfo ti = txt_info; ti.fontSize = s;  textInfo(ti); }
			void setTextSize(float s) noexcept          { TextInfo ti = txt_info; ti.textSize = s;  textInfo(ti); }

			float depth() const noexcept { return txt_depth; }
			void depth(float newValue) noexcept { txt_upToDate = (newValue == txt_depth); txt_depth = newValue; }

			void setText(std::string_view) noexcept;
			void setText(std::u32string) noexcept;

		private:
			VmaAllocator txt_vma;
			geom::DrawableShapeSet txt_shapeSet;
			std::u32string txt_str;
			TextCache::update_counter_t txt_lastCacheUpdate;
			float txt_depth;
			float txt_width;
			float txt_height;
			float txt_descender;
			TextInfo txt_info;
			bool txt_upToDate;
		};


		class PlaceholderTextCacheView : public virtual ui::Element {
		public:
			PlaceholderTextCacheView(VmaAllocator, TextCache&);
			~PlaceholderTextCacheView();

			Element::PrepareState ui_elem_prepareForDraw(LotId, Lot&, unsigned, ui::DrawContext&) override;
			void ui_elem_draw(LotId, Lot&, ui::DrawContext&) override;

			virtual ComputedBounds ui_elem_getBounds(const Lot&) const noexcept override;
			virtual EventFeedback  ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) override;

			auto& shapes() noexcept { return tcv_shapeSet; }

		private:
			VmaAllocator tcv_vma;
			TextCache* tcv_cache;
			geom::DrawableShapeSet tcv_shapeSet;
		};

	}

}
