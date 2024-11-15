#include "ui.hpp"

#include <cstring>



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
		lot_parentRegion(parentGrid),
		lot_child(nullptr)
	{ }


	Lot::Lot(Grid* parentGrid, Region* parentRegion, GridPosition gridOffset, GridSize size):
		Lot(parentGrid, gridOffset, size)
	{
		lot_parentRegion = parentRegion;
	}


	void Lot::setSize(GridSize size) noexcept {
		lot_size = size;
	}


	RelativeSize Lot::getTileSize(GridPosition pos) const noexcept {
		pos.row    += lot_gridOffset.row;
		pos.column += lot_gridOffset.column;
		return lot_parent->grid_getTileSize(pos);
	}


	ComputedBounds Lot::getBounds() const noexcept {
		GridPosition br = lot_gridOffset;
		br.row += lot_size.rows;
		br.column += lot_size.columns;
		auto relBounds = lot_parent->grid_getRegionRelativeBounds(lot_gridOffset, br);
		auto parentBounds = lot_parentRegion->region_getBounds();
		ComputedBounds r;
		r.viewportOffsetLeft = parentBounds.viewportOffsetLeft + (relBounds.left * parentBounds.viewportWidth);
		r.viewportOffsetTop  = parentBounds.viewportOffsetTop  + (relBounds.top  * parentBounds.viewportHeight);
		r.viewportWidth  = relBounds.width  * parentBounds.viewportWidth;
		r.viewportHeight = relBounds.height * parentBounds.viewportHeight;
		return r;
	}


	std::pair<ElementId, std::shared_ptr<Element>&> Lot::createElement(std::shared_ptr<Element> elem) {
		using Map = decltype(lot_elements);
		auto ins = lot_elements.insert(Map::value_type(
			lot_parent->grid_elemIdGen->generate(),
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
		lot_parent->grid_elemIdGen->recycle(id);
	}


	Element& Lot::getElement(ElementId id) {
		return * assert_not_end_(lot_elements, id)->second.get();
	}


	std::shared_ptr<BasicGrid> Lot::setChildBasicGrid(const GridInfo& info, init_list<float> rowSizes, init_list<float> columnSizes) {
		auto* rp = new BasicGrid(info, this, rowSizes, columnSizes); // `unique_ptr` would complain about the inaccessible constructor
		auto  srp = std::shared_ptr<BasicGrid>(rp);
		lot_child = srp;
		return srp;
	}


	std::shared_ptr<List> Lot::setChildList(const GridInfo& info, ListDirection direction, float elemSize, init_list<float> subelemSizes) {
		auto* rp = new List(info, this, direction, elemSize, subelemSizes); // `unique_ptr` would complain about the inaccessible constructor
		auto  srp = std::shared_ptr<List>(rp);
		lot_child = srp;
		return srp;
	}


	void Lot::setChildGrid(std::shared_ptr<Grid> container) {
		lot_child = std::move(container);
	}


	void Lot::removeChildGrid() {
		lot_child = nullptr;
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
		auto parentLot  = grid_parent;
		auto parentGrid = parentLot->parentGrid();
		if(parentGrid != this) parentGrid->setModified();
	}


	RelativeBounds Grid::grid_getRegionRelativeBounds(GridPosition tl, GridPosition br) const noexcept {
		RelativeBounds r;

		// tl and br are not guaranteed to be the idiomatic top-left and bottom-right points
		if(tl.row    > br.row   ) [[unlikely]] std::swap(tl.row,    br.row   );
		if(tl.column > br.column) [[unlikely]] std::swap(tl.column, br.column);

		auto measureRect = [&](GridPosition rectTl, GridPosition rectBr) {
			struct R { float width; float height; } r = { 0.0f, 0.0f };
			// This function assumes the two parameters are in increasing order
			grid_coord_t lowerBound = std::min(rectTl.row, rectTl.column);
			grid_coord_t upperBound = std::max(rectBr.row, rectBr.column);
			for(grid_coord_t i = lowerBound; i <= upperBound; ++i) {
				auto tile = grid_getTileSize({ i, i });
				if(i >= rectTl.row    && i < rectBr.row   ) r.height += tile.height;
				if(i >= rectTl.column && i < rectBr.column) r.width  += tile.width;
			}
			return r;
		};

		auto rect = measureRect({ 0, 0 }, { tl.row, tl.column });
		r.left = rect.width;
		r.top  = rect.height;
		rect = measureRect({ tl.row, tl.column }, { br.row, br.column });
		r.width  = rect.width;
		r.height = rect.height;

		return r;
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


	ComputedBounds BasicGrid::region_getBounds() const noexcept {
		auto parent = parentLot();
		assert(parent != nullptr);
		return parent->getBounds();
	}


	RelativeSize BasicGrid::grid_getTileSize(GridPosition pos) const noexcept {
		pos.row    = std::clamp<grid_coord_t>(pos.row,    0, std::max<grid_coord_t>(0, grid_coord_t(basic_grid_size.rows   ) - 1));
		pos.column = std::clamp<grid_coord_t>(pos.column, 0, std::max<grid_coord_t>(0, grid_coord_t(basic_grid_size.columns) - 1));
		RelativeSize r;
		if(basic_grid_size.rows    > 0) r.height = basic_grid_rowSizes[pos.row];    else r.height = 1.0f;
		if(basic_grid_size.columns > 0) r.width  = basic_grid_colSizes[pos.column]; else r.width  = 1.0f;
		return r;
	}


	BasicGrid::BasicGrid(const GridInfo& info, Lot* parent, std::initializer_list<float> rows, std::initializer_list<float> cols):
		Grid(info, parent),
		basic_grid_rowSizes(std::make_unique_for_overwrite<float[]>(rows.size())),
		basic_grid_colSizes(std::make_unique_for_overwrite<float[]>(cols.size())),
		basic_grid_size { .rows = rows.size(), .columns = cols.size() }
	{
		memcpy(basic_grid_rowSizes.get(), rows.begin(), rows.size() * sizeof(float));
		memcpy(basic_grid_colSizes.get(), cols.begin(), cols.size() * sizeof(float));
	}

	BasicGrid::BasicGrid(const GridInfo& info, std::initializer_list<float> rows, std::initializer_list<float> cols):
		Grid(info),
		basic_grid_rowSizes(std::make_unique_for_overwrite<float[]>(rows.size())),
		basic_grid_colSizes(std::make_unique_for_overwrite<float[]>(cols.size())),
		basic_grid_size { .rows = rows.size(), .columns = cols.size() }
	{
		memcpy(basic_grid_rowSizes.get(), rows.begin(), rows.size() * sizeof(float));
		memcpy(basic_grid_colSizes.get(), cols.begin(), cols.size() * sizeof(float));
	}


	void List::setSubelementSizes(init_list<float> sizes) {
		list_subelemSizes = std::make_unique_for_overwrite<float[]>(sizes.size());
		memcpy(list_subelemSizes.get(), sizes.begin(), sizes.size() * sizeof(float));
	}


	RelativeBounds List::grid_getRegionRelativeBounds(GridPosition tl, GridPosition br) const noexcept {
		GridPosition p0 = tl;
		GridPosition p1 = br;
		RelativeBounds r;
		if(list_direction == ListDirection::eVertical) {
			p0.row = p1.row = 0;
			r = Grid::grid_getRegionRelativeBounds(p0, p1);
			r.height = float(std::abs(tl.row - br.row));
		} else {
			p0.column = p1.column = 0;
			r = Grid::grid_getRegionRelativeBounds(p0, p1);
			r.width = float(std::abs(tl.column - br.column));
		}
		return r;
	}


	ComputedBounds List::region_getBounds() const noexcept {
		auto parent = parentLot();
		assert(parent);
		return parent->getBounds();
	}


	RelativeSize List::grid_getTileSize(GridPosition pos) const noexcept {
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


	List::List(const GridInfo& info, Lot* parent, ListDirection direction, float elemSize, init_list<float> subelementSizes):
		Grid(info, std::move(parent)),
		list_subelemSizes(std::make_unique_for_overwrite<float[]>(subelementSizes.size())),
		list_elemSize(elemSize),
		list_subelemCount(subelementSizes.size()),
		list_direction(direction)
	{
		memcpy(list_subelemSizes.get(), subelementSizes.begin(), subelementSizes.size() * sizeof(float));
	}


	Canvas::Canvas(ComputedBounds bounds, std::initializer_list<float> rowSizes, std::initializer_list<float> columnSizes):
		canvas_grid(std::unique_ptr<BasicGrid>(
			new BasicGrid(GridInfo(GridTraits::eMayYieldFocus), rowSizes, columnSizes))),
		canvas_bounds(bounds)
	{
		{ // Replicate the behavior of `Grid::createLot`
			canvas_grid->grid_isModified = true;
			canvas_lot = std::unique_ptr<Lot>(new Lot(canvas_grid.get(), this, GridPosition { 0, 0 }, canvas_grid->basic_grid_size));
			canvas_grid->grid_parent = canvas_lot.get();
		}
	}


	void Canvas::setBounds(ComputedBounds bounds) noexcept {
		canvas_bounds = bounds;
	}

}}



#undef assert_not_end_
#undef assert_not_nullptr_
