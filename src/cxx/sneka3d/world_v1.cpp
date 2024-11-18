#include "world.hpp"

#include <cassert>
#include <concepts>

#include <posixfio_tl.hpp>

#if defined OS_LINUX
	#include <unistd.h>
#elif defined OS_WINDOWS
	#include <sysinfoapi.h>
#endif



namespace sneka {

	namespace {

		constexpr uint64_t magicNo = serialize(0x646c7277'616b6e73);


		template <typename T, size_t N>
		requires std::is_trivially_copyable_v<T>
		void readInto(posixfio::FileView file, T dst[N], size_t& fileCursor) {
			auto rd = posixfio::readAll(file, dst, sizeof(T) * N);
			if(rd != sizeof(T) * N) throw World::BadFile { World::BadFile::eUnexpectedEof, fileCursor + rd };
			fileCursor += rd;
		}

		template <std::integral T>
		void readInto(posixfio::FileView file, T& dst, size_t& fileCursor) {
			auto rd = posixfio::readAll(file, &dst, sizeof(T));
			if(rd != sizeof(T)) throw World::BadFile { World::BadFile::eUnexpectedEof, fileCursor + rd };
			fileCursor += rd;
		}


		size_t mmapOffsetStride() {
			static const auto pageSize = []() -> size_t {
				#if defined OS_LINUX
					return std::max<size_t>(0, sysconf(_SC_PAGESIZE));
				#elif defined OS_WINDOWS
					SYSTEM_INFO si;
					GetSystemInfo(SYSTEM_INFO);
					return si.dwPageSize;
				#else
					return 0;
				#endif
			} ();
			return pageSize;
		}


		struct GenericAttribute {
			uint32_t type;
			uint32_t length;
			std::unique_ptr<std::byte[]> data;
		};


		GenericAttribute readAttribute(posixfio::FileView file, size_t& fileCursor) {
			uint32_t attribLength[2];
			readInto<uint32_t, 2>(file, attribLength, fileCursor);
			GenericAttribute r = { attribLength[0], attribLength[1], nullptr };
			r.type = attribLength[0];
			if(r.length > 0) {
				r.data = std::make_unique_for_overwrite<std::byte[]>(r.length + 1);
				auto rd = posixfio::readAll(file, r.data.get(), r.length + 1);
				if(rd != r.length + 1) throw World::BadFile { World::BadFile::eUnexpectedEof, fileCursor + rd };
				if(static_cast<unsigned char>(r.data[r.length]) != '\0') throw World::BadFile { World::BadFile::eUnexpectedEof, fileCursor + rd };
				fileCursor += rd;
			}
			return r;
		}

	}



	struct World::v1 {

		enum class AttributeType : uint32_t {
			eEndOfAttribs     = 0x1,
			eSceneryModel     = 0x2,
			eObjectClassModel = 0x3,
			ePlayerHeadModel  = 0x4 };


		static World::AttributeType mapAttribType(v1::AttributeType v1a) { switch(v1a) {
			#define MAP_(FROM_, TO_) case v1::AttributeType::FROM_: return World::AttributeType::TO_;
			MAP_(eEndOfAttribs    , eEndOfAttribs    )
			MAP_(eSceneryModel    , eSceneryModel    )
			MAP_(eObjectClassModel, eObjectClassModel)
			MAP_(ePlayerHeadModel , ePlayerHeadModel )
			default: return World::AttributeType(0);
			#undef MAP_
		}}

		static v1::AttributeType mapAttribType(World::AttributeType v1a) { switch(v1a) {
			#define MAP_(FROM_, TO_) case World::AttributeType::FROM_: return v1::AttributeType::TO_;
			MAP_(eEndOfAttribs    , eEndOfAttribs    )
			MAP_(eSceneryModel    , eSceneryModel    )
			MAP_(eObjectClassModel, eObjectClassModel)
			MAP_(ePlayerHeadModel , ePlayerHeadModel )
			default: return v1::AttributeType(0);
			#undef MAP_
		}}


		static World::Attribute translateAttribToWorld(GenericAttribute src, uint32_t begin = 0, uint32_t end = UINT32_MAX) {
			assert(begin <= end);
			auto attribType = mapAttribType(v1::AttributeType(src.type));
			if(begin == 0 && end >= src.length) {
				return World::Attribute { attribType, src.length, std::move(src.data) };
			} else {
				auto cutData = std::make_unique_for_overwrite<std::byte[]>(end - begin);
				memcpy(cutData.get(), src.data.get() + begin, end - begin);
				return World::Attribute { attribType, src.length, std::move(cutData) };
			}
		}

