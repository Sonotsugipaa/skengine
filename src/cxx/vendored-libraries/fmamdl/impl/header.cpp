#include <fmamdl/fmamdl.hpp>

#include "string.inl.hpp"

#include <cassert>



namespace fmamdl {

	namespace mdl {
		constexpr std::size_t OFF_MAGIC_NO    = 8 * 0;
		constexpr std::size_t OFF_FLAGS       = 8 * 1;
		constexpr std::size_t OFF_STR_STORAGE = 8 * 2;
		constexpr std::size_t OFF_STR_COUNT   = 8 * 3;
		constexpr std::size_t OFF_STR_SIZE    = 8 * 4;
		constexpr std::size_t OFF_MTL_TABLE   = 8 * 5;
		constexpr std::size_t OFF_MTL_COUNT   = 8 * 6;
		constexpr std::size_t OFF_MESH_TABLE  = 8 * 7;
		constexpr std::size_t OFF_MESH_COUNT  = 8 * 8;
		constexpr std::size_t OFF_BONE_TABLE  = 8 * 9;
		constexpr std::size_t OFF_BONE_COUNT  = 8 * 10;
		constexpr std::size_t OFF_FACE_TABLE  = 8 * 11;
		constexpr std::size_t OFF_FACE_COUNT  = 8 * 12;
		constexpr std::size_t OFF_IDX_TABLE   = 8 * 13;
		constexpr std::size_t OFF_IDX_COUNT   = 8 * 14;
		constexpr std::size_t OFF_VTX_TABLE   = 8 * 15;
		constexpr std::size_t OFF_VTX_COUNT   = 8 * 16;
		constexpr std::size_t OFF_VTX_LAYOUT  = 8 * 17;
	}


	struct MemoryCarriage {
		const std::byte* ptr;
		std::size_t offset;
		std::size_t limit;

		void seekSet(std::size_t i) {
			using ssize = std::make_signed_t<std::size_t>;
			ptr    = (ptr - ssize(offset)) + ssize(i);
			offset = i;
		}

		void seekCur(std::make_signed_t<std::size_t> i) {
			ptr    += i;
			offset += i;
		}

		void checkAccess(std::size_t size) {
			if(offset + size > limit) throw UnexpectedEofError(offset + size);
		}

		template <Numeric T>
		const T& read() {
			assert((offset % sizeof(T) == 0) && "Carriage is not aligned to the type");
			checkAccess(sizeof(T));
			auto r = reinterpret_cast<const T*>(ptr);
			seekCur(sizeof(T));
			return *r;
		}

		template <Numeric T>
		void write(const T& value) {
			assert((offset % sizeof(T) == 0) && "Carriage is not aligned to the type");
			checkAccess(sizeof(T));
			(* reinterpret_cast<T*>(const_cast<std::byte*>(ptr))) = value;
			seekCur(sizeof(T));
		}

		Nstr readNstr() {
			Nstr r = {
				reinterpret_cast<const char*>(ptr + 2),
				read<u2_t>() };

			// Add 1 in both cases due to the easily forsakeable null terminator
			u2_t inc;
			if(r.size % 2 == 0) {
				inc = r.size + 2;
			} else {
				inc = r.size + 1;
			}
			checkAccess(r.size + 1);
			seekCur(inc);

			if('\0' != r.base[r.size]) {
				throw ExpectedStringTerminatorError(offset - 1);
			}

			return r;
		}

		void writeNstr(std::string_view str) {
			auto sz        = str.size();
			auto szAligned = sz + ((sz % 2 == 0)? 2 : 1);
			write<u2_t>(sz);
			auto mptr = reinterpret_cast<char*>(const_cast<std::byte*>(ptr));
			checkAccess(sz + 1);
			memcpy(mptr, str.data(), sz);
			mptr[sz] = '\0';
			seekCur(szAligned);
		}
	};


