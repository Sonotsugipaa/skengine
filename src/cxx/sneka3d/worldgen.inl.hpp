#pragma once

#include <cassert>
#include <random>
#include <bit>
#include <unordered_set>
#include <concepts>

#include <timer.tpp>

#include "basic_unordered_sets.hpp"
#include "world.hpp"



namespace sneka {

	template <std::integral T, typename Rng, T minBits = std::numeric_limits<T>::digits>
	inline auto random(Rng rng) {
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
		float objCountRel = genFloat(0.4f, 0.95f);
		float wallToObstRatio = genFloat(0.5f, 2.0f);
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
				float typeRoll = genFloat(0.0f, 1.0f + wallToObstRatio);
				if(typeRoll > 1.0f) tile = GridObjectClass::eWall;
				else                tile = GridObjectClass::eObstacle;
			}
		}
		logger.info(
			"Generated world noise in {}+{} attempts [{}ms]",
			objCount,
			attemptLimitInit - attemptLimit,
			float(timer.count<std::micro>()) / 1000.0f );
	}


	template <typename Logger, typename Rng>
	auto generateWorld(Logger&& logger, World& dst, BasicUset<Vec2<uint64_t>>* dstPtObjs, Vec2<uint64_t> startPos, Rng&& rng) {
		using ucomp_t = uint64_t;
		using scomp_t = std::make_signed_t<ucomp_t>;
		using fcomp_t = long double;
		using Pos = Vec2<ucomp_t>;
		const auto w = dst.width();
		const auto h = dst.height();
		if(w * h < 2) return Pos { };
		auto genFloat = [&](auto min, auto max) { return std::uniform_real_distribution<decltype(auto(min))>(min, max)(rng); };
		auto genInt   = [&](auto min, auto max) { return std::uniform_int_distribution<decltype(auto(min))>(min, max)(rng); };
		auto rollProb = [&](auto prob) { return (prob > genFloat(decltype(auto(prob))(0.0), 1.0f)); };
		auto junctionMap        = BasicUmap<ucomp_t, Pos>(16);
		auto junctionSet        = BasicUset<Pos>(16);
		auto pointObjCandidates = BasicUset<Pos>(16);
		junctionMap.max_load_factor(4.0f);
		junctionSet.max_load_factor(4.0f);
		fcomp_t widthHeightAvg = fcomp_t(w) + fcomp_t(h) / fcomp_t(2.0);
		fcomp_t widthHeightAvgSq = widthHeightAvg * widthHeightAvg;

		ucomp_t minPathTiles = genInt(ucomp_t(4), std::min(w, h) / ucomp_t(2));
		ucomp_t maxPathTiles = genInt(ucomp_t(widthHeightAvgSq / fcomp_t(8)), ucomp_t(widthHeightAvgSq / fcomp_t(3)));
		fcomp_t tJunctionProb      = genFloat(fcomp_t(  0.3), fcomp_t(0.6)) / ((fcomp_t(minPathTiles) + widthHeightAvg) / fcomp_t(2.0));
		fcomp_t xJunctionProb      = genFloat(fcomp_t(  0.4), fcomp_t(0.9));
		fcomp_t targetJunctionProb = genFloat(fcomp_t( 0.05), fcomp_t(0.3));
		fcomp_t deadEndProb        = genFloat(fcomp_t(0.005), fcomp_t(0.5));
		fcomp_t diagonalCompBias   = genFloat(fcomp_t(  0.4), fcomp_t(0.6));
		fcomp_t pointObjProb       = genFloat(fcomp_t( 0.05), fcomp_t(0.2));
		fcomp_t maxDiagonalDist    = genFloat(glm::sqrt(std::min<fcomp_t>(w, h)), widthHeightAvg);

		auto randomPos = [&]() {
			return Pos { genInt(ucomp_t(0), w-1), genInt(ucomp_t(0), h-1) };
		};
		auto randomPosAround = [&](Vec2<ucomp_t> src, fcomp_t maxDistSq) {
			auto rx = genFloat(-maxDistSq, +maxDistSq);
			auto ry = genFloat(-maxDistSq, +maxDistSq);
			fcomp_t ptLen = glm::sqrt((rx*rx) + (ry*ry));
			fcomp_t rndDist = genFloat(fcomp_t(2.0), maxDistSq);
			rndDist /= ptLen;
			rx *= rndDist; ry *= rndDist;
			ucomp_t rrx = std::clamp(scomp_t(src.x) + scomp_t(rx), scomp_t(0), scomp_t(w-1));
			ucomp_t rry = std::clamp(scomp_t(src.y) + scomp_t(ry), scomp_t(0), scomp_t(h-1));
			return Pos { rrx, rry };
		};

		auto randomJunction = [&]() -> auto {
			assert(! junctionMap.empty() /* Not *necessary*, but it likely results in unreachable path tiles */);
			if(junctionMap.empty()) [[unlikely]] return randomPos();
			auto rndIdx = genInt(ucomp_t(0), junctionMap.size() - 1);
			assert(junctionMap.contains(rndIdx));
			return junctionMap.find(rndIdx)->second;
		};

		auto addJunction = [&](const Pos& p) {
			junctionMap.insert({ junctionSet.size(), p });
			junctionSet.insert(p);
		};

		auto carveAxisAligned = [&](Pos curPos, const Pos& endPos, bool vertical, GridObjectClass obj) {
			// `endPos` is not the idiomatic "end": the interval is [curPos, endPos] instead of [curPos, endPos)
			auto* curComp = vertical? (&curPos.y) : (&curPos.x);
			auto* endComp = vertical? (&endPos.y) : (&endPos.x);
			scomp_t step = (*curComp < *endComp)? +1 : -1;
			constexpr auto setTile = [](World& dst, BasicUset<Vec2<uint64_t>>* dstPtObjs, uint64_t x, uint64_t y, GridObjectClass obj) {
				dst.tile(x, y) = obj;
				if(dstPtObjs != nullptr && obj == GridObjectClass::ePoint) dstPtObjs->insert({ x, y });
			};
			auto move = [&]() {
				if(maxPathTiles > 0) [[likely]] {
					assert(curPos.x < w);
					assert(curPos.y < h);
					setTile(dst, dstPtObjs, curPos.x, curPos.y, obj);
					*curComp += step;
					-- maxPathTiles;
				}
			};
			auto cond = [&]() { return (*curComp != *endComp) && (maxPathTiles > 0); };
			if(cond()) {
				move();
			}
			while(cond()) {
				if(rollProb(tJunctionProb)) addJunction(curPos);
				move();
			}
			setTile(dst, dstPtObjs, curPos.x, curPos.y, obj);
			if(rollProb(xJunctionProb)) addJunction(curPos);
			return curPos;
		};

		auto carveDiagonal = [&](Pos curPos, const Pos& endPos, GridObjectClass obj) {
			// See comment in `carveAxisAligned`
			bool verticalFirst = ! rollProb(diagonalCompBias);
			curPos = carveAxisAligned(curPos, endPos,   verticalFirst, obj);
			curPos = carveAxisAligned(curPos, endPos, ! verticalFirst, obj);
			return curPos;
		};

		generateWorldNoise(std::forward<Logger>(logger), dst, std::forward<Rng>(rng));
		util::SteadyTimer<> timer;
		auto startingPoint = startPos;
		addJunction(startingPoint);
		while(minPathTiles < maxPathTiles) {
			bool targetJunction = rollProb(targetJunctionProb);
			bool createPointObjs = rollProb(pointObjProb);
			bool deadEnd = rollProb(deadEndProb);
			auto target = targetJunction? randomJunction() : randomPosAround(startingPoint, maxDiagonalDist);
			auto obj = createPointObjs? GridObjectClass::ePoint : GridObjectClass::eNoObject;
			startingPoint = carveDiagonal(startingPoint, target, obj);
			if(deadEnd) startingPoint = randomJunction();
		}
		logger.info(
			"Generated world paths [{}ms]",
			float(timer.count<std::micro>()) / 1000.0f );
		return randomJunction();
	}

}
