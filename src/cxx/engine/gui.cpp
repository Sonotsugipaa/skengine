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
		static auto& placeholderTextCache(gui::DrawContext& c, unsigned short size) { return c.engine->mGuiState.getTextCache(*c.engine, size); }
	};

	using Ea = DrawContext::EngineAccess;


	void DrawContext::insertDrawJob(
		VkPipeline pl,
		VkDescriptorSet imageDset,
		const ViewportScissor& vs,
		DrawableShapeSet* ds,
		glm::vec3 offset, glm::vec3 scale
	) {
		drawJobs
		[pl]
		[vs]
		[imageDset]
		.push_back(DrawJob {
			.pipeline = pl,
			.viewportScissor = vs,
			.imageDset = imageDset,
			.shapeSet = ds,
			.transform = { { offset.x, offset.y, offset.z }, { scale.x, scale.y, scale.z } } });
	}

	void DrawContext::insertDrawJob(
		VkPipeline pl,
		VkDescriptorSet imageDset,
		const ViewportScissor& vs,
		DrawableShapeSet* ds
	) {
		drawJobs
		[pl]
		[vs]
		[imageDset]
		.push_back(DrawJob {
			.pipeline = pl,
			.viewportScissor = vs,
			.imageDset = imageDset,
			.shapeSet = ds,
			.transform = { { }, { 1.0f, 1.0f, 1.0f } } });
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

		auto& pipelines = Ea::pipelineSet(guiCtx);
		auto  pipeline  = dpoly_doFill? pipelines.polyFill : pipelines.polyLine;

		guiCtx.insertDrawJob(pipeline, nullptr, vs, &dpoly_shapeSet);
	}


	ComputedBounds DrawablePolygon::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback DrawablePolygon::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}


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


	TextLine::TextLine(VmaAllocator vma, float depth, const TextInfo& ti, std::u32string str):
		txt_vma(vma),
		txt_str(std::move(str)),
		txt_lastCacheUpdate(0),
		txt_depth(depth),
		txt_info(ti),
		txt_upToDate(false)
	{ }

	TextLine::TextLine(VmaAllocator vma, float depth, const TextInfo& ti, std::string_view str):
		TextLine(vma, depth, ti, std::u32string(str.begin(), str.end()))
	{ }


	TextLine::~TextLine() {
		assert(txt_vma != nullptr); // No double-destruction, no zero init
		if(txt_shapeSet) DrawableShapeSet::destroy(txt_vma, txt_shapeSet);
		txt_vma = nullptr;
	}


	Element::PrepareState TextLine::ui_elem_prepareForDraw(LotId, Lot&, unsigned repeat, ui::DrawContext& uiCtx) {
		auto& guiCtx   = getGuiDrawContext(uiCtx);
		auto& txtCache = Ea::placeholderTextCache(guiCtx, txt_info.fontSize);

		struct Pen {
			float maxBaselineToBottom;
			float x;
		};

		auto pushCharVertices = [&](ShapeSet& dst, Pen& pen, const TextCache::CharMap& chars, codepoint_t c) {
			static constexpr auto mat1 = glm::mat4(1.0f);
			auto& charBounds = chars.find(c)->second;
			float baselineToBottom = charBounds.size[1] - charBounds.baseline[1];
			float off[2] = {
				pen.x * 2.0f,
				baselineToBottom };
			float u[2] = { charBounds.topLeftUv[0], charBounds.bottomRightUv[0] };
			float v[2] = { charBounds.topLeftUv[1], charBounds.bottomRightUv[1] };
			float x[2] = {
				0.0f,
				0.0f + (charBounds.size[0] * 2.0f) };
			float y[2] = {
				off[1] +2.0f - (charBounds.size[1] * 2.0f),
				off[1] +2.0f };
			auto shape = std::make_shared<Shape>(std::vector<TextVertex> {
				{{ x[0], y[0], 0.0f }, { u[0], v[0] }},
				{{ x[0], y[1], 0.0f }, { u[0], v[1] }},
				{{ x[1], y[1], 0.0f }, { u[1], v[1] }},
				{{ x[1], y[0], 0.0f }, { u[1], v[0] }} });
			constexpr auto color = glm::vec4 { 1.0f, 1.0f, 1.0f, 1.0f };
			glm::mat4 mat = mat1;
			mat = glm::translate(mat, { off[0], off[1], 0.0f });
			auto shapeRef = ShapeReference(std::move(shape), color, mat);
			dst.push_back(std::move(shapeRef));
			pen.x += charBounds.advance[0];
			pen.maxBaselineToBottom = std::max(baselineToBottom, pen.maxBaselineToBottom);
		};

		auto fetch = [&]() {
			txtCache.fetchChars(txt_str);
		};

		auto commit = [&]() {
			txtCache.updateImage(guiCtx.prepareCmdBuffer);
			if(txt_lastCacheUpdate != txtCache.getUpdateCounter()) txt_upToDate = false;
			if(! txt_upToDate) { // Update the shape set
				auto& chars = txtCache.getChars();
				ShapeSet refs; refs.reserve(txt_str.size());
				Pen pen = { };
				if(txt_shapeSet) DrawableShapeSet::destroy(txt_vma, txt_shapeSet);
				for(auto c : txt_str) pushCharVertices(refs, pen, chars, c);
				txt_width = pen.x;
				txt_baselineBottom = pen.maxBaselineToBottom;
				txt_shapeSet = DrawableShapeSet::create(txt_vma, std::move(refs));
				txt_upToDate = true;
				txt_lastCacheUpdate = txtCache.getUpdateCounter();
			}

			txt_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
		};

		switch(repeat) {
			case 0: fetch();  return PrepareState::eDefer;
			case 1: commit(); return PrepareState::eReady;
			default: std::unreachable(); abort();
		}
	}


	void TextLine::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cBounds = ui_elem_getBounds(lot);

		auto& extent   = guiCtx.engine->getPresentExtent();
		auto& txtCache = Ea::placeholderTextCache(guiCtx, txt_info.fontSize);
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		ViewportScissor vs;
		setViewportScissor(vs, xfExtent, yfExtent, cBounds);

		float baselineMul = 1.0f / (1.0f + txt_baselineBottom);
		glm::vec3 scale = {
			txt_info.textSize * baselineMul * yfExtent / xfExtent,
			txt_info.textSize * 1.0f,
			1.0f };
		glm::vec3 off = { { }, { }, txt_depth };

		switch(txt_info.alignment) { // Horizontal alignment
			case TextAlignment::eLeftTop: [[fallthrough]];
			case TextAlignment::eLeftCenter: [[fallthrough]];
			case TextAlignment::eLeftBottom:
				off.x = -1.0f; break;
			case TextAlignment::eCenterTop: [[fallthrough]];
			case TextAlignment::eCenter: [[fallthrough]];
			case TextAlignment::eCenterBottom:
				off.x = 0.0f - (txt_width * scale.x); break;
			case TextAlignment::eRightTop: [[fallthrough]];
			case TextAlignment::eRightCenter: [[fallthrough]];
			case TextAlignment::eRightBottom:
				off.x = +1.0f - (txt_width * scale.x * 2.0f); break;
		}

		switch(txt_info.alignment) { // Vertical alignment
			case TextAlignment::eLeftTop: [[fallthrough]];
			case TextAlignment::eCenterTop: [[fallthrough]];
			case TextAlignment::eRightTop:
				off.y = -1.0f; break;
			case TextAlignment::eLeftCenter: [[fallthrough]];
			case TextAlignment::eCenter: [[fallthrough]];
			case TextAlignment::eRightCenter:
				off.y = 0.0f - ((1.0f / baselineMul) * scale.y); break;
			case TextAlignment::eLeftBottom: [[fallthrough]];
			case TextAlignment::eCenterBottom: [[fallthrough]];
			case TextAlignment::eRightBottom:
				off.y = +1.0f - ((1.0f / baselineMul) * scale.y * 2.0f); break;
		}

		guiCtx.insertDrawJob(Ea::pipelineSet(guiCtx).text, txtCache.dset(), vs, &txt_shapeSet, off, scale);
	}


	ComputedBounds TextLine::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback TextLine::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}


	void TextLine::textInfo(const TextInfo& ti) noexcept {
		bool eq = true;
		eq = (txt_info.alignment == ti.alignment)? eq : false;
		eq = (txt_info.fontSize  == ti.fontSize )? eq : false;
		eq = (txt_info.textSize  == ti.textSize )? eq : false;
		txt_upToDate = txt_upToDate && eq;
		txt_info = ti;
	}


	void TextLine::setText(std::string_view str) noexcept {
		bool eq = true;
		for(size_t i = 0; i < str.size(); ++i) {
			if(char32_t(str[i]) != txt_str[i]) {
				eq = false;
				break;
			}
		}
		if(eq) return;
		txt_str = std::u32string(str.begin(), str.end());
		txt_upToDate = false;
	}

	void TextLine::setText(std::u32string str) noexcept {
		if(str == txt_str) return;
		txt_str = std::move(str);
		txt_upToDate = false;
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

		guiCtx.insertDrawJob(Ea::pipelineSet(guiCtx).text, tcv_cache->dset(), vs, &tcv_shapeSet);
	}


	ComputedBounds PlaceholderTextCacheView::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}


	EventFeedback PlaceholderTextCacheView::ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) {
		return EventFeedback::ePropagateUpwards;
	}

}
