#pragma once

#ifdef VS_CODE_HEADER_CHAIN_LINTING_WORKAROUND
	#ifndef VS_CODE_HEADER_CHAIN_LINTING_WORKAROUND_ENGINE_HPP
		#include <engine.hpp>
		#include <ui_renderer.hpp>
		#define VS_CODE_HEADER_CHAIN_LINTING_WORKAROUND_ENGINE_HPP
	#endif
#endif



namespace SKENGINE_NAME_NS {

	class GuiManager {
	public:
		friend ConcurrentAccess;

		ui::Canvas& canvas() noexcept { return *gm_uiRenderer->mState.canvas; }

		auto createBasicShape(ui::Lot&, ShapeSet, bool doFill);
		auto createTextLine(ui::Lot&, float depth, const TextInfo&, std::u32string);
		auto createTextLine(ui::Lot&, float depth, const TextInfo&, std::string);

	private:
		GuiManager(UiRenderer* e): gm_uiRenderer(e) { }
		UiRenderer* gm_uiRenderer;
	};


	inline GuiManager ConcurrentAccess::gui() const noexcept { return GuiManager(ca_engine->mUiRenderer_TMP_UGLY_NAME); }


	inline auto GuiManager::createBasicShape(ui::Lot& lot, ShapeSet shapes, bool doFill) {
		auto elem = std::make_shared<gui::BasicPolygon>(gm_uiRenderer->mState.vma, std::move(shapes), doFill);
		auto r    = lot.createElement(elem);
		return std::pair(r.first, std::move(elem));
	}


	inline auto GuiManager::createTextLine(ui::Lot& lot, float depth, const TextInfo& textInfo, std::u32string text = { }) {
		auto elem = std::make_shared<gui::TextLine>(gm_uiRenderer->mState.vma, depth, textInfo, text);
		auto r    = lot.createElement(elem);
		return std::pair(r.first, std::move(elem));
	}

	inline auto GuiManager::createTextLine(ui::Lot& lot, float depth, const TextInfo& textInfo, std::string text) { return createTextLine(lot, depth, textInfo, std::u32string(text.begin(), text.end())); }

}
