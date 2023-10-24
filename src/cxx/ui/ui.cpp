#include "ui.hpp"



namespace SKENGINE_NAME_NS {
inline namespace ui {

	GridSize List::grid_gridSize() noexcept {
		grid_ucoord_t elemCount    = grid_lots.size();
		grid_ucoord_t subelemCount = grid_ucoord_t(list_subelemCount);
		return (list_direction == ListDirection::eListOfRows)?
			GridSize { .rows = elemCount,    .columns = subelemCount } :
			GridSize { .rows = subelemCount, .columns = elemCount    } ;
	}

}}
