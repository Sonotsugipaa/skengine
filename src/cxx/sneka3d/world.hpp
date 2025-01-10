#pragma once

#include <cstring>
#include <bit>
#include <memory>

#include <posixfio_tl.hpp>

#include "memrange.inl.hpp"
#include "basic_unordered_sets.hpp"



namespace sneka {

	using std::byte;


	template <std::integral Dst, std::integral T>
	constexpr Dst hashValues(T x) noexcept { return x; }

	template <std::integral Dst, std::integral T0, std::integral T1, std::integral... T>
	constexpr Dst hashValues(T0 x0, T1 x1, T... x) noexcept {
		using UnsignedDst = std::make_unsigned_t<Dst>;
		return Dst(
			UnsignedDst(  std::rotl<Dst>(x0, 4)) +
			UnsignedDst(~ std::rotr<Dst>(x0, 7)) +
			UnsignedDst(~ hashValues<Dst>(x1, x...)) );
	}


	template <std::integral T>
	struct Vec2 {
		T x;
		T y;
		constexpr auto hash() const noexcept { return hashValues<size_t>(x, y); }
		constexpr bool operator==(this auto& l, const Vec2& r) noexcept { return (l.x == r.x) && (l.y == r.y); }
	};


	using grid_object_class_e = uint8_t;
	enum class GridObjectClass : grid_object_class_e {
		eNoObject = 0,
		eBoost    = 1,
		ePoint    = 2,
		eObstacle = 3,
		eWall     = 4 };


	class World {
	public:
		class v1;

		struct BadFile {
			enum class Reason : size_t {
				eUnexpectedEof = 1,
				eBadMagicNo    = 2,
				eBadVersion    = 3,
				eBadString     = 4,
				eBadAttribData = 4
			}; using enum Reason;
			Reason reason;
			size_t errorOffset;
		};


		enum class AttributeType : uint32_t {
			eEndOfAttribs     = 1,
			eSceneryModel     = 2,
			eObjectClassModel = 3,
			ePlayerHeadModel  = 4 };

		struct Attribute {
			AttributeType type;
			uint32_t      length;
			std::unique_ptr<std::byte[]> data;
			operator std::string_view() const noexcept { return std::string_view(reinterpret_cast<const char*>(data.get()), reinterpret_cast<const char*>(data.get()) + length); }
		};


		Attribute createAttrib(AttributeType type, size_t contentSize, const void* contentPtr);
		Attribute createAttrib(AttributeType type, std::string_view str) { return createAttrib(type, str.size(), str.data()); }

		static World fromFile(const char* filename);
		static World initEmpty(uint64_t width, uint64_t height);
		void toFile(const char* filename);


		const auto& getSceneryModel() const noexcept { return world_models.scenery; }
		const auto& getPlayerHeadModel() const noexcept { return world_models.playerHead; }
		const auto& getObjBoostModel() const noexcept { return world_models.objBoost; }
		const auto& getObjPointModel() const noexcept { return world_models.objPoint; }
		const auto& getObjWallModel() const noexcept { return world_models.objWall; }
		const auto& getObjObstacleModel() const noexcept { return world_models.objObstacle; }
		void setSceneryModel(std::string_view name) { world_models.scenery = std::string(name); }
		void setPlayerHeadModel(std::string_view name) { world_models.playerHead = std::string(name); }
		void setObjBoostModel(std::string_view name) { world_models.objBoost = std::string(name); }
		void setObjPointModel(std::string_view name) { world_models.objPoint = std::string(name); }
		void setObjWallModel(std::string_view name) { world_models.objWall = std::string(name); }
		void setObjObstacleModel(std::string_view name) { world_models.objObstacle = std::string(name); }

		auto  width () const { return world_width ; }
		auto  height() const { return world_height; }
		auto& tile(uint64_t x, uint64_t y)       { return * reinterpret_cast<GridObjectClass*>(world_rawGrid + (y * width()) + x); }
		auto  tile(uint64_t x, uint64_t y) const { return const_cast<World*>(this)->tile(x, y); }

	private:
		struct ModelStrings {
			std::string scenery;
			std::string playerHead;
			std::string objBoost;
			std::string objPoint;
			std::string objObstacle;
			std::string objWall;
		} world_models;
		MemoryRange world_mem;
		byte* world_rawGrid;
		uint64_t world_version;
		uint64_t world_width;
		uint64_t world_height;
	};

}
