#pragma once

#include <skengine_fwd.hpp>

#include <input/input.hpp>

#include <idgen.hpp>

#include <glm/mat3x3.hpp>

#include <memory>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <span>
#include <type_traits>

#include "util.inl.hpp"



#define EVENT_ENUM_(TYPE_, NUM_) (event_e(event_type_e(EventType::TYPE_)) | event_e(NUM_))



namespace SKENGINE_NAME_NS {
inline namespace ui {

	using propagation_offset_t = int;
	using pixel_coord_t  = signed;
	using pixel_ucoord_t = unsigned;
	using grid_coord_t  = int_fast64_t;
	using grid_ucoord_t = uint_fast64_t;

	template <typename T> using init_list = std::initializer_list<T>;


	using element_id_e = uint_fast32_t;
	using lot_id_e     = int_fast64_t; // Signed, because `Canvas` creates a special `Lot` with a negative ID
	enum class ElementId : element_id_e { };
	enum class LotId     : lot_id_e { };


	class Element;
	class Lot;
	class Grid;
	class BasicGrid;
	class List;
	class Canvas;


	struct Position {
		struct {
			int x;
			int y;
		} pixel;
		struct {
			float x;
			float y;
		} relative;
	};

	struct GridPosition {
		grid_coord_t row;
		grid_coord_t column;
	};

	struct GridSize {
		grid_ucoord_t rows;
		grid_ucoord_t columns;
	};

	struct ComputedBounds {
		float viewportOffsetLeft;
		float viewportOffsetTop;
		float viewportWidth;
		float viewportHeight;
	};

	struct RelativeSize {
		float width;
		float height;
	};

	struct RelativeBounds {
		float left;
		float top;
		float width;
		float height;
	};

	struct LotPadding {
		float left;
		float top;
		float right;
		float bottom;
	};

	struct DrawContext {
		void* ptr;
	};


	enum class ListDirection : bool {
		eVertical   = false,
		eHorizontal = true,
		eListOfRows    = eVertical,
		eListOfColumns = eHorizontal
	};


	enum class SizeHintType {
		eMinPixelWidth, eMinPixelHeight,
		eMaxPixelWidth, eMaxPixelHeight,
		eMinRelativeWidth, eMinRelativeHeight,
		eMaxRelativeWidth, eMaxRelativeHeight,
		eWeight
	};


	struct SizeHint {
		union Value {
			float    f;
			unsigned i;
		};

		SizeHintType type;
		Value        value;

		#define SCH_ static constexpr SizeHint
		#define MK_HINT_(ST_, VT_) return SizeHint { .type = SizeHintType::ST_, .value = { .VT_ = v } };
		SCH_ minPixelWidth  (unsigned v) noexcept { MK_HINT_(eMinPixelWidth,  i) }
		SCH_ minPixelHeight (unsigned v) noexcept { MK_HINT_(eMinPixelHeight, i) }
		SCH_ maxPixelWidth  (unsigned v) noexcept { MK_HINT_(eMaxPixelWidth,  i) }
		SCH_ maxPixelHeight (unsigned v) noexcept { MK_HINT_(eMaxPixelHeight, i) }
		SCH_ minRelativeWidth  (float v) noexcept { MK_HINT_(eMinRelativeWidth,  f) }
		SCH_ minRelativeHeight (float v) noexcept { MK_HINT_(eMinRelativeHeight, f) }
		SCH_ maxRelativeWidth  (float v) noexcept { MK_HINT_(eMaxRelativeWidth,  f) }
		SCH_ maxRelativeHeight (float v) noexcept { MK_HINT_(eMaxRelativeHeight, f) }
		SCH_ eWeight(float v) { MK_HINT_(eWeight, f) }
		#undef SCH_
		#undef MK_HINT_
	};


	using event_type_e = unsigned;

	constexpr event_type_e eventTypeMask = ~ event_type_e(0xFFF);

	enum class EventType : event_type_e {
		eInputAction = 0x1000,
		eMouseHover  = 0x2000,
		eFocus       = 0x3000
	};


	using event_e = unsigned;

	enum class Event : event_e {
		eInputPress    = EVENT_ENUM_(eInputAction, 1),
		eInputRelease  = EVENT_ENUM_(eInputAction, 2),
		eInputTyped    = EVENT_ENUM_(eInputAction, 3),
		eMouseHoverIn  = EVENT_ENUM_(eMouseHover,  1),
		eMouseHoverOut = EVENT_ENUM_(eMouseHover,  2),
		eElemFocus     = EVENT_ENUM_(eFocus, 1),
		eElemUnfocus   = EVENT_ENUM_(eFocus, 2)
	};

	constexpr EventType eventTypeOf(Event e) {
		return EventType( event_type_e(event_e(e)) & (~eventTypeMask) );
	}


	using event_feedback_e = unsigned;

	enum class EventFeedback {
		ePropagateUpwards = 0,
		eHandled          = 1
	};


	using grid_traits_t = uint_fast8_t;

	enum class GridTraits {
		eIsFocusable             = 0b0001,
		eMayYieldFocus           = 0b0010,
		eMayOverflowHorizontally = 0b0100,
		eMayOverflowVertically   = 0b1000
	};


	struct GridInfo {
		GridTraits traits;

		GridInfo(): GridInfo(GridTraits(0)) { }
		GridInfo(GridTraits t): traits(t) { }
	};


	struct InputActionParameters {
		Input input;
	};

	struct MouseHoverParameters {
		Position position;
	};

	struct FocusParameters {
		Lot* subject;
	};


	class EventData {
	public:
		EventType eventType() const noexcept { return event_data_eventType; }

		/* Note that *Params functions allow the caller to modify the event parameters:
		** this is intended behavior, as events are predictably propagated between elements. */
		#ifdef NDEBUG
			#define CT_CAST_(ET_, R_) noexcept { return event_data_params.R_; }
		#else
			#define CT_CAST_(ET_, R_) { if(event_data_eventType != EventType::ET_) throw std::logic_error("UI event type mismatch"); return event_data_params.R_; }
		#endif
		auto& inputActionParams() CT_CAST_(eInputAction, inputAction)
		auto& mouseHoverParams()  CT_CAST_(eMouseHover,  mouseHover)
		auto& focusParams()       CT_CAST_(eFocus,       focus)
		#undef CT_CAST_

	private:
		union params_u {
			InputActionParameters inputAction;
			MouseHoverParameters  mouseHover;
			FocusParameters       focus;
		};

		params_u  event_data_params;
		EventType event_data_eventType;
	};


	class Element {
	public:
		// DOCUMENTATION HINT:
		// if `ui_elem_prepareForDraw` returns `eDefer` for an element, the caller must ensure that it's called for the
		// same element again, but only after calling the same function for all other (relevant) elements exactly once -
		// allowing single elements to have multiple preparation phases that may depend on a shared resource, such
		// as a cache that is regularly reset and needs to be re-populated and updated before the element is drawn.
		enum class PrepareState { eReady, eDefer };

		virtual ComputedBounds ui_elem_getBounds(const Lot&) const noexcept = 0;
		virtual EventFeedback  ui_elem_onEvent(LotId, Lot&, EventData&, propagation_offset_t) = 0;
		virtual PrepareState   ui_elem_prepareForDraw(LotId, Lot&, unsigned repeatCount, DrawContext&) = 0;
		virtual void           ui_elem_draw(LotId, Lot&, DrawContext&) = 0;
	};


	class Lot {
	public:
		friend Canvas; // `Canvas` creates a special "loopback" lot

		Lot(Grid* parentGrid, GridPosition gridOffset, GridSize size);

		const LotPadding& padding() const noexcept { return lot_padding; }
		void              padding(const LotPadding& v) noexcept { lot_padding = v; }
		void              padding(float left, float top, float right, float bottom) noexcept { lot_padding = { left, top, right, bottom }; }
		const glm::mat3& transform() const noexcept { return lot_transform; }
		void             transform(const glm::mat3& v) noexcept { lot_transform = v; }

		RelativeSize   getTileSize(GridPosition) const noexcept;
		ComputedBounds getBounds() const noexcept;

		auto                parentGrid()             noexcept { return lot_parent; }
		const GridPosition& parentGridOffset() const noexcept { return lot_gridOffset; }

		auto      elements() noexcept { return ContainerIterable<decltype(lot_elements)>(lot_elements.begin(), lot_elements.end()); }
		auto      createElement(std::shared_ptr<Element>) -> std::pair<ElementId, std::shared_ptr<Element>&>;
		void      destroyElement(ElementId);
		Element&  getElement(ElementId);

		BasicGrid& setChildBasicGrid(const GridInfo&, init_list<float> rowSizes = { }, init_list<float> columnSizes = { });
		List&      setChildList(const GridInfo&, ListDirection, float elemSize, init_list<float> subelemSizes = { });
		void       setChildGrid(std::shared_ptr<Grid>);
		auto        childGrid() const noexcept { return lot_child; }
		auto        childGrid()       noexcept { return lot_child; }
		bool     hasChildGrid() const noexcept { return bool(lot_child); }
		void  removeChildGrid();

	private:
		using SptrElement = std::shared_ptr<Element>;
		using SptrGrid = std::shared_ptr<Grid>;
		std::shared_ptr<idgen::IdGenerator<ElementId>> lot_elemIdGen;
		std::map<ElementId, SptrElement> lot_elements;
		GridPosition lot_gridOffset;
		GridSize     lot_size;
		LotPadding   lot_padding;
		glm::mat3    lot_transform;
		Grid*        lot_parent;
		SptrGrid     lot_child;
	};


	class Grid {
	public:
		Grid() = default;

		Grid(const GridInfo& info, Lot* parent):
			grid_lotIdGen(parent->parentGrid()->grid_lotIdGen),
			grid_info(info),
			grid_parent(std::move(parent))
		{ }

		virtual ~Grid() = default;

		auto lots() noexcept { return ContainerIterable<decltype(grid_lots)>(grid_lots.begin(), grid_lots.end()); }
		auto createLot(GridPosition offset, GridSize size) -> std::pair<LotId, std::shared_ptr<Lot>&>;
		void destroyLot(LotId);
		Lot& getLot(LotId);

		auto parentLot() const noexcept { return grid_parent; }
		auto parentLot()       noexcept { return grid_parent; }

		bool isModified() const noexcept { return grid_isModified; }
		void setModified() noexcept;
		void resetModified() noexcept { grid_isModified = false; }

		virtual RelativeBounds grid_getRegionRelativeBounds(GridPosition, GridPosition) const noexcept;

		virtual ComputedBounds grid_getBounds() const noexcept = 0;
		virtual GridSize       grid_gridSize() const noexcept = 0;
		virtual RelativeSize   grid_getTileSize(GridPosition) const noexcept = 0;

	protected:
		LotId grid_genId() noexcept { return grid_lotIdGen->generate(); }
		LotId grid_recycleId() noexcept { return grid_lotIdGen->generate(); }

		std::shared_ptr<idgen::IdGenerator<LotId>> grid_lotIdGen;
		std::unordered_map<LotId, std::shared_ptr<Lot>> grid_lots;
		GridInfo grid_info;
		Lot*     grid_parent;
		bool     grid_isModified = false;
	};


	class BasicGrid : public Grid {
	public:
		friend Lot; // `Lot` can create a BasicGrid

		BasicGrid() = default;
		BasicGrid(BasicGrid&&) = default;
		BasicGrid& operator=(BasicGrid&&) = default;
		~BasicGrid() override = default;

		void setRowSizes(init_list<float>);
		void setColumnSizes(init_list<float>);
		auto getRowSizes()    const noexcept { return std::span<float, std::dynamic_extent>(basic_grid_rowSizes.get(), basic_grid_size.rows); }
		auto getColumnSizes() const noexcept { return std::span<float, std::dynamic_extent>(basic_grid_colSizes.get(), basic_grid_size.columns); }

		virtual ComputedBounds grid_getBounds() const noexcept override;
		virtual RelativeSize grid_getTileSize(GridPosition) const noexcept override;
		virtual GridSize grid_gridSize() const noexcept override { return basic_grid_size; }

	private:
		BasicGrid(const GridInfo&, Lot* parent, init_list<float> rowSizes = { }, init_list<float> columnSizes = { });

		std::unique_ptr<float[]> basic_grid_rowSizes;
		std::unique_ptr<float[]> basic_grid_colSizes;
		GridSize basic_grid_size;
	};


	/*
		| subelem 0 size |        subelem 1 size        |   subelem 2 size   |
		| subelem 0 size |        subelem 1 size        |   subelem 2 size   |
		| subelem 0 size |        subelem 1 size        |   subelem 2 size   |
		...
	*//**/
	class List : public Grid {
	public:
		friend Lot; // `Lot` can create a List

		List() = default;
		List(List&&) = default;
		List& operator=(List&&) = default;
		~List() override = default;

		void setElementSize(float elemSize) noexcept { list_elemSize = elemSize; }
		void setSubelementSizes(init_list<float>);
		auto getElementSize()     const noexcept { return list_elemSize; }
		auto getSubelementSizes() const noexcept { return std::span<float, std::dynamic_extent>(list_subelemSizes.get(), list_subelemCount); }

		virtual RelativeBounds grid_getRegionRelativeBounds(GridPosition, GridPosition) const noexcept override;

		virtual ComputedBounds grid_getBounds() const noexcept override;
		virtual RelativeSize grid_getTileSize(GridPosition) const noexcept override;
		virtual GridSize grid_gridSize() const noexcept override;

	private:
		List(const GridInfo&, Lot* parent, ListDirection, float elemSize, init_list<float> subelementSizes = { });

		std::unique_ptr<float[]> list_subelemSizes;
		float                    list_elemSize;
		size_t                   list_subelemCount;
		ListDirection            list_direction;
	};


	class Canvas : public BasicGrid {
	public:
		Canvas() = default;
		Canvas(Canvas&&) = default;
		Canvas& operator=(Canvas&&) = default;

		Canvas(ComputedBounds, init_list<float> rowSizes = { }, init_list<float> columnSizes = { });

		ComputedBounds grid_getBounds() const noexcept override { return canvas_bounds; }

		void setBounds(ComputedBounds) noexcept;

	private:
		ComputedBounds canvas_bounds;
	};

}}



#undef EVENT_ENUM_
