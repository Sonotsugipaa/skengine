#pragma once

#include <cstdint>
#include <cstring>
#include <stdfloat>
#include <stdexcept>



namespace fmamdl {

	struct DatumType {
		uint8_t value;

		constexpr operator uint8_t() const noexcept { return uint8_t(value); }
		constexpr DatumType(uint8_t u) noexcept: value(u) { }
		constexpr DatumType() noexcept = default;

		constexpr DatumType(bool real, bool signd, uint8_t widthExp) noexcept:
			DatumType(
				(uint8_t(real)  << 7) |
				(uint8_t(signd) << 6) |
				(widthExp & 0b0011'1111) )
		{ }

		constexpr bool isReal()      const noexcept { return uint8_t(value) >> 7; }
		constexpr bool isSigned()    const noexcept { return (uint8_t(value) >> 6) & 1; }
		constexpr uint8_t widthExp() const noexcept { return uint8_t(value) & 0b0011'1111; }
		constexpr uint8_t width()    const noexcept { return 1 << widthExp(); }
	};

	constexpr DatumType dtUint8   = 0x00;
	constexpr DatumType dtUint16  = 0x01;
	constexpr DatumType dtUint32  = 0x02;
	constexpr DatumType dtUint64  = 0x03;
	constexpr DatumType dtSint8   = 0x40;
	constexpr DatumType dtSint16  = 0x41;
	constexpr DatumType dtSint32  = 0x42;
	constexpr DatumType dtSint64  = 0x43;
	constexpr DatumType dtFloat16 = 0xc1;
	constexpr DatumType dtFloat32 = 0xc2;
	constexpr DatumType dtFloat64 = 0xc3;


	struct NumTypeInfo_ {
		uint8_t isReal   : 1;
		uint8_t isSigned : 1;
		uint8_t widthExp : 6;

		constexpr operator uint8_t() const noexcept {
			if constexpr(sizeof(uint8_t) == sizeof(NumTypeInfo_)) {
				return (isReal << 7) | (isSigned << 6) | widthExp;
			}
		}

		constexpr NumTypeInfo_() = default;

		constexpr NumTypeInfo_(uint8_t u) noexcept {
			if constexpr(sizeof(uint8_t) == sizeof(NumTypeInfo_)) {
				memcpy(this, &u, sizeof(uint8_t));
			} else {
				isReal   = u >> 7;
				isSigned = (u >> 6) & 1;
				widthExp = u & 0b0011'1111;
			}
		}

		constexpr NumTypeInfo_(bool real, bool signd, uint8_t width) noexcept:
			isReal(real),
			isSigned(signd),
			widthExp(width)
		{ }
	};


	class LayoutStringError : public std::runtime_error {
	public:
		LayoutStringError(const char* layoutString, uint32_t errorPosition):
			std::runtime_error("Invalid layout string (\"" + std::string(layoutString) + "\")"),
			lse_str(layoutString),
			lse_errorPos(errorPosition)
		{ }

		const std::string& layoutString()  const { return lse_str; }
		uint32_t           errorPosition() const { return lse_errorPos; }

	private:
		std::string lse_str;
		uint32_t    lse_errorPos;
	};


	class Layout {
	public:
		static Layout                 fromCstring (const char*);
		static const char*            toCstring   (const Layout&) noexcept;
		static const std::string_view toStringView(const Layout&) noexcept;
		static std::size_t            stringLength(const Layout&) noexcept;

		Layout(): l_width(0) { }
		~Layout();

		Layout(Layout&& mv) noexcept: l_storage(mv.l_storage), l_width(mv.l_width) { mv.l_width = 0; }
		Layout& operator=(Layout&& mv) noexcept { this->~Layout(); return * new (this) Layout(std::move(mv)); }

		const DatumType* begin() const noexcept;
		const DatumType* end()   const noexcept;

		const char*      toCstring()    const noexcept { return Layout::toCstring(*this); }
		std::string_view toStringView() const noexcept { return Layout::toStringView(*this); }
		std::size_t      stringLength() const noexcept { return Layout::stringLength(*this); }

		uint32_t length() const noexcept;
		uint32_t size  () const noexcept { return length(); }
		DatumType operator[](uint32_t i) const noexcept;

		bool operator==(const Layout&) const noexcept;
		bool operator==(std::string_view cmp) const noexcept { return cmp == toCstring(); }

	private:
		void*    l_storage;
		uint32_t l_width;
	};

	inline bool operator==(std::string_view l, const Layout& r) noexcept { return r == l; }

}
