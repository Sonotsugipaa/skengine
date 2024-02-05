#include <fmamdl/material.hpp>

#include "string.inl.hpp"

#include <cassert>



namespace fmamdl {

	namespace mat {
		constexpr std::size_t OFF_MAGIC_NO    = 8 * 0;
		constexpr std::size_t OFF_FLAGS       = 8 * 1;
		constexpr std::size_t OFF_DIFFUSE     = 8 * 2;
		constexpr std::size_t OFF_NORMAL      = 8 * 3;
		constexpr std::size_t OFF_SPECULAR    = 8 * 4;
		constexpr std::size_t OFF_EMISSIVE    = 8 * 5;
		constexpr std::size_t OFF_SPC_EXP     = 8 * 6  +  4 * 0;
		constexpr std::size_t OFF_PAD0        = 8 * 6  +  4 * 1;
		constexpr std::size_t OFF_STR_STORAGE = 8 * 7;
		constexpr std::size_t OFF_STR_COUNT   = 8 * 8;
		constexpr std::size_t OFF_STR_SIZE    = 8 * 9;
	}

	const u8_t&          MaterialView::magicNumber() const { return accessPrimitive<u8_t>(data, length, mat::OFF_MAGIC_NO); }
	const MaterialFlags& MaterialView::flags()       const { return accessPrimitive<MaterialFlags>(data, length, mat::OFF_FLAGS); }

	const u8_t& MaterialView::diffuseTexture()   const { return accessPrimitive<u8_t>(data, length, mat::OFF_DIFFUSE); }
	const u8_t& MaterialView::normalTexture()    const { return accessPrimitive<u8_t>(data, length, mat::OFF_NORMAL); }
	const u8_t& MaterialView::specularTexture()  const { return accessPrimitive<u8_t>(data, length, mat::OFF_SPECULAR); }
	const u8_t& MaterialView::emissiveTexture()  const { return accessPrimitive<u8_t>(data, length, mat::OFF_EMISSIVE); }
	const f4_t& MaterialView::specularExponent() const { return accessPrimitive<f4_t>(data, length, mat::OFF_SPC_EXP); }

	const u8_t&      MaterialView::stringStorageOffset() const { return accessPrimitive<u8_t>(data, length, mat::OFF_STR_STORAGE); }
	const u8_t&      MaterialView::stringStorageSize()   const { return accessPrimitive<u8_t>(data, length, mat::OFF_STR_SIZE); }
	const u8_t&      MaterialView::stringCount()         const { return accessPrimitive<u8_t>(data, length, mat::OFF_STR_COUNT); }
	const std::byte* MaterialView::stringPtr()           const { return data + stringStorageOffset(); }


	const char* MaterialView::getCstring(u8_t offset) const {
		return getStringView(offset).begin();
	}

	const std::string_view MaterialView::getStringView(u8_t offset) const {
		return accessNstr(stringPtr(), SIZE_MAX, offset);
	}

}
