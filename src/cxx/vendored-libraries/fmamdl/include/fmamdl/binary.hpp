#pragma once

#include <bit>
#include <cstdint>
#include <stdfloat>
#include <concepts>



#if   not defined VMAMDL_IGNORE_ENDIANNESS   or   VMAMDL_IGNORE_ENDIANNESS == false
	static_assert(std::endian::native == std::endian::little);
#endif

#if   not defined VMAMDL_IGNORE_SIGN   or   VMAMDL_IGNORE_SIGN == false
	static_assert(std::bit_cast<int32_t, uint32_t>(0xffffffff) == -1);
	static_assert(std::bit_cast<int32_t, uint32_t>(0x0fffffff) == 0x0fffffff);
	static_assert(std::bit_cast<int32_t, uint32_t>(0x80000001) == -(0x7fffffff));
#endif

#if   not defined VMAMDL_IGNORE_IEEE754   or   VMAMDL_IGNORE_IEEE754 == false
	static_assert(std::bit_cast<uint16_t, std::float16_t>(std::float16_t(+1.0)) == 0x3c00);
	static_assert(std::bit_cast<uint16_t, std::float16_t>(std::float16_t(-4.5)) == 0xc480);
	static_assert(std::bit_cast<uint16_t, std::float16_t>(std::float16_t(+0.0)) == 0x0000);
	static_assert(std::bit_cast<uint16_t, std::float16_t>(std::float16_t(+0.3)) == 0x34cd);
	static_assert(std::bit_cast<uint32_t, std::float32_t>(std::float32_t(+1.0)) == 0x3f800000);
	static_assert(std::bit_cast<uint32_t, std::float32_t>(std::float32_t(-4.5)) == 0xc0900000);
	static_assert(std::bit_cast<uint32_t, std::float32_t>(std::float32_t(+0.0)) == 0x00000000);
	static_assert(std::bit_cast<uint32_t, std::float32_t>(std::float32_t(+0.3)) == 0x3e99999a);
	static_assert(std::bit_cast<uint64_t, std::float64_t>(std::float64_t(+1.0)) == 0x3ff0000000000000);
	static_assert(std::bit_cast<uint64_t, std::float64_t>(std::float64_t(-4.5)) == 0xc012000000000000);
	static_assert(std::bit_cast<uint64_t, std::float64_t>(std::float64_t(+0.0)) == 0x0000000000000000);
	static_assert(std::bit_cast<uint64_t, std::float64_t>(std::float64_t(+0.3)) == 0x3fd3333333333333);
#endif

#if   not defined VMAMDL_IGNORE_BYTESIZES   or   VMAMDL_IGNORE_BYTESIZES == false
	static_assert(2 == sizeof(std::float16_t));
	static_assert(4 == sizeof(std::float32_t));
	static_assert(8 == sizeof(std::float64_t));
	static_assert(1 == sizeof(int8_t));
	static_assert(1 == sizeof(uint8_t));
	static_assert(2 == sizeof(int16_t));
	static_assert(2 == sizeof(uint16_t));
	static_assert(4 == sizeof(int32_t));
	static_assert(4 == sizeof(uint32_t));
	static_assert(8 == sizeof(int64_t));
	static_assert(8 == sizeof(uint64_t));
#endif



namespace fmamdl {

	using u1_t = uint8_t;
	using u2_t = uint16_t;
	using u4_t = uint32_t;
	using u8_t = uint64_t;

	using s1_t = int8_t;
	using s2_t = int16_t;
	using s4_t = int32_t;
	using s8_t = int64_t;

	using f2_t = std::float16_t;
	using f4_t = std::float32_t;
	using f8_t = std::float64_t;

	template <typename T>
	concept Numeric = std::integral<T> || std::floating_point<T>;


	constexpr u8_t magicNumber(u4_t version) noexcept {
		version = std::byteswap(version & u4_t(0x00ffffff));
		return u8_t(0x616d662323) | (u8_t(version) << u8_t(8*4));
	}

	constexpr u4_t currentFormatVersion = 0x4;
	constexpr u8_t currentMagicNumber   = magicNumber(currentFormatVersion);

}
