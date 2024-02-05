#pragma once

#include <fmamdl/binary.hpp>
#include <fmamdl/fmamdl.hpp>

#include <cassert>
#include <string_view>



namespace fmamdl {

	template <typename T>
	const T& accessPrimitive(const std::byte* base, std::size_t limit, std::size_t byteOffset) {
		assert((byteOffset % sizeof(T) == 0) && "Misaligned access");
		if(byteOffset + sizeof(T) > limit) [[unlikely]] throw OutOfBoundsError(byteOffset);
		return * reinterpret_cast<const T*>(base + byteOffset);
	}

	template <typename T>
	T& accessPrimitive(std::byte* base, std::size_t limit, std::size_t byteOffset) {
		assert((byteOffset % sizeof(T) == 0) && "Misaligned access");
		if(byteOffset + sizeof(T) > limit) [[unlikely]] throw OutOfBoundsError(byteOffset);
		return * reinterpret_cast<T*>(base + byteOffset);
	}

	const std::string_view accessNstr(const std::byte* base, std::size_t limit, std::size_t byteOffset);

	std::string_view accessNstr(std::byte* base, std::size_t limit, std::size_t byteOffset);

	void writeNstr(std::byte* base, std::size_t limit, std::size_t byteOffset, const std::string_view& string);

}
