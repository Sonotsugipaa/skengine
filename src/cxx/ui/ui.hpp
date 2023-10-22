#pragma once

#include <skengine_fwd.hpp>

#include <input/input.hpp>

#include <idgen.hpp>

#include <glm/mat3x3.hpp>

#include <memory>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <span>
#include <type_traits>



#define EVENT_ENUM_(TYPE_, NUM_) (event_e(event_type_e(EventType::TYPE_)) | event_e(NUM_))



namespace SKENGINE_NAME_NS {
inline namespace ui {

	using propagation_offset_t = int;
	using pixel_coord_t  = signed;
	using pixel_ucoord_t = unsigned;
	using grid_coord_t  = int_fast64_t;
	using grid_ucoord_t = uint_fast64_t;


	enum class LotId : grid_ucoord_t { };


	class Element;
	class Container;
	class Lot;
	class Grid;
	class BasicGrid;
	class List;


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
		grid_ucoord_t row;
		grid_ucoord_t column;
	};

	struct GridSize {
		grid_ucoord_t rows;
		grid_ucoord_t columns;
	};

	struct ComputedBounds {
		pixel_coord_t  offsetLeft;
		pixel_coord_t  offsetTop;
		pixel_ucoord_t width;
		pixel_ucoord_t height;
	};

	struct RelativeSize {
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


	using container_traits_t = uint_fast8_t;

	enum class ContainerTraits {
		eIsFocusable             = 0b0001,
		eMayYieldFocus           = 0b0010,
		eMayOverflowHorizontally = 0b0100,
		eMayOverflowVertically   = 0b1000
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
		#define CT_CAST_(ET_, R_) { if(event_data_eventType != EventType::ET_) throw std::logic_error("UI event type mismatch"); return event_data_params.R_; }
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
		virtual ComputedBounds  ui_elem_getBounds() const noexcept = 0;
		virtual EventFeedback   ui_elem_onEvent(Lot&, EventData&, propagation_offset_t) = 0;
		virtual void            ui_elem_draw(DrawContext*) = 0;
		virtual bool            ui_elem_hasBeenModified() const noexcept = 0;
	};


	class Container {
	public:
		Container() = default;
		~Container() = default;

	private:
		RelativeSize    container_viewport;
		RelativeSize    container_scissor;
		ContainerTraits container_traits;
		std::unique_ptr<Grid> container_grid;
	};


	class Lot {
	public:
		Lot(std::vector<std::unique_ptr<Element>> = { });

		const LotPadding& padding() const noexcept { return lot_padding; }
		void              padding(const LotPadding& v) noexcept { lot_padding = v; }
		void              padding(float left, float top, float right, float bottom) noexcept { lot_padding = { left, top, right, bottom }; }
		const glm::mat3& transform() const noexcept { return lot_transform; }
		void             transform(const glm::mat3& v) noexcept { lot_transform = v; }

		RelativeSize   getDesiredTileSize(GridPosition) const noexcept; // IMPLEMENTATION NOTE: get the desired size of the tile from the parent grid
		ComputedBounds getBounds() const noexcept; // IMPLEMENTATION NOTE: get the actual bounds from the elements and the child container

		Grid&               parentGrid()             noexcept { return *lot_parent; }
		const GridPosition& parentGridOffset() const noexcept { return lot_gridOffset; }
		auto                elements()               noexcept { return std::span<Lot::UptrElement>(lot_elements); }

	private:
		using UptrElement   = std::unique_ptr<Element>;
		using UptrContainer = std::unique_ptr<Container>;
		std::vector<UptrElement> lot_elements;
		GridPosition  lot_gridOffset;
		LotPadding    lot_padding;
		glm::mat3     lot_transform;
		Grid*         lot_parent;
		UptrContainer lot_container;
	};


	class Grid {
	public:
		Grid() = default;
		virtual ~Grid() = default;

		std::span<LotId> lots() noexcept;
		LotId            createLot(Lot);
		void             destroyLot(LotId);
		Lot&             getLot(LotId);

		virtual ComputedBounds grid_getBounds() noexcept = 0;
		virtual GridSize       grid_gridSize() noexcept = 0;
		virtual RelativeSize   grid_desiredTileSize(GridPosition) noexcept = 0;

	protected:
		idgen::IdGenerator<LotId>      grid_lotIdGen;
		std::unordered_map<LotId, Lot> grid_lots;
		std::vector<LotId>             grid_lotIds; // Redundant, but allows `grid_lots()` to return a span
		Lot* grid_parent;

		LotId grid_genId() noexcept { return grid_lotIdGen.generate(); }
		LotId grid_recycleId() noexcept { return grid_lotIdGen.generate(); }
	};


	class BasicGrid : public Grid {
	public:
		ComputedBounds grid_getBounds() noexcept override;

		RelativeSize   grid_desiredTileSize(GridPosition) noexcept override;

		GridSize grid_gridSize() noexcept override { return basic_grid_size; }

	private:
		std::unique_ptr<float[]> basic_grid_rowSizes;
		std::unique_ptr<float[]> basic_grid_colSizes;
		GridSize basic_grid_size;
	};


	class List : public Grid {
	public:
		List(ListDirection direction, std::initializer_list<float> subelementSizes);
		List(std::initializer_list<float> subelementSizes): List(ListDirection::eVertical, subelementSizes) { }

		ComputedBounds grid_getBounds() noexcept override;
		RelativeSize   grid_desiredTileSize(GridPosition) noexcept override;

		GridSize grid_gridSize() noexcept override {
			#warning "UN-INLINE THIS"
			grid_ucoord_t elemCount    = grid_lots.size();
			grid_ucoord_t subelemCount = grid_ucoord_t(list_subelemCount);
			return (list_direction == ListDirection::eListOfRows)?
				GridSize { .rows = elemCount,    .columns = subelemCount } :
				GridSize { .rows = subelemCount, .columns = elemCount    } ;
		}

	private:
		std::unique_ptr<float[]> list_subelemSizes;
		size_t                   list_subelemCount;
		ListDirection            list_direction;
	};


	class Canvas : private Grid {
	public:
		Canvas(std::unique_ptr<Grid> underlyingGrid, ComputedBounds bounds):
			canvas_grid(std::move(underlyingGrid)),
			canvas_bounds(bounds)
		{ }

		ComputedBounds grid_getBounds() noexcept override { return canvas_bounds; }
		GridSize       grid_gridSize()  noexcept override { return canvas_grid->grid_gridSize(); }
		RelativeSize   grid_desiredTileSize(GridPosition p) noexcept override { return canvas_grid->grid_desiredTileSize(p); }

		const Grid& grid() const noexcept { return *canvas_grid.get(); }
		Grid&       grid()       noexcept { return *canvas_grid.get(); }

	private:
		std::unique_ptr<Grid> canvas_grid;
		ComputedBounds        canvas_bounds;
	};

}}



#undef EVENT_ENUM_