		static GenericAttribute translateAttribFromWorld(World::Attribute src, uint32_t begin = 0, uint32_t end = UINT32_MAX) {
			assert(begin <= end);
			if(begin == 0 && end >= src.length) {
				return GenericAttribute { uint32_t(src.type), src.length, std::move(src.data) };
			} else {
				auto cutData = std::make_unique_for_overwrite<std::byte[]>(end - begin);
				memcpy(cutData.get(), src.data.get() + begin, end - begin);
				return GenericAttribute { uint32_t(src.type), src.length, std::move(cutData) };
			}
		}

		static void objModelFromAttrib(World::ModelStrings* dst, const GenericAttribute& src, size_t fileCursorAtAttrib) {
			using enum GridObjectClass;
			assert(src.type == uint32_t(v1::AttributeType::eObjectClassModel));
			if(src.length < 2) throw BadFile { BadFile::eBadAttribData, fileCursorAtAttrib + sizeof(uint8_t) };
			auto const * const& dataPtr = reinterpret_cast<const char*>(src.data.get());
			auto r = std::string_view(dataPtr + 1, dataPtr + src.length);
			switch(GridObjectClass(src.data[0])) {
				default: throw BadFile { BadFile::eBadAttribData, fileCursorAtAttrib + sizeof(uint8_t) }; break;
				case eNoObject: return;
				case eBoost   : dst->objBoost    = std::move(r); break;
				case ePoint   : dst->objPoint    = std::move(r); break;
				case eObstacle: dst->objObstacle = std::move(r); break;
				case eWall    : dst->objWall     = std::move(r); break;
			}
		}


		static World readFile(posixfio::File file) {
			size_t fileCursor = 2 * sizeof(uint64_t);
			World r = { };
			r.world_version = 1;

			{
				uint64_t widthHeight[2];
				auto rd = readAll(file, widthHeight, sizeof(widthHeight));
				if(rd != sizeof(widthHeight)) throw BadFile { BadFile::eUnexpectedEof, fileCursor + rd };
				fileCursor += rd;
				r.world_width  = deserialize(widthHeight[0]);
				r.world_height = deserialize(widthHeight[1]);
			}

			GenericAttribute attrib;
			auto oldFileCursor = fileCursor;
			while((attrib = readAttribute(file, fileCursor)).type != uint32_t(v1::AttributeType::eEndOfAttribs)) {
				switch(v1::AttributeType(attrib.type)) {
					case v1::AttributeType::eSceneryModel:
						r.world_models.scenery = translateAttribToWorld(std::move(attrib)); break;
					case v1::AttributeType::eObjectClassModel:
						objModelFromAttrib(&r.world_models, attrib, oldFileCursor); break;
					case v1::AttributeType::ePlayerHeadModel:
						r.world_models.playerHead = translateAttribToWorld(std::move(attrib)); break;
					default: break;
				}
				oldFileCursor = fileCursor;
			}

			{ // Skip to the next 4096-byte block and mmap/read the grid
				const size_t gridBytes = r.world_width * r.world_height * sizeof(uint8_t);
				size_t padding = (4096 - (fileCursor % 4096)) % 4096; // 0->0, 1->4095, 4095->1, 4096->0
				bool mrSet = false;

				if(4096 == mmapOffsetStride()) { // Try to mmap, only if the page size is 4096 (the file's content doesn't magically realign itself across platforms)
					using Mmap  = posixfio::MemMapFlags;
					using Mprot = posixfio::MemProtFlags;

					try { // ... to mmap
						auto mmap = file.mmap(gridBytes, Mprot::eRead, Mmap::ePrivate, fileCursor + padding);
						r.world_mem = std::move(mmap);
						r.world_rawGrid = r.world_mem.data();
						mrSet = true;
					} catch(posixfio::Errcode& e) {
						switch(e.errcode) {
							case ENOTSUP: [[fallthrough]];
							case ENXIO: [[fallthrough]];
							case EINVAL: [[fallthrough]];
							case ENODEV: break;
							default: std::rethrow_exception(std::current_exception());
						}
					}
				}

				if(! mrSet) {
					try { // ... to seek to the next 4096-byte block
						file.lseek(fileCursor + padding, posixfio::Whence::eCur);
						mrSet = true;
					} catch(posixfio::Errcode& e) {
						if(e.errcode != ESPIPE) std::rethrow_exception(std::current_exception());
					}

					if(! mrSet) { // Do this the Bad Way
						auto discard = std::make_unique_for_overwrite<std::byte[]>(padding);
						auto rd = posixfio::readAll(file, discard.get(), padding);
						if(rd == 0) throw BadFile { BadFile::eUnexpectedEof, fileCursor };
						fileCursor += padding;
					}

					r.world_mem = MemoryRange::allocate(gridBytes);
					r.world_rawGrid = r.world_mem.data();
					memset(r.world_mem.data(), 0, gridBytes);
					posixfio::readAll(file, r.world_mem.data(), gridBytes);
				}
			}

			return r;
		}


