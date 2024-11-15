#pragma once

#include <cstring>
#include <cassert>
#include <bit>
#include <memory>

#include <posixfio_tl.hpp>



namespace sneka {

	using std::byte;

	enum class GridObjectClass : uint8_t { eNoObject = 0, eBoost = 1, ePoint = 2, eObstacle = 3, eWall = 4 };


	struct World {
		struct Header {
			uint64_t width;
			uint64_t height;

			void toLittleEndian() noexcept {
				if constexpr (std::endian::native == std::endian::big) {
					width  = std::byteswap(width);
					height = std::byteswap(height);
				}
			}
		};

		struct BadFile {
			size_t atByte;
		};


		std::unique_ptr<byte[]> rawData;


		static World fromFile(const char* filename) {
			auto file = posixfio::File::open(filename, posixfio::OpenFlags::eRdonly);
			Header h;
			auto rd = posixfio::readAll(file, &h, sizeof(Header));
			if(rd != sizeof(Header)) throw BadFile { size_t(rd) };
			h.toLittleEndian();
			size_t gridBytes = sizeof(uint8_t) * h.width * h.height;
			auto r = World { .rawData = std::make_unique_for_overwrite<byte[]>(sizeof(Header) + gridBytes) };
			memcpy(r.rawData.get(), &h, sizeof(Header));
			rd = posixfio::readAll(file, r.rawData.get() + sizeof(Header), gridBytes);
			if(size_t(rd) != gridBytes) throw BadFile { sizeof(Header) + size_t(rd) };
			return r;
		}

		static World initEmpty(uint64_t width, uint64_t height) {
			size_t gridBytes = sizeof(uint8_t) * width * height;
			auto r = World { .rawData = std::make_unique_for_overwrite<byte[]>(sizeof(Header) + gridBytes) };
			Header& h = * reinterpret_cast<Header*>(r.rawData.get());
			h = { width, height };
			h.toLittleEndian();
			memset(r.rawData.get() + sizeof(Header), 0, gridBytes);
			return r;
		}

		void toFile(const char* filename) {
			assert(rawData);
			auto file = posixfio::File::open(filename, posixfio::OpenFlags::eWronly);
			Header& h = * reinterpret_cast<Header*>(rawData.get());
			posixfio::writeAll(file, rawData.get(), sizeof(Header) * h.width * h.height);
		}


		auto  width () const { assert(rawData); return reinterpret_cast<const Header*>(rawData.get())->width ; }
		auto  height() const { assert(rawData); return reinterpret_cast<const Header*>(rawData.get())->height; }
		auto& tile(uint64_t x, uint64_t y)       { assert(rawData); return * reinterpret_cast<GridObjectClass*>(rawData.get() + sizeof(Header) + (y * width()) + x); }
		auto  tile(uint64_t x, uint64_t y) const { return const_cast<World*>(this)->tile(x, y); }
	};

}
