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



	void DrawablePolygon::ui_elem_prepareForDraw(LotId, Lot&, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		dpoly_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
	}


	void DrawablePolygon::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cbounds = lot.getBounds();

		auto& cmd    = guiCtx.drawCmdBuffer;
		auto& extent = guiCtx.engine->getPresentExtent();
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		{
			VkViewport viewport = { }; {
				viewport.x      = cbounds.viewportOffsetLeft * xfExtent;
				viewport.y      = cbounds.viewportOffsetTop  * yfExtent;
				viewport.width  = cbounds.viewportWidth      * xfExtent;
				viewport.height = cbounds.viewportHeight     * yfExtent;
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
			}

			VkRect2D scissor = { }; {
				scissor.offset = { int32_t(viewport.x),      int32_t(viewport.y) };
				scissor.extent = { uint32_t(viewport.width), uint32_t(viewport.height) };
			}

			vkCmdSetViewport(cmd, 0, 1, &viewport);
			vkCmdSetScissor(cmd, 0, 1, &scissor);
		}

		VkBuffer vtx_buffers[] = { dpoly_shapeSet.vertexBuffer(), dpoly_shapeSet.vertexBuffer() };
		VkDeviceSize offsets[] = { dpoly_shapeSet.instanceCount() * sizeof(PolyInstance), 0 };
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, guiCtx.pipelineSet->polyFill);
		vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
		vkCmdDrawIndirect(cmd, dpoly_shapeSet.drawIndirectBuffer(), 0, dpoly_shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
	}


	ComputedBounds DrawablePolygon::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback DrawablePolygon::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}


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

}
