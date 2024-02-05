#include <fmamdl/layout.hpp>

#include <utility>
#include <cassert>
#include <cstring>



namespace fmamdl {

	namespace {

		int32_t layoutStrFindError(const char* str) {
			int32_t r = 0;
			char    lastType = '\0';

			#define CMP_(C_) if(*str == C_)

			CMP_('s') goto st_type_s;
			CMP_('u') goto st_type_u;
			CMP_('f') goto st_type_f;
			CMP_('\0') goto st_end;
			goto st_error;

			st_type_s:
			if(lastType == 's') goto st_error;
			lastType = 's';
			++ str;
			goto st_read_data_i;

			st_type_u:
			if(lastType == 'u') goto st_error;
			lastType = 'u';
			++ str;
			goto st_read_data_i;

			st_type_f:
			if(lastType == 'f') goto st_error;
			lastType = 'f';
			++ str;
			goto st_read_data_f;

			st_read_data_f:
			CMP_('2') goto st_datum_f_fwd;
			CMP_('4') goto st_datum_f_fwd;
			CMP_('8') goto st_datum_f_fwd;
			CMP_('\0') goto st_end;
			goto st_error;

			st_read_data_i:
			CMP_('1') goto st_datum_i_fwd;
			CMP_('2') goto st_datum_i_fwd;
			CMP_('4') goto st_datum_i_fwd;
			CMP_('8') goto st_datum_i_fwd;
			CMP_('\0') goto st_end;
			goto st_error;

			st_read_any_i:
			CMP_('s') goto st_type_s;
			CMP_('u') goto st_type_u;
			CMP_('f') goto st_type_f;
			CMP_('\0') goto st_end;
			goto st_read_data_i;

			st_read_any_f:
			CMP_('s') goto st_type_s;
			CMP_('u') goto st_type_u;
			CMP_('f') goto st_type_f;
			CMP_('\0') goto st_end;
			goto st_read_data_f;

			st_datum_i_fwd:
			++ r;
			++ str;
			goto st_read_any_i;

			st_datum_f_fwd:
			++ r;
			++ str;
			goto st_read_any_f;

			st_error: return -r;
			st_end:   return +r;

			#undef CMP_
			std::unreachable();
		}


		void layoutStrPopulate(const char* str, DatumType* infos) {
			bool    signd;
			uint8_t wExp;

			#define CMP_(C_) if(*str == C_)

			CMP_('s') goto st_type_s;
			CMP_('u') goto st_type_u;
			CMP_('f') goto st_type_f;
			CMP_('\0') goto st_end;
			goto st_error;

			st_type_s:
			signd = true;
			++ str;
			goto st_read_data_i;

			st_type_u:
			signd = false;
			++ str;
			goto st_read_data_i;

			st_type_f:
			++ str;
			goto st_read_data_f;

			st_read_data_f:
			CMP_('2') goto st_datum_f_2;
			CMP_('4') goto st_datum_f_4;
			CMP_('8') goto st_datum_f_8;
			CMP_('\0') goto st_end;
			goto st_error;

			st_read_data_i:
			CMP_('1') goto st_datum_i_1;
			CMP_('2') goto st_datum_i_2;
			CMP_('4') goto st_datum_i_4;
			CMP_('8') goto st_datum_i_8;
			CMP_('\0') goto st_end;
			goto st_error;

			st_read_any_i:
			CMP_('s') goto st_type_s;
			CMP_('u') goto st_type_u;
			CMP_('f') goto st_type_f;
			CMP_('\0') goto st_end;
			goto st_read_data_i;

			st_read_any_f:
			CMP_('s') goto st_type_s;
			CMP_('u') goto st_type_u;
			CMP_('f') goto st_type_f;
			CMP_('\0') goto st_end;
			goto st_read_data_f;

			st_datum_i_1:
			wExp = 0;
			goto st_datum_i_fwd;

			st_datum_i_2:
			wExp = 1;
			goto st_datum_i_fwd;

			st_datum_i_4:
			wExp = 2;
			goto st_datum_i_fwd;

			st_datum_i_8:
			wExp = 3;
			goto st_datum_i_fwd;

			st_datum_i_fwd:
			*infos = DatumType(false, signd, wExp);
			++ str;
			++ infos;
			goto st_read_any_i;

			st_datum_f_2:
			wExp = 1;
			goto st_datum_f_fwd;

			st_datum_f_4:
			wExp = 2;
			goto st_datum_f_fwd;

			st_datum_f_8:
			wExp = 3;
			goto st_datum_f_fwd;

			st_datum_f_fwd:
			*infos = DatumType(true, true, wExp);
			++ str;
			++ infos;
			goto st_read_any_f;

			st_error: std::unreachable(/* Errors MUST be detected prior to calling this function */); abort();
			st_end:   return;

			#undef CMP_
			std::unreachable();
		}


