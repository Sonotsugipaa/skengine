#include "ui.hpp"



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace SKENGINE_NAME_NS {
inline namespace ui {

	Lot::Lot(Grid* parentGrid, GridPosition gridOffset, GridSize size):
		lot_elements(),
		lot_gridOffset(gridOffset),
		lot_size(size),
		lot_padding({ 0.0f, 0.0f, 0.0f, 0.0f }),
		lot_transform(glm::mat3(1.0f)),
		lot_parent(parentGrid),
		lot_container(nullptr)
	{
		lot_parent = parentGrid;
		if(parentGrid == nullptr) {
			// This is only meant to be the case when creating a top-level lot,
			// by avoiding "chain-climbing" member function calls or manually
			// setting private member variables after construction.
			lot_elemIdGen = std::make_shared<idgen::IdGenerator<ElementId>>();
		} else {
			auto parentLot = parentGrid->parentLot().lock();
			assert(parentLot);
			lot_elemIdGen = parentLot->lot_elemIdGen;
		}
	}


	RelativeSize Lot::getDesiredTileSize(GridPosition pos) const noexcept {
		pos.row    += lot_gridOffset.row;
		pos.column += lot_gridOffset.column;
		return lot_parent->grid_desiredTileSize(pos);
	}


	ComputedBounds Lot::getBounds() const noexcept {
		GridPosition br = lot_gridOffset;
		br.row += lot_size.rows;
		br.column += lot_size.columns;
		return lot_parent->getRegionBounds(lot_gridOffset, br);
	}


	std::pair<ElementId, std::shared_ptr<Element>&> Lot::createElement(std::shared_ptr<Element> elem) {
		using Map = decltype(lot_elements);
		auto ins = lot_elements.insert(Map::value_type(
			lot_elemIdGen->generate(),
			std::move(elem) ));
		return std::pair<ElementId, std::shared_ptr<Element>&>(ins.first->first, ins.first->second);
	}


	void Lot::destroyElement(ElementId id) {
		#ifndef NDEBUG
			auto removed =
		#endif
		lot_elements.erase(id);
		#ifndef NDEBUG
			assert(removed == 1);
		#endif
		lot_elemIdGen->recycle(id);
	}


	Element& Lot::getElement(ElementId id) {
		return * assert_not_end_(lot_elements, id)->second.get();
	}


	std::pair<LotId, std::shared_ptr<Lot>&> Grid::createLot(GridPosition offset, GridSize size) {
		using Map = decltype(grid_lots);
		setModified();
		auto ins = grid_lots.insert(Map::value_type(
			grid_lotIdGen->generate(),
			std::make_shared<Lot>(this, offset, size) ));
		return std::pair<LotId, std::shared_ptr<Lot>&>(ins.first->first, ins.first->second);
	}


	void Grid::destroyLot(LotId id) {
		auto removed = grid_lots.erase(id);
		if(removed) setModified();
		#ifndef NDEBUG
			assert(removed == 1);
		#endif
		grid_lotIdGen->recycle(id);
	}


	Lot& Grid::getLot(LotId id) {
		return * assert_not_end_(grid_lots, id)->second.get();
	}


	void Grid::setModified() noexcept {
		grid_isModified = true;
		auto parentLot  = grid_parent.lock();
		auto parentGrid = parentLot->parentGrid();
		if(parentGrid != this) parentGrid->setModified();
	}


	ComputedBounds Grid::getRegionBounds(GridPosition tl, GridPosition br) const noexcept {
		ComputedBounds r;

		// tl and br are not guaranteed to be the idiomatic top-left and bottom-right points
		if(tl.row    > br.row   ) [[unlikely]] std::swap(tl.row,    br.row   );
		if(tl.column > br.column) [[unlikely]] std::swap(tl.column, br.column);

		auto measureRect = [&](GridPosition rectTl, GridPosition rectBr) {
			struct R { float width; float height; } r = { 0.0f, 0.0f };
			// This function assumes the two parameters are in increasing order
			grid_coord_t lowerBound = std::min(rectTl.row, rectTl.column);
			grid_coord_t upperBound = std::max(rectBr.row, rectBr.column);
			grid_coord_t i = lowerBound;
			for(; i < upperBound; ++i) {
				auto tile = grid_desiredTileSize({ i, i });
				if(i >= rectTl.row && i < rectBr.column) r.height += tile.height;
				if(i >= rectTl.row && i < rectBr.column) r.width  += tile.width;
			}
			return r;
		};

		auto rect = measureRect({ 0, 0 }, { tl.row, tl.column });
		r.viewportOffsetLeft = rect.width;
		r.viewportOffsetTop  = rect.height;
		rect = measureRect({ tl.row, tl.column }, { br.row, br.column });
		r.viewportWidth  = rect.width;
		r.viewportHeight = rect.height;

		return r;
	}


	BasicGrid::BasicGrid(std::shared_ptr<Lot> parent, std::initializer_list<float> rows, std::initializer_list<float> cols):
		Grid(std::move(parent)),
		basic_grid_rowSizes(std::make_unique_for_overwrite<float[]>(rows.size())),
		basic_grid_colSizes(std::make_unique_for_overwrite<float[]>(cols.size())),
		basic_grid_size { .rows = rows.size(), .columns = cols.size() }
	{
		memcpy(basic_grid_rowSizes.get(), rows.begin(), rows.size() * sizeof(float));
		memcpy(basic_grid_colSizes.get(), cols.begin(), cols.size() * sizeof(float));
	}


	void BasicGrid::setRowSizes(init_list<float> rows) {
		basic_grid_rowSizes = std::make_unique_for_overwrite<float[]>(rows.size());
		memcpy(basic_grid_rowSizes.get(), rows.begin(), rows.size() * sizeof(float));
		basic_grid_size.rows = rows.size();
	}


	void BasicGrid::setColumnSizes(init_list<float> cols) {
		basic_grid_colSizes = std::make_unique_for_overwrite<float[]>(cols.size());
		memcpy(basic_grid_colSizes.get(), cols.begin(), cols.size() * sizeof(float));
		basic_grid_size.columns = cols.size();
	}


	ComputedBounds BasicGrid::grid_getBounds() const noexcept {
		auto parent = parentLot().lock();
		assert(parent);
		return parent->getBounds();
	}


	RelativeSize BasicGrid::grid_desiredTileSize(GridPosition pos) const noexcept {
		pos.row    = std::clamp<grid_coord_t>(pos.row,    0, basic_grid_size.rows);
		pos.column = std::clamp<grid_coord_t>(pos.column, 0, basic_grid_size.columns);
		RelativeSize r;
		if(basic_grid_size.rows    > 0) r.height = basic_grid_rowSizes[pos.row];    else r.height = 1.0f;
		if(basic_grid_size.columns > 0) r.width  = basic_grid_colSizes[pos.column]; else r.width  = 1.0f;
		return r;
	}


	List::List(std::shared_ptr<Lot> parent, float elemSize, init_list<float> subelementSizes):
		List(std::move(parent), ListDirection::eVertical, elemSize, subelementSizes)
	{ }


	List::List(std::shared_ptr<Lot> parent, ListDirection direction, float elemSize, init_list<float> subelementSizes):
		Grid(std::move(parent)),
		list_subelemSizes(std::make_unique_for_overwrite<float[]>(subelementSizes.size())),
		list_elemSize(elemSize),
		list_subelemCount(subelementSizes.size()),
		list_direction(direction)
	{
		memcpy(list_subelemSizes.get(), subelementSizes.begin(), subelementSizes.size() * sizeof(float));
	}


	void List::setSubelementSizes(init_list<float> sizes) {
		list_subelemSizes = std::make_unique_for_overwrite<float[]>(sizes.size());
		memcpy(list_subelemSizes.get(), sizes.begin(), sizes.size() * sizeof(float));
	}


	ComputedBounds List::grid_getBounds() const noexcept {
		auto parent = parentLot().lock();
		assert(parent);
		return parent->getBounds();
	}


	RelativeSize List::grid_desiredTileSize(GridPosition pos) const noexcept {
		// Assume the list is vertical
		pos.column = std::clamp<grid_coord_t>(pos.column, 0, list_subelemCount);
		RelativeSize r;
		if(list_subelemCount > 0) r.width = list_subelemSizes[pos.column]; else r.height = 1.0f;
		r.height = list_elemSize;

		// Swap the row/column counts if the list is NOT vertical, as previously assumed
		if(list_direction != ListDirection::eVertical) {
			assert(list_direction == ListDirection::eHorizontal);
			std::swap(r.width, r.height);
		}

		return r;
	}


	GridSize List::grid_gridSize() const noexcept {
		grid_ucoord_t elemCount    = grid_lots.size();
		grid_ucoord_t subelemCount = grid_ucoord_t(list_subelemCount);
		return (list_direction == ListDirection::eListOfRows)?
			GridSize { .rows = elemCount,    .columns = subelemCount } :
			GridSize { .rows = subelemCount, .columns = elemCount    } ;
	}


	Canvas::Canvas(ComputedBounds bounds, std::initializer_list<float> rowSizes, std::initializer_list<float> columnSizes):
		BasicGrid(),
		canvas_bounds(bounds)
	{
		constexpr LotId loopbackLotId = LotId(-1);

		grid_lotIdGen = std::make_shared<idgen::IdGenerator<LotId>>();

		{ // This replicates the behavior of `Lot::createLot`
			auto lot = std::make_shared<Lot>(nullptr, GridPosition { }, GridSize { rowSizes.size(), columnSizes.size() });
			auto ins = grid_lots.insert(decltype(grid_lots)::value_type(
				loopbackLotId, lot ));
			assert(ins.second);
			grid_parent = ins.first->second;
			lot->lot_parent = this;
		}

		setRowSizes(rowSizes);
		setColumnSizes(columnSizes);
	}


	void Canvas::setBounds(ComputedBounds bounds) noexcept {
		canvas_bounds = bounds;
	}

}}



#undef assert_not_end_
#undef assert_not_nullptr_
