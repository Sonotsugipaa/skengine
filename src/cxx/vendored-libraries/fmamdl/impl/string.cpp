#include "string.inl.hpp"

#include <fmamdl/fmamdl.hpp>

#include <cassert>



namespace fmamdl {

	const std::string_view accessNstr(const std::byte* base, std::size_t limit, std::size_t byteOffset) {
		assert((byteOffset % 2 == 0) && "Misaligned access");
		if(byteOffset + 2 > limit) [[unlikely]] throw OutOfBoundsError(byteOffset);
		auto& len = accessPrimitive<u2_t>(base, limit, byteOffset);
		auto* chr = reinterpret_cast<const char*>((&len) + 1);
		if(byteOffset + 2 + len > limit) [[unlikely]] throw OutOfBoundsError(byteOffset);
		return std::string_view(chr, len);
	}

	std::string_view accessNstr(std::byte* base, std::size_t limit, std::size_t byteOffset) {
		assert((byteOffset % 2 == 0) && "Misaligned access");
		if(byteOffset + 2 > limit) [[unlikely]] throw OutOfBoundsError(byteOffset);
		auto& len = accessPrimitive<u2_t>(base, limit, byteOffset);
		auto* chr = reinterpret_cast<char*>((&len) + 1);
		if(byteOffset + 2 + len > limit) [[unlikely]] throw OutOfBoundsError(byteOffset);
		return std::string_view(chr, len);
	}

	void writeNstr(std::byte* base, std::size_t limit, std::size_t byteOffset, const std::string_view& str) {
		assert((byteOffset % 2 == 0) && "Misaligned access");
		if(limit < (byteOffset + 2+1 + str.length())) [[unlikely]] throw OutOfBoundsError(byteOffset);
		auto& len = accessPrimitive<u2_t>(base, limit, byteOffset);
		len = str.length();
		memcpy((&len) + 1, str.data(), len);
		auto& nullterm = accessPrimitive<char>(base, limit, len + 2);
		nullterm = '\0';
	}

}
