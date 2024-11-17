#pragma once

#include <cstring>
#include <bit>
#include <memory>

#include <posixfio_tl.hpp>

#include "memrange.inl.hpp"



namespace sneka {

	using std::byte;

	enum class GridObjectClass : uint8_t {
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
				eBadString     = 4
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


		static World fromFile(const char* filename);
		static World initEmpty(uint64_t width, uint64_t height);
		void toFile(const char* filename);


		auto  width () const { return world_width ; }
		auto  height() const { return world_height; }
		auto& tile(uint64_t x, uint64_t y)       { return * reinterpret_cast<GridObjectClass*>(world_rawGrid + (y * width()) + x); }
		auto  tile(uint64_t x, uint64_t y) const { return const_cast<World*>(this)->tile(x, y); }

	private:
		struct ModelStrings {
			Attribute scenery;
			Attribute playerHead;
			Attribute objWall;
		} world_models;
		MemoryRange world_mem;
		byte* world_rawGrid;
		uint64_t world_version;
		uint64_t world_width;
		uint64_t world_height;
	};

}