		uint32_t getLayoutDataWidth(uint32_t width) {
			return width & 0x0000'0fff;
		}


		uint32_t getLayoutStringWidth(uint32_t width) {
			return (width >> 12) & 0x000f'ffff;
		}


		uint32_t layoutWidth(uint32_t data, uint32_t string) {
			assert(data   < (uint32_t(1) << 12));
			assert(string < (uint32_t(1) << 20));
			return data | (string << 12);
		}

	}



	Layout Layout::fromCstring(const char* str) {
		Layout l;
		uint32_t strLen    = strlen(str);
		int32_t  layoutLen = layoutStrFindError(str);
		if(layoutLen <= 0) {
			// Error or null string
			if(*str == '\0') {
				l.l_width = 0;
				return l;
			} else {
				throw LayoutStringError(str, -layoutLen);
			}
		} else {
			// 100% valid string (allegedly)
			l.l_width = layoutWidth(layoutLen, strLen);
			uint32_t layoutOffset = uint32_t(layoutLen) * sizeof(DatumType);
			l.l_storage = new std::byte[strLen + layoutOffset];
			layoutStrPopulate(str, reinterpret_cast<DatumType*>(l.l_storage));
			memcpy(
				layoutOffset + reinterpret_cast<std::byte*>(l.l_storage),
				str,
				strLen );
			return l;
		}
		std::unreachable();
	}


	const char* Layout::toCstring(const Layout& layout) noexcept {
		if(layout.l_width == 0) {
			return "";
		} else {
			return reinterpret_cast<const char*>(
				getLayoutDataWidth(layout.l_width)
				+ reinterpret_cast<const std::byte*>(layout.l_storage) );
		}
	}


	const std::string_view Layout::toStringView(const Layout& layout) noexcept {
		if(layout.l_width == 0) {
			return { };
		} else {
			return std::string_view(
				getLayoutDataWidth(layout.l_width)
				+ reinterpret_cast<const char*>(layout.l_storage),
				getLayoutStringWidth(layout.l_width) );
		}
	}


	std::size_t Layout::stringLength(const Layout& layout) noexcept {
		return getLayoutStringWidth(layout.l_width);
	}


	Layout::~Layout() {
		if(l_width > 0) {
			delete[] reinterpret_cast<std::byte*>(l_storage);
			l_width = 0;
		}
	}


	const DatumType* Layout::begin() const noexcept {
		return reinterpret_cast<DatumType*>(l_storage);
	}


	const DatumType* Layout::end() const noexcept {
		return begin() + length();
	}


	uint32_t Layout::length() const noexcept {
		return getLayoutDataWidth(l_width);
	}


	DatumType Layout::operator[](uint32_t i) const noexcept {
		uint32_t len = length();
		if(i < len) {
			return begin()[i];
		} else {
			if(len < 1) return DatumType(false, false, 0);
			return begin()[len-1];
		}
	}


	bool Layout::operator==(const Layout& cmp) const noexcept {
		#define MEMCMP_(LEN_) (0 == memcmp(l_storage, cmp.l_storage, LEN_))
		auto len0 = length();
		auto len1 = cmp.length();
		if(len0 == len1) {
			return MEMCMP_(len0);
		} else {
			const Layout* cmp0 = this;
			const Layout* cmp1 = &cmp;
			if(len0 > len1) {
				std::swap(len0, len1);
				std::swap(cmp0, cmp1);
			}
			if(! MEMCMP_(len0)) return false;
			auto lastCmpValue = (len0 > 0)? begin()[len0-1] : (*this)[0];
			for(decltype(len0) i = len0; i < len1; ++i) {
				if(cmp1->begin()[i] != lastCmpValue) return false;
			}
			return true;
		}
		#undef MEMCMP_
	}

}
