#include <vulkan/vulkan.h> // VK_NO_PROTOTYPES, defined in other headers, makes it necessary to include this one first

#include "gui.hpp"

#include "engine.hpp"

#include <glm/gtc/matrix_transform.hpp>



namespace SKENGINE_NAME_NS::gui {

	namespace {

		gui::DrawContext& getGuiDrawContext(ui::DrawContext& uiCtx) {
			auto& r = * reinterpret_cast<gui::DrawContext*>(uiCtx.ptr);
			assert(r.magicNumber == gui::DrawContext::magicNumberValue);
			return r;
		}

	}



	struct DrawContext::EngineAccess {
		static auto& pipelineSet(gui::DrawContext& c) { return c.engine->mGeomPipelines; };
		static auto& placeholderCharDset(gui::DrawContext& c) { return c.engine->mPlaceholderGlyphDset; };
	};

	using Ea = DrawContext::EngineAccess;


	void DrawablePolygon::ui_elem_prepareForDraw(LotId, Lot&, ui::DrawContext& uiCtx) {
		auto& guiCtx = getGuiDrawContext(uiCtx);
		dpoly_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
	}


	void DrawablePolygon::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cbounds = ui_elem_getBounds(lot);

		auto& cmd    = guiCtx.drawCmdBuffer;
		auto& extent = guiCtx.engine->getPresentExtent();
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		{
			VkViewport viewport = { }; {
				viewport.x      = std::floor(cbounds.viewportOffsetLeft * xfExtent);
				viewport.y      = std::floor(cbounds.viewportOffsetTop  * yfExtent);
				viewport.width  = std::ceil (cbounds.viewportWidth      * xfExtent);
				viewport.height = std::ceil (cbounds.viewportHeight     * yfExtent);
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
			}

			VkRect2D scissor = { }; {
				scissor.offset = {  int32_t(viewport.x),      int32_t(viewport.y) };
				scissor.extent = { uint32_t(viewport.width), uint32_t(viewport.height) };
			}

			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
		}

		VkBuffer vtx_buffers[] = { dpoly_shapeSet.vertexBuffer(), dpoly_shapeSet.vertexBuffer() };
		VkDeviceSize offsets[] = { dpoly_shapeSet.instanceCount() * sizeof(geom::Instance), 0 };
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Ea::pipelineSet(guiCtx).polyFill);
		vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
		vkCmdDrawIndirect(cmd, dpoly_shapeSet.drawIndirectBuffer(), 0, dpoly_shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
	}


	ComputedBounds DrawablePolygon::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback DrawablePolygon::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}


	#warning "TODO: move this logic to `DrawablePolygon` "
	BasicElement::BasicElement(VmaAllocator vma, ShapeSet shapes, bool doFill):
		DrawablePolygon(doFill),
		basic_elem_vma(vma)
	{
		auto& sh = DrawablePolygon::shapes();
		sh = DrawableShapeSet::create(vma, shapes);
	}


	BasicElement::~BasicElement() {
		auto& sh = shapes();
		if(sh) DrawableShapeSet::destroy(basic_elem_vma, sh);
	}


	PlaceholderChar::PlaceholderChar(VmaAllocator vma, const GlyphBitmap& glyph):
		ph_char_vma(vma)
	{
		float x = float(glyph.width) / float(glyph.height);
		float y = 1.0f;
		static auto shape = std::make_shared<Shape>(std::vector<TextVertex> {
			{{ -x, -y, 0.0f }, { 0.0f, 0.0f }}, {{ -x, +y, 0.0f }, { 0.0f, 1.0f }},
			{{ +x, +y, 0.0f }, { 1.0f, 1.0f }}, {{ +x, -y, 0.0f }, { 1.0f, 0.0f }} });
		static auto shapeRef = ShapeReference(shape, { 1.0f, 1.0f, 1.0f, 1.0f }, glm::mat4(1.0f));
		ph_char_shapeSet = DrawableShapeSet::create(vma, ShapeSet { shapeRef });
	}


	PlaceholderChar::~PlaceholderChar() {
		assert(ph_char_vma != nullptr); // No double-destruction, no zero init
		DrawableShapeSet::destroy(ph_char_vma, ph_char_shapeSet);
		ph_char_vma = nullptr;
	}


	void PlaceholderChar::ui_elem_prepareForDraw(LotId, Lot&, ui::DrawContext& uiCtx) {
		auto& guiCtx = getGuiDrawContext(uiCtx);
		ph_char_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
	}


	void PlaceholderChar::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cbounds = ui_elem_getBounds(lot);

		auto& cmd    = guiCtx.drawCmdBuffer;
		auto& extent = guiCtx.engine->getPresentExtent();
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		{
			VkViewport viewport = { }; {
				viewport.x      = std::floor(cbounds.viewportOffsetLeft * xfExtent);
				viewport.y      = std::floor(cbounds.viewportOffsetTop  * yfExtent);
				viewport.width  = std::ceil (cbounds.viewportWidth      * xfExtent);
				viewport.height = std::ceil (cbounds.viewportHeight     * yfExtent);
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
			}

			VkRect2D scissor = { }; {
				scissor.offset = {  int32_t(viewport.x),      int32_t(viewport.y) };
				scissor.extent = { uint32_t(viewport.width), uint32_t(viewport.height) };
			}

			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
		}

		VkBuffer vtx_buffers[] = { ph_char_shapeSet.vertexBuffer(), ph_char_shapeSet.vertexBuffer() };
		VkDeviceSize offsets[] = { ph_char_shapeSet.instanceCount() * sizeof(geom::Instance), 0 };
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Ea::pipelineSet(guiCtx).text);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Ea::pipelineSet(guiCtx).layout, 0, 1, &Ea::placeholderCharDset(guiCtx), 0, nullptr);
		vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
		vkCmdDrawIndirect(cmd, ph_char_shapeSet.drawIndirectBuffer(), 0, ph_char_shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
	}


	ComputedBounds PlaceholderChar::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback PlaceholderChar::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}

}