		static void writeFile(posixfio::File file, World& src) {
			auto fb = posixfio::ArrayOutputBuffer<256>(file);
			const size_t gridBytes = src.world_width * src.world_height * sizeof(uint8_t);
			size_t fileCursor = 0;
			const auto write = [&]<std::integral T>(T v) {
				v = serialize(v);
				auto rd = fb.writeAll(&v, sizeof(auto(v)));
				fileCursor += sizeof(auto(v));
				assert(rd == sizeof(auto(v))); (void) rd;
			};
			const auto writeAttrib = [&](AttributeType type, const std::string& attrib) {
				write(uint32_t(mapAttribType(type)));
				write(uint32_t(attrib.size()));
				if(attrib.size() > 0) {
					fb.writeAll(attrib.c_str(), attrib.size() + /* null-term */ 1);
				}
			};
			const auto writeObjMdlAttrib = [&](GridObjectClass objClass, const std::string& attrib) {
				write(uint32_t(mapAttribType(AttributeType::eObjectClassModel)));
				write(uint32_t(sizeof(uint8_t) + attrib.size()));
				if(attrib.size() > 0) {
					write(uint8_t(objClass));
					fb.writeAll(attrib.c_str(), attrib.size() + /* null-term */ 1);
				}
			};
			write(uint64_t(magicNo));
			write(uint64_t(src.world_version));
			write(uint64_t(src.world_width));
			write(uint64_t(src.world_height));
			writeAttrib(AttributeType::eSceneryModel,     src.world_models.scenery);
			writeAttrib(AttributeType::ePlayerHeadModel,  src.world_models.playerHead);
			writeObjMdlAttrib(GridObjectClass::eBoost,    src.world_models.objBoost);
			writeObjMdlAttrib(GridObjectClass::ePoint,    src.world_models.objPoint);
			writeObjMdlAttrib(GridObjectClass::eObstacle, src.world_models.objObstacle);
			writeObjMdlAttrib(GridObjectClass::eWall,     src.world_models.objWall);
			write(uint32_t(v1::AttributeType::eEndOfAttribs));
			write(uint32_t(0));
			size_t padding = (4096 - (fileCursor % 4096)) % 4096; // 0->0, 1->4095, 4095->1, 4096->0
			fb.flush();
			try {
				file.lseek(padding, posixfio::Whence::eCur);
			} catch(posixfio::Errcode& e) {
				constexpr uint64_t zero = 0;
				if(e.errcode != ESPIPE) [[unlikely]] std::rethrow_exception(std::current_exception());
				auto rd = fb.writeAll(&zero, sizeof(zero));
				assert(rd == sizeof(zero)); (void) rd;
				fileCursor += sizeof(zero);
			}
			auto rd = fb.writeAll(src.world_rawGrid, gridBytes);
			assert(rd >= 0 && size_t(rd) == gridBytes);
			fileCursor += rd;
		}

	};



	World::Attribute World::createAttrib(AttributeType type, size_t contentSize, const void* contentPtr) {
		Attribute r = {
			.type = type,
			.length = decltype(Attribute::length)(contentSize),
			.data = contentSize < 1 ? nullptr : std::make_unique_for_overwrite<std::byte[]>(contentSize) };
		if(contentSize > 0) memcpy(r.data.get(), contentPtr, contentSize);
		return r;
	}


	World World::fromFile(const char* filename) {
		using namespace posixfio;
		auto file = File::open(filename, OpenFlags::eRdwr);
		size_t fileCursor = 0;
		uint64_t mnoVersion[2]; {
			auto rd = readAll(file, mnoVersion, sizeof(mnoVersion));
			if(rd != sizeof(mnoVersion)) throw BadFile { BadFile::eUnexpectedEof, fileCursor + rd };
			fileCursor += rd;
			mnoVersion[0] = deserialize(mnoVersion[0]);
			mnoVersion[1] = deserialize(mnoVersion[1]);
		}
		if(mnoVersion[0] != magicNo) throw BadFile { BadFile::eBadMagicNo, 0 };
		switch(mnoVersion[1]) {
			case 1: return v1::readFile(std::move(file));
			default: throw BadFile { BadFile::eBadVersion, sizeof(uint64_t) };
		}
	}

	void World::toFile(const char* filename) {
		using enum posixfio::OpenFlags;
		assert(world_mem.data() != nullptr);
		auto file = posixfio::File::open(filename, eWronly | eCreat | eTrunc);
		v1::writeFile(std::move(file), *this);
	}


	World World::initEmpty(uint64_t width, uint64_t height) {
		const size_t gridBytes = width * height * sizeof(GridObjectClass);
		World r;
		r.world_mem = MemoryRange::allocate(gridBytes);
		r.world_rawGrid = r.world_mem.data();
		r.world_version = 1;
		r.world_width = width;
		r.world_height = height;
		memset(r.world_mem.data(), 0, gridBytes);
		return r;
	}

}
