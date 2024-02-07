#include "config.hpp"

#include "extent_parser.inl.hpp"

#include <locale>
#include <array>



inline namespace main_ns {

	struct Span {
		const void* begin;
		const void* end;
	};


	template <typename char_tp>
	bool readBuffer(Span& buffer, char_tp* dst) {
		if(buffer.begin < buffer.end) [[likely]] {
			*dst = * reinterpret_cast<const char_tp*>(buffer.begin);
			buffer.begin = 1 + reinterpret_cast<const char_tp*>(buffer.begin);
			return true;
		}
		return false;
	};


	template <typename char_tp, typename StringView>
	constexpr bool charMatches(char_tp match, std::span<char_tp> validValues) {
		for(auto& c : validValues) {
			if(match == c) return true;
		}
		return false;
	}

	template <typename char_tp, size_t validValueCount, std::array<char_tp, validValueCount> validValues>
	constexpr bool charMatches(char_tp match) {
		for(auto& c : validValues) {
			if(match == c) return true;
		}
		return false;
	}


	template <
		typename StringView,
		size_t whitespaceCharCount, std::array<typename StringView::value_type, whitespaceCharCount> whitespaceChars >
	bool skipSequence(Span& buffer) {
		using char_t = StringView::value_type;

		char_t c;
		while(readBuffer(buffer, &c)) {
			if(! charMatches<char_t, whitespaceCharCount, whitespaceChars>(c)) [[unlikely]] {
				buffer.begin = reinterpret_cast<const char_t*>(buffer.begin) - 1;
				return true;
			}
		}
		return false;
	}


	template <typename char_tp>
	bool seekChar(Span& buffer, char_tp terminator) {
		char_tp c;
		while(readBuffer(buffer, &c)) {
			if(c == terminator) return true;
		}
		return false;
	}


	template <
		typename StringView,
		size_t newlineCharCount, std::array<typename StringView::value_type, newlineCharCount> newlineChars >
	Span readLine(Span& buffer) {
		using char_t = StringView::value_type;

		Span r = { buffer.begin, buffer.begin };
		char_t c;
		bool   n_end;
		while(
			(n_end = readBuffer(buffer, &c))
			&& (! charMatches<char_t, newlineCharCount, newlineChars>(c))
		) {
			r.end = 1 + reinterpret_cast<const char_t*>(r.end);
		}
		if(! n_end) r.end = 1 + reinterpret_cast<const char_t*>(r.end);

		return r;
	}


	template <
		typename StringView,
		size_t whitespaceCharCount, std::array<typename StringView::value_type, whitespaceCharCount> whitespaceChars >
	StringView readKey(Span& buffer) {
		using char_t = StringView::value_type;
		static constexpr auto terminators = []() {
			auto r = std::array<char_t, 1 + whitespaceCharCount> { ':' };
			for(size_t i = 1; i < whitespaceCharCount; ++i) r[i] = whitespaceChars[i-1];
			return r;
		} ();

		auto beg = reinterpret_cast<const char_t*>(buffer.begin);
		auto end = beg;
		char_t c;
		bool readSuccess;
		while((readSuccess = readBuffer(buffer, &c)) && ! charMatches<char_t, terminators.size(), terminators>(c)) {
			++ end;
		}
		if(readSuccess) buffer.begin = reinterpret_cast<const char_t*>(buffer.begin) - 1;

		return StringView(beg, end);
	}


	void parseSettings(Settings* dst, const posixfio::MemMapping& data, spdlog::logger& logger) {
		using namespace std::string_view_literals;
		using StringView = std::u8string_view;
		static constexpr auto whitespaceChars = std::array<char8_t, 2>{ ' ', '\t' };
		static constexpr auto newlineChars    = std::array<char8_t, 1>{ '\n' };
		constexpr auto readLine = [](Span& span) { return main_ns::readLine<StringView, newlineChars.size(), newlineChars>(span); };
		constexpr auto skipWhitespaces = [](Span& span) { return main_ns::skipSequence<StringView, whitespaceChars.size(), whitespaceChars>(span); };
		constexpr auto readKey = [](Span& span) { return main_ns::readKey<StringView, whitespaceChars.size(), whitespaceChars>(span); };

		Span dataSpan = { data.get<void>(), data.get<std::byte>() + data.size() };

		#pragma region // lambdas for parsing values
		auto parseExtent = [&](Extent* dst, const std::string_view& ln, bool autoIsZero) {
			if(ln == "auto") {
				if(autoIsZero) *dst = { 0, 0 };
			} else {
				auto ext = extent_parser::parseExtent(ln);
				if(ext.second) {
					*dst = { ext.first.width, ext.first.height };
				} else {
					logger.error("Invalid extent: {}", ext.second.errorDescription);
				}
			}
		};
		auto parsePresentMode = [&](PresentMode* dst, const std::string_view& ln) {
			if     (ln == "immediate") *dst = PresentMode::eImmediate;
			else if(ln == "fifo")      *dst = PresentMode::eFifo;
			else if(ln == "vsync")     *dst = PresentMode::eFifo;
			else if(ln == "mailbox")   *dst = PresentMode::eMailbox;
			else logger.error("Invalid present mode: \"{}\"", ln);
		};
		auto parseCelCount = [&](uint32_t* dst, const std::string_view& ln) {
			auto n = number_parser::parseNumber(ln);
			if(ln == "none")  *dst = 0;
			else if(n.second) *dst = n.first.get<uint32_t>();
			else              logger.error("Invalid cel-shading value: \"{}\"", ln);
		};
		auto parseRealNumber = [&](float* dst, const std::string_view& ln) {
			auto n = number_parser::parseNumber(ln);
			if(n.second) {
				*dst = n.first.get<float>();
			} else {
				logger.error("Invalid number: \"{}\"", ln);
			}
		};
		auto parseIntFramerate = [&](uint32_t* dst, const std::string_view& ln) {
			auto n = number_parser::parseNumber(ln);
			if(n.second) {
				auto v = n.first.get<uint32_t>();
				if(v < 1) logger.error("Invalid framerate value: must not be lower than 1", ln);
				*dst = v;
			} else {
				logger.error("Invalid framerate value: \"{}\"", ln);
			}
		};
		auto parseRealFramerate = [&](float* dst, const std::string_view& ln) {
			auto n = number_parser::parseNumber(ln);
			if(n.second) {
				auto v = n.first.get<float>();
				if(v < 1) logger.error("Invalid framerate value: must not be lower than 1", ln);
				*dst = v;
			} else {
				logger.error("Invalid framerate value: \"{}\"", ln);
			}
		};
		auto parseFov = [&](float* dst, const std::string_view& ln) {
			auto n = number_parser::parseNumber(ln);
			if(n.second) {
				auto v = n.first.get<float>();
				if(v < 0.01)   [[unlikely]] logger.error("Invalid field of view: must be higher than 0", ln);
				else
				if(v > 179.99) [[unlikely]] logger.error("Invalid field of view: must be lower than 180", ln);
				*dst = v;
			} else {
				logger.error("Invalid field of view: \"{}\"", ln);
			}
		};
		#pragma endregion

		auto trimTrailingWspaces = [&](Span& ln) {
			auto end = reinterpret_cast<const char8_t*>(ln.end);
			char8_t c;
			bool charAvailable;
			auto shiftBack = [&]() {
				if(end <= ln.begin) [[unlikely]] return false;
				-- end;
				c = *end;
				return true;
			};
			while((charAvailable = shiftBack()) && charMatches<char8_t, whitespaceChars.size(), whitespaceChars>(c)) { /* NOP */ }
			if(charAvailable) [[likely]] ++ end;
			ln.end = end;
		};

		auto readConfigLine = [&]() {
			Span ln = readLine(dataSpan);
			if(skipWhitespaces(ln)) {
				auto key = readKey(ln);
				skipWhitespaces(ln);
				seekChar<char8_t>(ln, ':');
				skipWhitespaces(ln);
				trimTrailingWspaces(ln);
				std::string_view lnv = std::string_view(reinterpret_cast<const char*>(ln.begin), reinterpret_cast<const char*>(ln.end));
				if(key.empty() || (key.front() == '#')) { /* NOP */ }
				else if(key == u8"initial-present-resolution") parseExtent(&dst->initialPresentExtent, lnv, false);
				else if(key == u8"max-render-resolution")      parseExtent(&dst->maxRenderExtent, lnv, true);
				else if(key == u8"present-mode")               parsePresentMode(&dst->presentMode, lnv);
				else if(key == u8"cel-shading")                parseCelCount(&dst->shadeStepCount, lnv);
				else if(key == u8"cel-smoothness")             parseRealNumber(&dst->shadeStepSmooth, lnv);
				else if(key == u8"cel-gamma")                  parseRealNumber(&dst->shadeStepGamma, lnv);
				else if(key == u8"framerate-samples")          parseIntFramerate(&dst->framerateSamples, lnv);
				else if(key == u8"target-framerate")           parseRealFramerate(&dst->targetFramerate, lnv);
				else if(key == u8"target-tickrate")            parseRealFramerate(&dst->targetTickrate, lnv);
				else if(key == u8"field-of-view")              parseFov(&dst->fieldOfView, lnv);
				else logger.error("Unrecognized settings key: \"{}\"", std::string_view(reinterpret_cast<const char*>(key.begin()), reinterpret_cast<const char*>(key.end())));
			} else {
				// just skipped optional spaces on the last empty line
			}
		};

		while(dataSpan.begin < dataSpan.end) {
			readConfigLine();
		}
	}

}
