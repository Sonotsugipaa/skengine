#pragma once


// This is a very simple single-header library, and I care not what happens
// to it as long as I can easily copy it around and use it.
// I don't even want to bother with setting up multiple files for it,
// so I'm just embedding the Unlicense below just in case it ends up being
// necessary or even useful.


// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org>


// Version 1



#ifndef NUMSTR_NO_NAMESPACE
#ifndef NUMSTR_ANON_NAMESPACE
	#ifndef NUMSTR_NAMESPACE
		// NUMSTR_INLINE_NAMESPACE makes the namespace inline
		#define NUMSTR_NAMESPACE numstr
	#endif
#endif
#endif



// // Features
//
// Basic human-readable numeric serialization and deserialization
// Numeric base up to 61 (with 0 < a < A)
// Fractional numbers with base != 10
// Reasonable rounding of repeated extreme digits (like 1.99999999 or 1.00000001)
//
// // Example expressions (assuming NUMSTR_NAMESPACE = numstr)
//
// std::cout <<  numstr::numToRep<std::string, int>(-841);         // prints "-841"
// std::cout <<  numstr::numToRep<std::string>(33, 16);            // prints "21"
// std::cout <<  numstr::repToNum<int>("56.1"sv);                  // prints "56"
// std::cout <<  numstr::repToNum<int, std::string_view>("-56.1"); // prints "-57"



#include <concepts>
#include <cstdint>
#include <utility>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>



