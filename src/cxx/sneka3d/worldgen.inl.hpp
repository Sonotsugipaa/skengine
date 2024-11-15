#pragma once

#include <random>
#include <bit>

#include <timer.tpp>

#include "world.inl.hpp"



namespace sneka {

	template <std::integral T, typename Rng, T minBits = std::numeric_limits<T>::digits>
	auto random(Rng rng) {
		constexpr T resBits = std::bit_width(Rng::max() - Rng::min());
		T bitsLeft = minBits - resBits;
		T r = rng() - Rng::min();
		while(bitsLeft > 0) {
			r = (r << resBits) | (rng() - Rng::min());
			bitsLeft -= resBits;
		}
		return r;
	}


	template <typename Logger, typename Rng>
	void generateWorldNoise(Logger&& logger, World& dst, Rng&& rng) {
		util::SteadyTimer<> timer;
		auto genFloat = [&](auto min, auto max) { return std::uniform_real_distribution<float>(min, max)(rng); };
		auto genUint  = [&](auto min, auto max) { return std::uniform_int_distribution<unsigned>(min, max)(rng); };
		auto w = dst.width();
		auto h = dst.height();
		assert(w * h > 0);
		float objCountRel = genFloat(0.6f, 0.9f);
		unsigned objCount = float(w * h) * objCountRel;
		const unsigned attemptLimitInit = objCount * (1.0f / (1.0f - objCountRel));
		unsigned attemptLimit = attemptLimitInit;
		for(unsigned i = 1; i <= objCount; ++ i) {
			assert(i > 0 /* `--i` needs to be possible */);
			auto xGen = genUint(0, w-1);
			auto yGen = genUint(0, h-1);
			auto& tile = dst.tile(xGen, yGen);
			if(tile != GridObjectClass::eNoObject) {
				-- attemptLimit;
				-- i;
				if(attemptLimit <= 0) {
					logger.error(
						"Generating world noise: giving up after {} placement failures",
						attemptLimitInit );
					return;
				}
			} else {
				tile = GridObjectClass::eWall;
			}
		}
		logger.info(
			"Generated world noise in {}+{} attempts [{}ms]",
			objCount,
			attemptLimitInit - attemptLimit,
			float(timer.count<std::micro>()) / 1000.0f );
	}


	template <typename Logger, typename Rng>
	void generateWorldPath(Logger&& logger, World& dst, Rng&& rng, unsigned maxLength) {
		util::SteadyTimer<> timer;
		auto genUint = [&](auto min, auto max) { return std::uniform_int_distribution<unsigned>(min, max)(rng); };
		auto w = dst.width();
		auto h = dst.height();
		assert(w > 1 || h > 1);
		maxLength = std::min<unsigned>(maxLength, (w + h) - decltype(w)(2));
		unsigned x = genUint(0, w-1);
		unsigned y = genUint(0, h-1);
		while(maxLength > 0) {
			unsigned* comp;
			unsigned  compEnd;
			unsigned  inc;
			if(genUint(0, 1) == 0) {
				comp = &x;
				compEnd = genUint(0, 1)? 0 : (w-1);
				inc = compEnd == 0? -1 : 1;
			} else {
				comp = &y;
				compEnd = genUint(0, 1)? 0 : (h-1);
				inc = compEnd == 0? -1 : 1;
			}
			unsigned segmLengthMax = (int(compEnd) - int(*comp)) * inc;
			segmLengthMax = std::min(segmLengthMax, maxLength);
			if(segmLengthMax == 0) [[unlikely]] {
				-- maxLength;
			} else {
				unsigned segmLength = genUint(1, segmLengthMax);
				maxLength -= segmLength;
				#define FWD_ { assert(*comp != compEnd); *comp += inc; dst.tile(x, y) = GridObjectClass::eNoObject; }
					FWD_
					while(--segmLength > 0) FWD_
				#undef FWD_
			}
		}
		logger.info(
			"Generated world path [{}ms]",
			float(timer.count<std::micro>()) / 1000.0f );
	}

}
