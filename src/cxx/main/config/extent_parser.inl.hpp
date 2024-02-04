#pragma once

#include "number_parser.inl.hpp"



#ifdef NDEBUG
	#define NDEBUGV true
#else
	#define NDEBUGV false
#endif

#define CAR const auto&
#define SV_TO_C(SV_) ([](const std::string_view& sv) { assert(! sv.empty()); return sv[0]; } (SV_))



inline namespace main_ns {
namespace extent_parser {

	using ctpg::no_type;


	struct Extent {
		unsigned width;
		unsigned height;
	};


	struct ParseResult {
		std::string errorDescription;
		bool success;

		operator bool() const noexcept { return success; }
	};



	namespace pat {
		#define PAT_(NM_, V_) constexpr const char NM_[] = V_;
		PAT_(digit_n0,   R"([1-9])")
		PAT_(whitespace, R"([ \t])")
		#undef PAT_
	}



	namespace t {
		constexpr auto digit1 = ctpg::regex_term<pat::digit_n0>  ("non-zero digit");
		constexpr auto wspace = ctpg::regex_term<pat::whitespace>("whitespace");
	}



	namespace nt {
		constexpr auto wspaces = ctpg::nterm<no_type>("whitespaces");
		constexpr auto number  = ctpg::nterm<std::string>("non-zero integer number");
		constexpr auto number0 = ctpg::nterm<std::string>("integer number");
		constexpr auto extent  = ctpg::nterm<Extent>("resolution");
		constexpr auto digit   = ctpg::nterm<char>("digit");
	}



	constexpr auto extentParser = ctpg::parser(
		nt::extent,
		ctpg::terms(t::digit1, t::wspace, 'x', '0'),
		ctpg::nterms(nt::extent, nt::number, nt::number0, nt::wspaces, nt::digit),
		ctpg::rules(
			nt::extent(nt::number, nt::wspaces, 'x', nt::wspaces, nt::number) >= [](std::string_view n0rep, CAR, CAR, CAR, std::string_view n1rep) {
				auto n0 = number_parser::parseNumber(n0rep).first;
				auto n1 = number_parser::parseNumber(n1rep).first;
				return Extent { n0.get<unsigned>(), n1.get<unsigned>() };
			},
			nt::number(),
			nt::number(t::digit1, nt::number0) >= [](CAR d, std::string&& n) { n.insert(n.begin(), SV_TO_C(d)); return n; },
			nt::wspaces(),
			nt::wspaces(nt::wspaces, t::wspace) >= [](CAR, CAR) { return no_type(); },
			nt::number0() >= []() { std::string r; r.reserve(5 /* a 99999 pixel-wide image */); return r; },
			nt::number0(nt::number0, nt::digit) >= [](std::string&& n, char d) { n.push_back(d); return n; },
			nt::digit(t::digit1) >= [](CAR d) { return SV_TO_C(d); },
			nt::digit('0') ) );


	std::pair<Extent, ParseResult> parseExtent(std::string_view rep) {
		using R = std::pair<Extent, ParseResult>;
		static constexpr auto opt = ctpg::parse_options { .verbose = NDEBUGV, .skip_whitespace = true, .skip_newline = false };
		auto buf = ctpg::buffers::string_buffer(std::string(rep));
		auto err = std::stringstream();
		auto r   = extentParser.parse(opt, buf, err);
		if(r) {
			return R(r.value(), { .errorDescription = "", .success = true });
		} else {
			return R({ }, { .errorDescription = std::move(err).str(), .success = false });
		}
	}

}}



#undef NDEBUGV
#undef CAR
#undef SV_TO_C
