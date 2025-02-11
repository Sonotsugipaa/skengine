#include <vulkan/vulkan.h> // VK_NO_PROTOTYPES, defined in other headers, makes it necessary to include this one first

#include "gui.hpp"

#include <engine/engine.hpp>

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



	void DrawContext::insertDrawJob(const DrawJob& job) {
		// VkPipeline pl,
		// VkDescriptorSet imageDset,
		// const ViewportScissor& vs,
		// DrawableShapeSet* ds,
		// glm::vec3 offset, glm::vec3 scale
		// ---
		// .push_back(DrawJob {
		// 	.pipeline = pl,
		// 	.viewportScissor = vs,
		// 	.imageDset = imageDset,
		// 	.shapeSet = ds,
		// 	.transform = { { offset.x, offset.y, offset.z }, { scale.x, scale.y, scale.z } } });
		drawJobs
		[job.pipeline]
		[job.viewportScissor]
		[job.imageDset]
		.push_back(job);
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

		auto& pipelines = guiCtx.uiRenderer->getPipelineSet();

		DrawJob job = {
			.pipeline = dpoly_doFill? pipelines.polyFill : pipelines.polyLine,
			.viewportScissor = vs,
			.imageDset = nullptr,
			.shapeSet = &dpoly_shapeSet,
			.transform = { { }, { 1.0f, 1.0f, 1.0f } } };

		guiCtx.insertDrawJob(job);
	}


	ComputedBounds DrawablePolygon::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
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
		if(txt_str.empty()) {
			txt_upToDate = true;
			return PrepareState::eReady;
		}

		auto& guiCtx   = getGuiDrawContext(uiCtx);
		auto& txtCache = guiCtx.uiRenderer->getTextCache(txt_info.fontSize);

		struct Pen {
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
			float height = charBounds.size[1] * 2.0f;
			float y[2] = {
				off[1] +2.0f - height,
				off[1] +2.0f };
			auto shape = std::make_shared<Shape>(std::vector<TextVertex> {
				{{ x[0], y[0], 0.0f }, { u[0], v[0] }},
				{{ x[0], y[1], 0.0f }, { u[0], v[1] }},
				{{ x[1], y[1], 0.0f }, { u[1], v[1] }},
				{{ x[1], y[0], 0.0f }, { u[1], v[0] }} });
			constexpr auto color = glm::vec4 { 1.0f, 1.0f, 1.0f, 1.0f };
			glm::mat4 mat = glm::translate(mat1, { off[0], off[1], 0.0f });
			auto shapeRef = ShapeReference(std::move(shape), color, mat);
			dst.push_back(std::move(shapeRef));
			pen.x += charBounds.advance[0];
		};

		auto fetch = [&]() {
			txtCache.fetchChars(txt_str);
		};

		auto updateTxtImage = [&]() {
			txtCache.updateImage(guiCtx.prepareCmdBuffer);
		};

		auto commit = [&]() {
			if(txt_lastCacheUpdate != txtCache.getUpdateCounter()) txt_upToDate = false;
			if(! txt_upToDate) { // Update the shape set
				auto& chars = txtCache.getChars();
				auto face = txtCache.fontFace()->ftFace();
				float faceUnits = face->units_per_EM;
				ShapeSet refs; refs.reserve(txt_str.size());
				Pen pen = { };
				if(txt_shapeSet) DrawableShapeSet::destroy(txt_vma, txt_shapeSet);
				for(auto c : txt_str) pushCharVertices(refs, pen, chars, c);
				txt_width  = pen.x;
				txt_height = (face->bbox.yMax - face->bbox.yMin) / faceUnits;
				txt_descender = float(-face->descender) / faceUnits;
				txt_shapeSet = DrawableShapeSet::create(txt_vma, std::move(refs));
				txt_upToDate = true;
				txt_lastCacheUpdate = txtCache.getUpdateCounter();
			}

			txt_shapeSet.commitVkBuffers(guiCtx.engine->getVmaAllocator());
		};

		switch(repeat) {
			case 0: fetch();          return PrepareState::eDefer;
			case 1: updateTxtImage(); return PrepareState::eDefer;
			case 2: commit();         return PrepareState::eReady;
			default: std::unreachable(); abort();
		}
	}


	void TextLine::ui_elem_draw(LotId, Lot& lot, ui::DrawContext& uiCtx) {
		if(txt_str.empty()) return;

		auto& guiCtx  = getGuiDrawContext(uiCtx);
		auto  cBounds = ui_elem_getBounds(lot);

		auto& extent   = guiCtx.engine->getPresentExtent();
		auto& txtCache = guiCtx.uiRenderer->getTextCache(txt_info.fontSize);
		float xfExtent = float(extent.width);
		float yfExtent = float(extent.height);

		ViewportScissor vs;
		setViewportScissor(vs, xfExtent, yfExtent, cBounds);

		float baselineMul = 1.0f / (1.0f + txt_descender);
		glm::vec3 scale = {
			txt_info.textSize * baselineMul * yfExtent / xfExtent,
			txt_info.textSize * txt_height * 0.5f / cBounds.viewportHeight,
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

		DrawJob job = {
			.pipeline = guiCtx.uiRenderer->getPipelineSet().text,
			.viewportScissor = vs,
			.imageDset = txtCache.dset(),
			.shapeSet = &txt_shapeSet,
			.transform = { { off.x, off.y, off.z }, { scale.x, scale.y, scale.z } } };

		guiCtx.insertDrawJob(job);
	}


	ComputedBounds TextLine::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
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
		if(str.size() != txt_str.size()) {
			eq = false;
		} else {
			for(size_t i = 0; i < str.size(); ++i) {
				if(char32_t(str[i]) != txt_str[i]) {
					eq = false;
					break;
				}
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

		DrawJob job = {
			.pipeline = guiCtx.uiRenderer->getPipelineSet().text,
			.viewportScissor = vs,
			.imageDset = tcv_cache->dset(),
			.shapeSet = &tcv_shapeSet,
			.transform = { { }, { 1.0f, 1.0f, 1.0f } } };

		guiCtx.insertDrawJob(job);
	}


	ComputedBounds PlaceholderTextCacheView::ui_elem_getBounds(const Lot& lot) const noexcept {
		return lot.getBounds();
	}

}
