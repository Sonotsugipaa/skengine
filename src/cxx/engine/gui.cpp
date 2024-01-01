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


		void setViewportScissor(VkViewport& vp, VkRect2D& sc, float extWidth, float extHeight, const ui::ComputedBounds& cbounds) {
			vp = { }; {
				vp.x      = std::floor(cbounds.viewportOffsetLeft * extWidth);
				vp.y      = std::floor(cbounds.viewportOffsetTop  * extHeight);
				vp.width  = std::ceil (cbounds.viewportWidth      * extWidth);
				vp.height = std::ceil (cbounds.viewportHeight     * extHeight);
				vp.minDepth = 0.0f;
				vp.maxDepth = 1.0f;
			}

			sc = { }; {
				sc.offset = {  int32_t(vp.x),      int32_t(vp.y) };
				sc.extent = { uint32_t(vp.width), uint32_t(vp.height) };
			}
		}

	}



	struct DrawContext::EngineAccess {
		static auto& pipelineSet(gui::DrawContext& c) { return c.engine->mGeomPipelines; };
		static auto& placeholderTextCache(gui::DrawContext& c) { return c.engine->mPlaceholderTextCache; };
	};

	using Ea = DrawContext::EngineAccess;


	Element::PrepareState DrawablePolygon::ui_elem_prepareForDraw(LotId, Lot&, unsigned, ui::DrawContext& uiCtx) {
		auto& guiCtx = getGuiDrawContext(uiCtx);
		dpoly_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
		return PrepareState::eReady;
	}


	void DrawablePolygon::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cbounds = ui_elem_getBounds(lot);

		auto& cmd    = guiCtx.drawCmdBuffer;
		auto& extent = guiCtx.engine->getPresentExtent();
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		VkViewport viewport;
		VkRect2D scissor;
		setViewportScissor(viewport, scissor, xfExtent, yfExtent, cbounds);
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

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


	PlaceholderChar::PlaceholderChar(VmaAllocator vma, codepoint_t c):
		ph_char_vma(vma),
		ph_char_codepoint(c),
		ph_char_lastCacheUpdate(0),
		ph_char_upToDate(false)
	{ }


	PlaceholderChar::~PlaceholderChar() {
		assert(ph_char_vma != nullptr); // No double-destruction, no zero init
		if(ph_char_shapeSet) DrawableShapeSet::destroy(ph_char_vma, ph_char_shapeSet);
		ph_char_vma = nullptr;
	}


	Element::PrepareState PlaceholderChar::ui_elem_prepareForDraw(LotId, Lot&, unsigned repeat, ui::DrawContext& uiCtx) {
		auto& guiCtx   = getGuiDrawContext(uiCtx);
		auto& txtCache = Ea::placeholderTextCache(guiCtx);

		auto fetch = [&]() {
			txtCache.fetchChar(ph_char_codepoint);
		};

		auto commit = [&]() {
			txtCache.updateImage(guiCtx.drawCmdBuffer, VkFence(VK_NULL_HANDLE));
			auto& chars = txtCache.getChars();
			auto& charBounds = chars.find(ph_char_codepoint)->second;
			if(ph_char_lastCacheUpdate != txtCache.getUpdateCounter()) ph_char_upToDate = false;
			if(! ph_char_upToDate) { // Update the shape set
				float u[2] = { charBounds.topLeftUv[0], charBounds.bottomRightUv[0] };
				float v[2] = { charBounds.topLeftUv[1], charBounds.bottomRightUv[1] };
				float x[2] = { -charBounds.size[0], +charBounds.size[0] };
				float y[2] = { -charBounds.size[1], +charBounds.size[1] };
				auto shape = std::make_shared<Shape>(std::vector<TextVertex> {
					{{ x[0], y[0], 0.0f }, { u[0], v[0] }}, {{ x[0], y[1], 0.0f }, { u[0], v[1] }},
					{{ x[1], y[1], 0.0f }, { u[1], v[1] }}, {{ x[1], y[0], 0.0f }, { u[1], v[0] }} });
				auto shapeRef = ShapeReference(shape, { 1.0f, 1.0f, 1.0f, 1.0f }, glm::mat4(1.0f));
				if(ph_char_shapeSet) DrawableShapeSet::destroy(ph_char_vma, ph_char_shapeSet);
				ph_char_shapeSet = DrawableShapeSet::create(ph_char_vma, ShapeSet { shapeRef });
				ph_char_preparedChar = charBounds;
				ph_char_upToDate = true;
			}
			ph_char_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
		};

		switch(repeat) {
			case 0: fetch();  return PrepareState::eDefer;
			case 1: commit(); return PrepareState::eReady;
			default: std::unreachable(); abort();
		}
	}


	void PlaceholderChar::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cbounds = ui_elem_getBounds(lot);

		auto& cmd      = guiCtx.drawCmdBuffer;
		auto& extent   = guiCtx.engine->getPresentExtent();
		auto& txtCache = Ea::placeholderTextCache(guiCtx);
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		VkViewport viewport;
		VkRect2D scissor;
		setViewportScissor(viewport, scissor, xfExtent, yfExtent, cbounds);

		auto& chars = txtCache.getChars();
		auto& charBounds = chars.find(ph_char_codepoint)->second;
		float baselineToBottom = charBounds.size[1] - charBounds.baseline[1];
		float shift = baselineToBottom * viewport.height;
		scissor.offset.y += std::clamp<float>(shift, 0, yfExtent - scissor.extent.height);
		viewport.y       += std::clamp<float>(shift, 0, yfExtent - viewport.height);

		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		VkBuffer vtx_buffers[] = { ph_char_shapeSet.vertexBuffer(), ph_char_shapeSet.vertexBuffer() };
		VkDeviceSize offsets[] = { ph_char_shapeSet.instanceCount() * sizeof(geom::Instance), 0 };
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Ea::pipelineSet(guiCtx).text);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Ea::pipelineSet(guiCtx).layout, 0, 1, &Ea::placeholderTextCache(guiCtx).dset(), 0, nullptr);
		vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
		vkCmdDrawIndirect(cmd, ph_char_shapeSet.drawIndirectBuffer(), 0, ph_char_shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
	}


	ComputedBounds PlaceholderChar::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback PlaceholderChar::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}


	void PlaceholderChar::setChar(codepoint_t c) noexcept {
		ph_char_codepoint = c;
		ph_char_upToDate = false;
	}


	PlaceholderTextCacheView::PlaceholderTextCacheView(VmaAllocator vma, TextCache& cache):
		tcv_vma(vma),
		tcv_cache(&cache)
	{
		constexpr float x[2] = { -1.0f, +1.0f };
		constexpr float y[2] = { -1.0f, +1.0f };
		constexpr float u[2] = { 0.0f, 1.0f };
		constexpr float v[2] = { 0.0f, 1.0f };
		static auto shape = std::make_shared<Shape>(std::vector<TextVertex> {
			{{ x[0], y[0], 0.0f }, { u[0], v[0] }}, {{ x[0], y[1], 0.0f }, { u[0], v[1] }},
			{{ x[1], y[1], 0.0f }, { u[1], v[1] }}, {{ x[1], y[0], 0.0f }, { u[1], v[0] }} });
		static auto shapeRef = ShapeReference(shape, { 1.0f, 0.7f, 0.7f, 1.0f }, glm::mat4(1.0f));
		tcv_shapeSet = DrawableShapeSet::create(tcv_vma, ShapeSet { shapeRef });
	}


	PlaceholderTextCacheView::~PlaceholderTextCacheView() {
		DrawableShapeSet::destroy(tcv_vma, tcv_shapeSet);
		tcv_vma = nullptr;
	}


	Element::PrepareState PlaceholderTextCacheView::ui_elem_prepareForDraw(LotId, Lot&, unsigned, ui::DrawContext& uiCtx) {
		auto& guiCtx = getGuiDrawContext(uiCtx);
		tcv_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
		return PrepareState::eReady;
	}


	void PlaceholderTextCacheView::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cbounds = ui_elem_getBounds(lot);

		auto& cmd    = guiCtx.drawCmdBuffer;
		auto& extent = guiCtx.engine->getPresentExtent();
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		VkViewport viewport;
		VkRect2D scissor;
		setViewportScissor(viewport, scissor, xfExtent, yfExtent, cbounds);
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		VkBuffer vtx_buffers[] = { tcv_shapeSet.vertexBuffer(), tcv_shapeSet.vertexBuffer() };
		VkDeviceSize offsets[] = { tcv_shapeSet.instanceCount() * sizeof(geom::Instance), 0 };
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Ea::pipelineSet(guiCtx).text);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Ea::pipelineSet(guiCtx).layout, 0, 1, &tcv_cache->dset(), 0, nullptr);
		vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
		vkCmdDrawIndirect(cmd, tcv_shapeSet.drawIndirectBuffer(), 0, tcv_shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
	}


	ComputedBounds PlaceholderTextCacheView::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback PlaceholderTextCacheView::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}

}
