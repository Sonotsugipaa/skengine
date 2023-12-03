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

		const auto crosshairShape = std::make_shared<geom::Shape>(
			std::vector<PolyVertex> {
				{{ -1.0f, -1.0f,  0.0f }},
				{{ -1.0f, +1.0f,  0.0f }},
				{{ +1.0f, +1.0f,  0.0f }},
				{{ +1.0f, -1.0f,  0.0f }} });

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


	Crosshair::Crosshair(VmaAllocator vma, float strokeLengthRelative, float strokeWidthPixels):
			DrawablePolygon(true),
			ch_vma(vma),
			ch_strokeLength(strokeLengthRelative),
			ch_strokeWidth(strokeWidthPixels)
	{
		auto& sh = shapes();
		auto shapeInst0 = geom::ShapeInstance(crosshairShape, { { 0.8f, 0.8f, 0.8f, 1.0f }, glm::mat4(1.0f) });
		auto shapeInst1 = geom::ShapeInstance(crosshairShape, { { 0.8f, 0.8f, 0.8f, 1.0f }, glm::mat4(1.0f) });
		sh = geom::ShapeSet::create(vma, { std::move(shapeInst0), std::move(shapeInst1) });
		auto modShape0 = sh.modifyShapeInstance(0);
		auto modShape1 = sh.modifyShapeInstance(1);
		modShape0.transform = glm::scale(glm::mat4(1.0f), { 1.0f, 0.1f, 1.0f });
		modShape1.transform = glm::scale(glm::mat4(1.0f), { 0.1f, 1.0f, 1.0f });
	}


	Crosshair::~Crosshair() {
		auto& sh = shapes();
		if(sh) ShapeSet::destroy(ch_vma, sh);
	}


	ComputedBounds Crosshair::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback Crosshair::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}

}
