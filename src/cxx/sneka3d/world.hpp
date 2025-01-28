#pragma once

#include <cstring>
#include <bit>
#include <memory>
#include <vector>

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

		template <std::integral U> requires (! std::same_as<U, T>)
		constexpr operator Vec2<U>() const noexcept { return { U(x), U(y) }; }
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
			ePlayerHeadModel  = 4,
			ePlayerBodyModel  = 5,
			ePlayerTailModel  = 6,
			eEntryPoint       = 7 };

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


		const auto& getSceneryModel() const noexcept    { return world_models.scenery; }
		const auto& getPlayerHeadModel() const noexcept { return world_models.playerHead; }
		auto getObjBoostModels() const noexcept   { return std::span<const std::string>(world_models.objBoost); }
		auto getObjPointModels() const noexcept   { return std::span<const std::string>(world_models.objPoint); }
		auto getObjWallModels() const noexcept    { return std::span<const std::string>(world_models.objWall); }
		auto getObjObstacleModels() const noexcept { return std::span<const std::string>(world_models.objObstacle); }
		void setSceneryModel(std::string_view name)    { world_models.scenery = std::string(name); }
		void setPlayerHeadModel(std::string_view name) { world_models.playerHead = std::string(name); }
		void addObjBoostModel(std::string_view name)    { world_models.objBoost.push_back(std::string(name)); }
		void addObjPointModel(std::string_view name)    { world_models.objPoint.push_back(std::string(name)); }
		void addObjWallModel(std::string_view name)     { world_models.objWall.push_back(std::string(name)); }
		void addObjObstacleModel(std::string_view name) { world_models.objObstacle.push_back(std::string(name)); }
		void removeObjBoostModel(std::string_view name)    { std::erase(world_models.objBoost, name); }
		void removeObjPointModel(std::string_view name)    { std::erase(world_models.objPoint, name); }
		void removeObjWallModel(std::string_view name)     { std::erase(world_models.objWall, name); }
		void removeObjObstacleModel(std::string_view name) { std::erase(world_models.objObstacle, name); }

		auto  width () const { return world_width ; }
		auto  height() const { return world_height; }

		auto& tile(uint64_t x, uint64_t y)       { return * reinterpret_cast<GridObjectClass*>(world_rawGrid + (y * width()) + x); }
		auto  tile(uint64_t x, uint64_t y) const { return const_cast<World*>(this)->tile(x, y); }
		auto& entryPointX(this auto& self) noexcept { return self.world_entryPoint[0]; }
		auto& entryPointY(this auto& self) noexcept { return self.world_entryPoint[1]; }

	private:
		struct ModelStrings {
			std::string scenery;
			std::string playerHead;
			std::vector<std::string> objBoost;
			std::vector<std::string> objPoint;
			std::vector<std::string> objObstacle;
			std::vector<std::string> objWall;
		} world_models;
		MemoryRange world_mem;
		byte* world_rawGrid;
		uint64_t world_version;
		uint64_t world_width;
		uint64_t world_height;
		uint64_t world_entryPoint[2];
	};

}
