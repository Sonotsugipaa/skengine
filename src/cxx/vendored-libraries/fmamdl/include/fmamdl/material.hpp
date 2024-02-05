#pragma once

#include <bit>
#include <cstdint>
#include <stdfloat>
#include <stdexcept>
#include <span>

#include "fmamdl/binary.hpp"



// # FMA 4 material format specification
//
// An inline pixel with RGBA={0x51, 0x52, 0x53, 0x54} has the value `0x0000000051525354`
//
// Material:
// U8    | Magic Number + ("##fma" + big-endian version number)
// MTFL  | Flags
//	U8    | Diffuse texture name or inline pixel (U1111, RGBA, least significant bytes)
//	U8    | Normal texture name or inline pixel (U1111, RGBA, least significant bytes)
//	U8    | Specular texture name or inline pixel (U1111, RGBA, least significant bytes)
//	U8    | Emissive texture name or inline pixel (U1111, RGBA, least significant bytes)
// F4    | Specular exponent
// Pad4  |
// U8    | Pointer to string storage
// U8    | String count
// U8    | String storage size
//
// MTFL:
// Bit8  | Transparent
// Bit8  | Diffuse Inline Pixel
// Bit8  | Normal Inline Pixel
// Bit8  | Specular Inline Pixel
// Bit8  | Emissive Inline Pixel
//
// String storage:
// Nstr  | First String
// ...   | Remaining Strings



namespace fmamdl {

	using material_flags_e = u8_t;
	enum class MaterialFlags : material_flags_e {
		eTransparent         = 1 << 0,
		eDiffuseInlinePixel  = 1 << 1,
		eNormalInlinePixel   = 1 << 2,
		eSpecularInlinePixel = 1 << 3,
		eEmissiveInlinePixel = 1 << 4
	};


	class MaterialView {
	public:
		/// \brief Non-owning pointer to the material.
		///
		std::byte*  data;

		/// \brief The size of the material, in bytes.
		///
		std::size_t length;

		#define GETTER_REF_(T_, N_)   const T_& N_() const;   T_& N_() { return const_cast<T_&>(const_cast<const MaterialView&>(*this).N_()); }
		#define GETTER_PTR_(T_, N_)   const T_* N_() const;   T_* N_() { return const_cast<T_*>(const_cast<const MaterialView&>(*this).N_()); }
		#define GETTER_SPN_(T_, N_, B_)   const std::span<const T_> N_() const { return std::span<const T_>(B_##Ptr(), size_t(B_##Count())); }   std::span<T_> N_() noexcept { return std::span<T_>(B_##Ptr(), size_t(B_##Count())); }

			GETTER_REF_(u8_t,          magicNumber)
			GETTER_REF_(MaterialFlags, flags)

			GETTER_REF_(u8_t, diffuseTexture)
			GETTER_REF_(u8_t, normalTexture)
			GETTER_REF_(u8_t, specularTexture)
			GETTER_REF_(u8_t, emissiveTexture)
			GETTER_REF_(f4_t, specularExponent)

			GETTER_REF_(u8_t,      stringStorageOffset)
			GETTER_REF_(u8_t,      stringCount)
			GETTER_REF_(u8_t,      stringStorageSize)
			GETTER_PTR_(std::byte, stringPtr)

		#undef GETTER_SPN_
		#undef GETTER_PTR_
		#undef GETTER_REF_

		const char* getCstring(u8_t offset) const;
		const std::string_view getStringView(u8_t offset) const;
	};

}