	const u8_t&        HeaderView::magicNumber() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_MAGIC_NO); }
	const HeaderFlags& HeaderView::flags()       const { return accessPrimitive<HeaderFlags>(data, length, mdl::OFF_FLAGS); }

	const u8_t&      HeaderView::stringStorageOffset() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_STR_STORAGE); }
	const u8_t&      HeaderView::stringStorageSize()   const { return accessPrimitive<u8_t>(data, length, mdl::OFF_STR_SIZE); }
	const u8_t&      HeaderView::stringCount()         const { return accessPrimitive<u8_t>(data, length, mdl::OFF_STR_COUNT); }
	const std::byte* HeaderView::stringPtr()           const { return data + stringStorageOffset(); }

	const u8_t&     HeaderView::materialTableOffset() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_MTL_TABLE); }
	const u8_t&     HeaderView::materialCount()       const { return accessPrimitive<u8_t>(data, length, mdl::OFF_MTL_COUNT); }
	const Material* HeaderView::materialPtr()         const { return reinterpret_cast<const Material*>(data + materialTableOffset()); }

	const u8_t& HeaderView::meshTableOffset() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_MESH_TABLE); }
	const u8_t& HeaderView::meshCount()       const { return accessPrimitive<u8_t>(data, length, mdl::OFF_MESH_COUNT); }
	const Mesh* HeaderView::meshPtr()         const { return reinterpret_cast<const Mesh*>(data + meshTableOffset()); }

	const u8_t& HeaderView::boneTableOffset() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_BONE_TABLE); }
	const u8_t& HeaderView::boneCount()       const { return accessPrimitive<u8_t>(data, length, mdl::OFF_BONE_COUNT); }
	const Bone* HeaderView::bonePtr()         const { return reinterpret_cast<const Bone*>(data + boneTableOffset()); }

	const u8_t& HeaderView::faceTableOffset() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_FACE_TABLE); }
	const u8_t& HeaderView::faceCount()       const { return accessPrimitive<u8_t>(data, length, mdl::OFF_FACE_COUNT); }
	const Face* HeaderView::facePtr()         const { return reinterpret_cast<const Face*>(data + faceTableOffset()); }

	const u8_t&  HeaderView::indexTableOffset() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_IDX_TABLE); }
	const u8_t&  HeaderView::indexCount()       const { return accessPrimitive<u8_t>(data, length, mdl::OFF_IDX_COUNT); }
	const Index* HeaderView::indexPtr()         const { return reinterpret_cast<const Index*>(data + indexTableOffset()); }

	const u8_t&   HeaderView::vertexTableOffset() const { return accessPrimitive<u8_t>(data, length, mdl::OFF_VTX_TABLE); }
	const u8_t&   HeaderView::vertexCount()       const { return accessPrimitive<u8_t>(data, length, mdl::OFF_VTX_COUNT); }
	const Vertex* HeaderView::vertexPtr()         const { return reinterpret_cast<const Vertex*>(data + vertexTableOffset()); }


	std::size_t HeaderView::requiredBytesFor(const Layout& layout) noexcept {
		return
			(8 * 17) // Fixed width data
			+ (3 + layout.stringLength());
	}


	const char* HeaderView::getCstring(StringOffset offset) const {
		return getStringView(offset).begin();
	}

	const std::string_view HeaderView::getStringView(StringOffset offset) const {
		return accessNstr(stringPtr(), SIZE_MAX, string_offset_e(offset));
	}


	Layout HeaderView::getVertexLayout() const {
		auto cstr = accessNstr(data, length, mdl::OFF_VTX_LAYOUT);
		return Layout::fromCstring(cstr.data());
	}


	void HeaderView::setVertexLayout(const Layout& v) {
		auto sv = Layout::toStringView(v);
		assert(length >= mdl::OFF_VTX_LAYOUT + sv.length());
		writeNstr(data, length, mdl::OFF_VTX_LAYOUT, sv);
	}

}
