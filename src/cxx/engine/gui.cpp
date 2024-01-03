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


		void setViewportScissor(ViewportScissor& vs, float extWidth, float extHeight, const ui::ComputedBounds& cbounds) {
			vs.viewport = { }; {
				vs.viewport.x      = std::floor(cbounds.viewportOffsetLeft * extWidth);
				vs.viewport.y      = std::floor(cbounds.viewportOffsetTop  * extHeight);
				vs.viewport.width  = std::ceil (cbounds.viewportWidth      * extWidth);
				vs.viewport.height = std::ceil (cbounds.viewportHeight     * extHeight);
				vs.viewport.minDepth = 0.0f;
				vs.viewport.maxDepth = 1.0f;
			}

			vs.scissor = { }; {
				vs.scissor.offset = {  int32_t(vs.viewport.x),      int32_t(vs.viewport.y) };
				vs.scissor.extent = { uint32_t(vs.viewport.width), uint32_t(vs.viewport.height) };
			}
		}

	}



	struct DrawContext::EngineAccess {
		static auto& pipelineSet(gui::DrawContext& c) { return c.engine->mGuiState.geomPipelines; };
		static auto& placeholderTextCache(gui::DrawContext& c) { return c.engine->mGuiState.textCache; };
	};

	using Ea = DrawContext::EngineAccess;


	void DrawContext::insertDrawJob(VkPipeline pl, VkDescriptorSet imageDset, const ViewportScissor& vs, DrawableShapeSet* ds) {
		drawJobs
		[pl]
		[vs]
		[imageDset]
		.push_back(DrawJob {
			.pipeline = pl,
			.viewportScissor = vs,
			.imageDset = imageDset,
			.shapeSet = ds });
	}


	Element::PrepareState DrawablePolygon::ui_elem_prepareForDraw(LotId, Lot&, unsigned, ui::DrawContext& uiCtx) {
		auto& guiCtx = getGuiDrawContext(uiCtx);
		dpoly_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
		return PrepareState::eReady;
	}


	void DrawablePolygon::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cbounds = ui_elem_getBounds(lot);

		auto& extent = guiCtx.engine->getPresentExtent();
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		ViewportScissor vs;
		setViewportScissor(vs, xfExtent, yfExtent, cbounds);

		guiCtx.insertDrawJob(Ea::pipelineSet(guiCtx).polyFill, nullptr, vs, &dpoly_shapeSet);
	}


	ComputedBounds DrawablePolygon::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback DrawablePolygon::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}


	#warning "TODO: move this logic to `DrawablePolygon` "
	BasicPolygon::BasicPolygon(VmaAllocator vma, ShapeSet shapes, bool doFill):
		DrawablePolygon(doFill),
		basic_elem_vma(vma)
	{
		auto& sh = DrawablePolygon::shapes();
		sh = DrawableShapeSet::create(vma, shapes);
	}


	BasicPolygon::~BasicPolygon() {
		auto& sh = shapes();
		if(sh) DrawableShapeSet::destroy(basic_elem_vma, sh);
	}


	void BasicPolygon::setShapes(ShapeSet newShapes) {
		auto& oldShapes = shapes();
		if(oldShapes) DrawableShapeSet::destroy(basic_elem_vma, oldShapes);
		oldShapes = DrawableShapeSet::create(basic_elem_vma, std::move(newShapes));
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
			txtCache.updateImage(guiCtx.prepareCmdBuffer, VkFence(VK_NULL_HANDLE));
			auto& chars = txtCache.getChars();
			auto& charBounds = chars.find(ph_char_codepoint)->second;
			if(ph_char_lastCacheUpdate != txtCache.getUpdateCounter()) ph_char_upToDate = false;
			if(! ph_char_upToDate) { // Update the shape set
				float u[2] = { charBounds.topLeftUv[0], charBounds.bottomRightUv[0] };
				float v[2] = { charBounds.topLeftUv[1], charBounds.bottomRightUv[1] };
				float x[2] = {
					-1.0f,
					-1.0f + (charBounds.size[0] * 2.0f) };
				float y[2] = {
					+1.0f - (charBounds.size[1] * 2.0f),
					+1.0f };
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

		auto& extent   = guiCtx.engine->getPresentExtent();
		auto& txtCache = Ea::placeholderTextCache(guiCtx);
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		ViewportScissor vs;
		setViewportScissor(vs, xfExtent, yfExtent, cbounds);

		auto& chars = txtCache.getChars();
		auto& charBounds = chars.find(ph_char_codepoint)->second;
		float baselineToBottom = charBounds.size[1] - charBounds.baseline[1];
		float shift = baselineToBottom * vs.viewport.height;
		vs.scissor.offset.y += std::clamp<float>(shift, 0, yfExtent - vs.scissor.extent.height);
		vs.viewport.y       += std::clamp<float>(shift, 0, yfExtent - vs.viewport.height);

		guiCtx.insertDrawJob(Ea::pipelineSet(guiCtx).text, Ea::placeholderTextCache(guiCtx).dset(), vs, &ph_char_shapeSet);
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

		auto& extent = guiCtx.engine->getPresentExtent();
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		ViewportScissor vs;
		setViewportScissor(vs, xfExtent, yfExtent, cbounds);

		guiCtx.insertDrawJob(Ea::pipelineSet(guiCtx).text, Ea::placeholderTextCache(guiCtx).dset(), vs, &tcv_shapeSet);
	}


	ComputedBounds PlaceholderTextCacheView::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback PlaceholderTextCacheView::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}

}
