#pragma once

#ifdef VS_CODE_HEADER_CHAIN_LINTING_WORKAROUND
	#ifndef VS_CODE_HEADER_CHAIN_LINTING_WORKAROUND_ENGINE_HPP
		#include <engine.hpp>
		#define VS_CODE_HEADER_CHAIN_LINTING_WORKAROUND_ENGINE_HPP
	#endif
#endif



namespace SKENGINE_NAME_NS {

	class GuiManager {
	public:
		friend ConcurrentAccess;

		ui::Canvas& canvas() noexcept { return *gm_engine->mGuiState.canvas; }

		auto createBasicShape(ui::Lot&, ShapeSet, bool doFill);

	private:
		GuiManager(Engine* e): gm_engine(e) { }
		Engine* gm_engine;
	};


	inline GuiManager ConcurrentAccess::gui() const noexcept { return GuiManager(ca_engine); }


	inline auto GuiManager::createBasicShape(ui::Lot& lot, ShapeSet shapes, bool doFill) {
		auto elem = std::make_shared<gui::BasicPolygon>(gm_engine->mVma, std::move(shapes), doFill);
		auto r    = lot.createElement(elem);
		return std::pair(r.first, elem);
	}

}
