#pragma once

#include <engine/engine.hpp>

#include "ui_renderer.hpp"
#include "gui.hpp"



namespace SKENGINE_NAME_NS {

	class GuiManager {
	public:
		GuiManager(std::shared_ptr<UiRenderer> e): gm_uiRenderer(e) { }

		ui::Canvas& canvas() noexcept { return *gm_uiRenderer->mState.canvas; }

		auto createBasicShape(ui::Lot&, ShapeSet, bool doFill);
		auto createTextLine(ui::Lot&, float depth, const TextInfo&, std::u32string);
		auto createTextLine(ui::Lot&, float depth, const TextInfo&, std::string);

	private:
		std::shared_ptr<UiRenderer> gm_uiRenderer;
	};


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
