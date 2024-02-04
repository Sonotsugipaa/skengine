#pragma once

#include <ctpg/ctpg.hpp>

#include <string_view>
#include <cassert>
#include <type_traits>
#include <sstream>



#ifdef NDEBUG
	#define NDEBUGV true
#else
	#define NDEBUGV false
#endif



namespace number_parser {

	#define SV_TO_C_(SRC_, DST_) assert(SRC_.size() == 1); char DST_ = SRC_[0];


	struct IntPart {
		std::string rep;
		int32_t value;
	};

	struct FracPart {
		std::string rep;
		float value;
		float leastValue;
	};

	struct NumberTerm {
		std::string rep;
		int32_t intValue;
		float value;
		bool isInteger;
	};

	struct Frac {
		std::string rep;
		int32_t intValue;
		float num;
		float den;
		bool isInteger;
	};

	struct Number {
		union {
			int32_t intValue;
			float   realValue;
		};
		bool isInteger;

		template <typename T>
		constexpr T get() const noexcept { return isInteger? T(intValue) : T(realValue); }
	};

	struct ParseResult {
		std::string errorDescription;
		bool success;

		operator bool() const noexcept { return success; }
	};


	void pushFracDigit(FracPart& fp, char d) {
		fp.rep.push_back(d);
		fp.value += (float(d - '0') * float(fp.leastValue));
		fp.leastValue /= float(10);
	}

	void pushIntDigit(IntPart& n, char d) {
		n.rep.push_back(d);
		n.value = (n.value * 10) + int32_t(d - '0');
	}



	namespace pat {
		#define PAT_(NM_, V_) constexpr const char NM_[] = V_;

		PAT_(number_d,  R"([0-9])")
		PAT_(number_s,  R"([\+\-])")
		#define NUMBER_T_REGEX_ "[0-9]('?[0-9])*" "(\\.[0-9]('?[0-9])*)?"
			PAT_(number_t, NUMBER_T_REGEX_)
			PAT_(number,   "[\\+\\-]?" NUMBER_T_REGEX_ "(" "/" NUMBER_T_REGEX_ ")?")
		#undef NUMBER_T_REGEX_
		PAT_(number_ip, R"([0-9]['0-9]*)")
		PAT_(number_fp, R"([0-9]['0-9]*)")

		#undef PAT_
	}



	constexpr auto number_d  = ctpg::regex_term<pat::number_d>("digit");
	constexpr auto number_s  = ctpg::regex_term<pat::number_s>("sign");

	constexpr auto number_ip = ctpg::nterm<IntPart>("number integer part");
	constexpr auto number_fp = ctpg::nterm<FracPart>("number fractional part");
	constexpr auto number    = ctpg::nterm<Number>("number");
	constexpr auto number_f  = ctpg::nterm<Frac>("fraction");
	constexpr auto number_t  = ctpg::nterm<NumberTerm>("term" /* Math "term", not terminal "term" */);


	constexpr auto numberParser = ctpg::parser(
		number,
		ctpg::terms(number_s, number_d, '.', '/', '\''),
		ctpg::nterms(number_t, number_ip, number_fp, number_f, number),
		ctpg::rules(
			number_ip(number_d)
			>= [](std::string_view digit) {
				static_assert('5' - '0' == 5);
				SV_TO_C_(digit, d)
				assert(d != '\'');
				return IntPart { .rep = std::string(digit), .value = int32_t(d - '0') };
			},
			number_ip(number_ip, number_d)
			>= [](IntPart&& n, std::string_view digit) {
				SV_TO_C_(digit, d)
				pushIntDigit(n, d);
				return std::move(n);
			},
			number_ip(number_ip, '\'', number_d) >= [](IntPart&& n, char q, std::string_view digit) {
				SV_TO_C_(digit, d)
				n.rep.push_back(q);
				pushIntDigit(n, d);
				return std::move(n);
			},
			number_fp(number_d)
			>= [](std::string_view digit) {
				SV_TO_C_(digit, d)
				return FracPart { std::string(digit), float(d - '0') / float(10), float(0.01) };
			},
			number_fp(number_fp, number_d)
			>= [](FracPart&& fp, std::string_view digit) {
				SV_TO_C_(digit, d)
				pushFracDigit(fp, d);
				return std::move(fp);
			},
			number_fp(number_fp, '\'', number_d) >= [](FracPart&& fp, char q, std::string_view digit) {
				SV_TO_C_(digit, d)
				fp.rep.push_back(q);
				pushFracDigit(fp, d);
				return std::move(fp);
			},
			number_t(number_ip) >= [](IntPart&& n) { return NumberTerm { .rep = std::move(n.rep), .intValue = n.value, .value = float(n.value), .isInteger = true }; },
			number_t(number_ip, '.', number_fp)
			>= [](IntPart&& n, char dot, FracPart&& fp) {
				return NumberTerm {
					.rep = std::move(n.rep) + dot + std::move(fp.rep),
					.intValue = 0,
					.value = float(n.value) + fp.value,
					.isInteger = false };
			},
			number_t('.', number_fp)
			>= [](char dot, FracPart&& fp) {
				return NumberTerm {
					.rep = dot + std::move(fp.rep),
					.intValue = 0,
					.value = fp.value,
					.isInteger = false };
			},
			number_f(number_t) >= [](NumberTerm&& n) { return Frac { .rep = std::move(n.rep), .intValue = n.intValue, .num = n.value, .den = float(1), .isInteger = n.isInteger }; },
			number_f(number_t, '/', number_t)
			>= [](NumberTerm&& num, char dot, NumberTerm&& den) {
				return Frac {
					.rep = std::move(num).rep + dot + std::move(den).rep,
					.intValue = 0,
					.num = num.value,
					.den = den.value,
					.isInteger = false };
			},
			number(number_f) >= [](Frac&& f) { return Number { .realValue = f.num / f.den, .isInteger = false }; },
			number(number_s, number_f)
			>= [](std::string_view sign, Frac&& f) {
				SV_TO_C_(sign, s)
				if(f.isInteger) {
					return Number { .intValue = f.intValue * (s == '-'? -1:+1), .isInteger = true };
				} else {
					auto r = f.num / f.den;
					if(s == '-') r = -r;
					f.rep.insert(f.rep.begin(), s);
					return Number { .realValue = r, .isInteger = false };
				}
			} ));


	std::pair<Number, ParseResult> parseNumber(std::string_view rep) {
		using R = std::pair<Number, ParseResult>;
		static constexpr auto opt = ctpg::parse_options { .verbose = NDEBUGV, .skip_whitespace = false, .skip_newline = false };
		auto buf = ctpg::buffers::string_buffer(std::string(rep));
		auto err = std::stringstream();
		auto r   = numberParser.parse(opt, buf, err);
		if(r) {
			return R(r.value(), { .errorDescription = "", .success = true });
		} else {
			return R({ }, { .errorDescription = std::move(err).str(), .success = false });
		}
	}

	#undef SV_TO_C_

}



#undef NDEBUGV