#ifndef NUMSTR_NO_NAMESPACE
	#ifdef NUMSTR_INLINE_NAMESPACE
		inline
	#endif
	namespace
		#ifndef NUMSTR_ANON_NAMESPACE
		NUMSTR_NAMESPACE
		#endif
	{
#endif

	namespace aux {

		template <typename N, std::integral C> requires std::is_arithmetic_v<N>
		struct Segm { C b; C e; N first; N last; };

		template <typename N, std::integral C> requires std::is_arithmetic_v<N>
		struct SegmTriplet { Segm<N, C> s1; Segm<N, C> s2; Segm<N, C> s3; };

		template <typename N, std::integral C> requires std::is_arithmetic_v<N>
		constexpr auto charsetSortedSegmTriplet = []() {
			constexpr auto s0 = Segm<N, C> { '0', '9', N( 0), N( 0 + '9' - '0') };
			constexpr auto sa = Segm<N, C> { 'a', 'z', N(10), N(10 + 'z' - 'a') };
			constexpr auto sA = Segm<N, C> { 'A', 'Z', N(36), N(36 + 'Z' - 'A') };
			if constexpr ('0' < 'a' && 'a' < 'A') return SegmTriplet { s0, sa, sA };
			if constexpr ('0' < 'A' && 'A' < 'a') return SegmTriplet { s0, sA, sa };
			if constexpr ('a' < '0' && '0' < 'A') return SegmTriplet { sa, s0, sA };
			if constexpr ('a' < 'A' && 'A' < '0') return SegmTriplet { sa, sA, s0 };
			if constexpr ('A' < '0' && '0' < 'a') return SegmTriplet { sA, s0, sa };
			if constexpr ('A' < 'a' && 'a' < '0') return SegmTriplet { sA, sa, s0 };
		} ();


		template <std::integral I>
		constexpr I guessRoundingDigitsFromBase(I base) {
			if(base <=  2) return 12;
			if(base <= 10) return 7;
			if(base <= 16) return 5;
			return 3;
		}


		template <typename N>
		requires std::is_arithmetic_v<N>
		constexpr void checkBase(N n) {
			if(n < 2 || n > 61) [[unlikely]] throw std::invalid_argument(fmt::format("invalid base \"{}\"", n));
		}

	}



	using std::convertible_to;
	using std::same_as;
	using std::size_t;


	constexpr bool charsetIsSensible = []() {
		#define C_(CH_) ctr += 1; if(ctr != CH_) return false;
		unsigned char ctr = '0';
		C_('1') C_('2') C_('3') C_('4') C_('5')
		C_('6') C_('7') C_('8') C_('9')
		ctr = 'a';
		C_('b') C_('c') C_('d') C_('e') C_('f')
		C_('g') C_('h') C_('i') C_('j') C_('k')
		C_('l') C_('m') C_('n') C_('o') C_('p')
		C_('q') C_('r') C_('s') C_('t') C_('u')
		C_('v') C_('w') C_('x') C_('y') C_('z')
		ctr = 'A';
		C_('B') C_('C') C_('D') C_('E') C_('F')
		C_('G') C_('H') C_('I') C_('J') C_('K')
		C_('L') C_('M') C_('N') C_('O') C_('P')
		C_('Q') C_('R') C_('S') C_('T') C_('U')
		C_('V') C_('W') C_('X') C_('Y') C_('Z')
		return true;
		#undef C_
	} ();


	template <typename N> requires std::is_arithmetic_v<N>
	constexpr N invalidDigitValue = 62;



	template <typename N, std::integral C> requires std::is_arithmetic_v<N>
	constexpr N charToDigit(C c) {
		if constexpr (charsetIsSensible) {
			auto&& [s1, s2, s3] = aux::charsetSortedSegmTriplet<N, C>;
			if(c >= s1.b && c <= s1.e) return (c + s1.first) - s1.b;
			if(c >= s2.b && c <= s2.e) return (c + s2.first) - s2.b;
			if(c >= s3.b && c <= s3.e) return (c + s3.first) - s3.b;
			return invalidDigitValue<N>;
		} else
		switch(c) {
			#define C_(CH_, N_) case CH_: return N_;
			C_('0',  0) C_('1',  1) C_('2',  2) C_('3',  3) C_('4',  4)
			C_('5',  5) C_('6',  6) C_('7',  7) C_('8',  8) C_('9',  9)
			C_('a', 10) C_('b', 11) C_('c', 12) C_('d', 13) C_('e', 14)
			C_('f', 15) C_('g', 16) C_('h', 17) C_('i', 18) C_('j', 19)
			C_('k', 20) C_('l', 21) C_('m', 22) C_('n', 23) C_('o', 24)
			C_('p', 25) C_('q', 26) C_('r', 27) C_('s', 28) C_('t', 29)
			C_('u', 30) C_('v', 31) C_('w', 32) C_('x', 33) C_('y', 34)
			C_('z', 35)
			C_('A', 36) C_('B', 37) C_('C', 38) C_('D', 39) C_('E', 40)
			C_('F', 41) C_('G', 42) C_('H', 43) C_('I', 44) C_('J', 45)
			C_('K', 46) C_('L', 47) C_('M', 48) C_('N', 49) C_('O', 50)
			C_('P', 51) C_('Q', 52) C_('R', 53) C_('S', 54) C_('T', 55)
			C_('U', 56) C_('V', 57) C_('W', 58) C_('X', 59) C_('Y', 60)
			C_('Z', 61)
			default: return invalidDigitValue<N>;
			#undef C_
		}
	}
	static_assert(charToDigit<int>('0') ==  0);
	static_assert(charToDigit<int>('9') ==  9);
	static_assert(charToDigit<int>('a') == 10);
	static_assert(charToDigit<int>('z') == 35);
	static_assert(charToDigit<int>('A') == 36);
	static_assert(charToDigit<int>('Z') == 61);
	static_assert(charToDigit<int>('?') == invalidDigitValue<int>);


	template <std::integral C, typename N> requires std::is_arithmetic_v<N>
	constexpr C digitToChar(N n) {
		if constexpr (charsetIsSensible) {
			auto&& [s1, s2, s3] = aux::charsetSortedSegmTriplet<N, C>;
			if(n >= s1.first && n <= s1.last) return C(s1.b) + (C(n) - C(s1.first));
			if(n >= s2.first && n <= s2.last) return C(s2.b) + (C(n) - C(s2.first));
			if(n >= s3.first && n <= s3.last) return C(s3.b) + (C(n) - C(s3.first));
			return C(0);
		} else
		switch(n) {
			if constexpr (std::floating_point<N>) n = std::floor(n);
			#define C_(CH_, N_) case N_: return CH_;
			C_('0',  0) C_('1',  1) C_('2',  2) C_('3',  3) C_('4',  4)
			C_('5',  5) C_('6',  6) C_('7',  7) C_('8',  8) C_('9',  9)
			C_('a', 10) C_('b', 11) C_('c', 12) C_('d', 13) C_('e', 14)
			C_('f', 15) C_('g', 16) C_('h', 17) C_('i', 18) C_('j', 19)
			C_('k', 20) C_('l', 21) C_('m', 22) C_('n', 23) C_('o', 24)
			C_('p', 25) C_('q', 26) C_('r', 27) C_('s', 28) C_('t', 29)
			C_('u', 30) C_('v', 31) C_('w', 32) C_('x', 33) C_('y', 34)
			C_('z', 35)
			C_('A', 36) C_('B', 37) C_('C', 38) C_('D', 39) C_('E', 40)
			C_('F', 41) C_('G', 42) C_('H', 43) C_('I', 44) C_('J', 45)
			C_('K', 46) C_('L', 47) C_('M', 48) C_('N', 49) C_('O', 50)
			C_('P', 51) C_('Q', 52) C_('R', 53) C_('S', 54) C_('T', 55)
			C_('U', 56) C_('V', 57) C_('W', 58) C_('X', 59) C_('Y', 60)
			C_('Z', 61)
			default: return C(0);
			#undef C_
		}
	}
	static_assert(digitToChar<char>( 0) == '0');
	static_assert(digitToChar<char>( 9) == '9');
	static_assert(digitToChar<char>(10) == 'a');
	static_assert(digitToChar<char>(35) == 'z');
	static_assert(digitToChar<char>(36) == 'A');
	static_assert(digitToChar<char>(61) == 'Z');


	template <typename T>
	concept StringType = requires (T t) {
		requires std::is_pointer_v<decltype(auto(t.data()))>;
		{ auto(*t.data()) } -> std::integral;
		{ auto(t.size()) } -> std::unsigned_integral;
	};


	template <typename T>
	concept StlStringType = StringType<T> && requires (T t, size_t sz) {
		typename T::value_type;
		t.push_back(typename T::value_type());
		t.clear();
		t.resize(sz);
		{ t.size() } -> same_as<size_t>;
	};


	template <typename T>
	concept StringBuilderType = requires (T t, size_t sz) {
		typename T::CharType;
		typename T::ReturnType;
		requires StringType<typename T::ReturnType>;
		T();
		t.append(typename T::CharType());
		t.reset();
		t.trimToSize(sz);
		{ t.finalize() } -> same_as<typename T::ReturnType>;
	};


	template <typename T>
	concept RangedStringType = StringType<T> && requires (T t) {
		{ t } -> std::ranges::forward_range;
		{ auto(*std::ranges::begin(t)) } -> std::integral;
	};


	template <StlStringType Str>
	struct StlStringBuilder {
		using CharType = Str::value_type;
		using ReturnType = Str;
		Str string;
		void reset() { string.clear(); }
		void trimToSize(size_t sz) { if(sz < string.size()) string.resize(sz); }
		void append(CharType c) { string.push_back(c); }
		Str finalize() { return std::move(string); }
	};


	template <StringBuilderType StrBuilder, std::integral N>
	auto numToRep(N n, N base = N(10)) {
		using C = StrBuilder::CharType;
		aux::checkBase(base);
		auto b = StrBuilder();
		bool neg = (n < 0);
		if(n == 0) {
			b.append(C('0'));
		} else {
			if constexpr (std::signed_integral<N>) if(neg) { n = -n; }
			while(n > 0) {
				b.append(digitToChar<C>(n % base));
				n /= base;
			}
			if constexpr (std::signed_integral<N>) if(neg) b.append(C('-'));
		}
		constexpr auto reverse = [](typename StrBuilder::ReturnType str) {
			size_t sz   = str.size();
			size_t szm1 = sz - size_t(1);
			size_t szd2 = sz / size_t(2);
			auto* strData = str.data();
			for(size_t i=0; i < szd2; ++i) {
				std::swap(strData[i], strData[szm1 - i]);
			}
			return std::move(str);
		};
		return reverse(b.finalize());
	}


	template <StringBuilderType StrBuilder, std::floating_point N>
	auto numToRep(N n, uintmax_t base = 10, uintmax_t roundingDigits = 0 /* 0: automatically chosen from the base */) {
		using C = StrBuilder::CharType;
		const N fbase = base;
		const auto actualRoundingDigits = (roundingDigits == 0)? aux::guessRoundingDigitsFromBase(base) : roundingDigits;
		const N roundingFactor = std::pow(base, actualRoundingDigits);
		aux::checkBase(base);
		auto b = StrBuilder();
		start_over:
		N nAbs = n;
		bool neg = (nAbs < 0);
		if(neg) { nAbs = -nAbs; }
		if(std::isinf(nAbs)) [[unlikely]] {
			b.append(C(neg? '-' : '+'));
			b.append(C('i')); b.append(C('n')); b.append(C('f'));
			return b.finalize();
		}
		if(nAbs > N(INTMAX_MAX)) [[unlikely]] {
			b.append(C(neg? '-' : '+'));
			b.append(C('b')); b.append(C('i')); b.append(C('g'));
			return b.finalize();
		}
		intmax_t intPart = nAbs;
		uintmax_t intPartLen = 0;
		uintmax_t strSize = 0;
		if(intPart == 0) {
			b.append(C('0'));
			++ strSize;
			++ intPartLen;
		} {
			N frcPart = nAbs - std::floor(nAbs);
			while(intPart > 0) {
				b.append(digitToChar<C>(intPart % base));
				++ strSize;
				intPart /= base;
				++ intPartLen;
			}
			if(neg) {
				b.append(C('-'));
				++ strSize;
				++ intPartLen;
			}
			if(frcPart > N(0)) {
				N originalFrcPart = frcPart;
				unsigned curDigit = 0;
				size_t firstTrailingZero = 1;
				b.append(C('.')); ++ strSize;
				do {
					frcPart *= fbase;
					N frcPartRoundedBig = std::round(frcPart * roundingFactor);
					N frcPartRounded = frcPartRoundedBig / roundingFactor;
					bool smallEnoughToTrim = (frcPartRoundedBig <= fbase);
					bool closeEnoughToOne = (uintmax_t(frcPartRoundedBig) >= ((uintmax_t(roundingFactor) * base) - base));
					if(smallEnoughToTrim) {
						break;
					}
					else if(closeEnoughToOne) {
						N basePow = std::pow<N>(fbase, + N(curDigit));
						N lastDigitApprox = std::round(originalFrcPart * basePow) / basePow;
						if(neg) n = -(N(intmax_t(nAbs)) + lastDigitApprox);
						else    n = +(N(intmax_t(nAbs)) + lastDigitApprox);
						b.reset();
						goto start_over;
					}
					uintmax_t ifrcPart = frcPartRounded;
					b.append(digitToChar<C>(ifrcPart));
					++ curDigit;
					++ strSize;
					if(ifrcPart != 0) firstTrailingZero = curDigit;
					frcPart -= N(ifrcPart);
				} while(frcPart > N(0));
				if(curDigit == 0) {
					b.trimToSize(intPartLen); }
				else {
					b.trimToSize(intPartLen + 1 + firstTrailingZero); }
			}
		}
		constexpr auto reverse = [](typename StrBuilder::ReturnType str, size_t from, size_t ntil) {
			size_t sz   = ntil;
			size_t szm1 = sz - size_t(1);
			size_t szd2 = sz / size_t(2);
			auto* strData = str.data();
			for(size_t i = from; i < szd2; ++i) {
				std::swap(strData[i], strData[szm1 - i]);
			}
			return std::move(str);
		};
		return reverse(b.finalize(), 0, intPartLen);
	}


	template <StringType String, std::integral N>
	requires (! StringBuilderType<String>) && StlStringType<String>
	auto numToRep(N n, N base = N(10)) {
		return numToRep<StlStringBuilder<String>, N>(n, base);
	}

	template <StringType String, std::floating_point N>
	requires (! StringBuilderType<String>) && StlStringType<String>
	auto numToRep(N n, uintmax_t base = 10, uintmax_t roundingDigits = 0) {
		return numToRep<StlStringBuilder<String>, N>(n, base, roundingDigits);
	}


	template <typename N>
	requires std::integral<N> || std::floating_point<N>
	class RepToNumResult {
	public:
		constexpr RepToNumResult(): rtn_value(0), rtn_invChar(SIZE_MAX) { }
		constexpr RepToNumResult(N n): rtn_value(n), rtn_invChar(SIZE_MAX) { }
		constexpr RepToNumResult(N n, size_t firstInvalidChar): rtn_value(n), rtn_invChar(firstInvalidChar) { }

		constexpr N value() const noexcept { return rtn_value; }
		constexpr bool stringIsValid() const noexcept { return rtn_invChar == SIZE_MAX; }
		constexpr size_t validStringLength() const noexcept { return rtn_invChar; }
		constexpr operator N() const noexcept { return value(); }

	private:
		N rtn_value;
		size_t rtn_invChar;
	};


	template <typename N, RangedStringType String>
	requires std::integral<N> || std::floating_point<N>
	constexpr RepToNumResult<N> repToNum(String&& str, uintmax_t base = 10) {
		using C = decltype(auto(*str.data()));
		const auto nbase = N(base);
		aux::checkBase(base);
		auto iter = str.begin();
		auto end  = str.end();
		N r = 0;
		bool neg = false;
		if(iter != end) {
			if(*iter == C('+')) {
				++ iter;
			}
			else if(*iter == C('-')) {
				neg = true;
				++ iter;
			}
		} else {
			return RepToNumResult<N>(r);
		}
		C c;
		N digit;
		size_t charIndex = 0;
		size_t lastDelimiterIndex = -1;
		auto fwd = [&]() { ++ iter; ++ charIndex; };
		auto rneg = [&]() {
			if constexpr (std::is_signed_v<N> || std::floating_point<N>) {
				return r * (neg? N(-1) : N(1));
			} else {
				if(neg) return (std::numeric_limits<N>::max() - r) + N(1);
				else return r;
			}
		};
		while(iter < end) {
			c = *iter;
			digit = charToDigit<N>(c);
			if(digit == invalidDigitValue<N>) [[unlikely]] {
				if(c == C('.')) { fwd(); break; }
				else if(c == C('\'')) {
					if(lastDelimiterIndex == charIndex - 1) [[unlikely]] return RepToNumResult(rneg(), charIndex);
					lastDelimiterIndex = charIndex;
					fwd(); continue;
				}
				else return RepToNumResult<N>(rneg(), charIndex);
			}
			if(digit >= nbase) [[unlikely]] return RepToNumResult<N>(rneg(), charIndex);
			r = (r * nbase) + digit;
			fwd();
		}
		N mul = N(1) / nbase;
		bool subOne = false;
		while(iter < end) {
			c = *iter;
			digit = charToDigit<N>(c);
			if(digit == invalidDigitValue<N>) [[unlikely]] {
				if(c == C('\'')) { fwd(); continue; }
				else return RepToNumResult<N>(rneg(), charIndex);
			}
			if(digit >= nbase) [[unlikely]] return RepToNumResult<N>(rneg(), charIndex);
			if(digit > 0) subOne = true;
			if constexpr (std::floating_point<N>) {
				r = r + (digit * mul);
				mul /= nbase;
			}
			fwd();
		}
		if constexpr (! std::floating_point<N>) {
			// "-1.005" is approximated to -2
			if(neg && subOne) r += N(1);
		}
		return RepToNumResult<N>(rneg());
	}

#ifndef NUMSTR_NO_NAMESPACE
}
#endif
