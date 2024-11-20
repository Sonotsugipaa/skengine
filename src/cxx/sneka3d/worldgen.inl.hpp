#pragma once

#include <cassert>
#include <random>
#include <bit>
#include <unordered_set>
#include <concepts>

#include <timer.tpp>

#include "world.hpp"



namespace sneka {

	template <typename T>
	concept StdHashable = requires(const T& ctr) {
		{ auto(ctr.hash()) } -> std::same_as<std::size_t>;
	};

	template <typename T>
	concept StdEqualComparable = requires(const T& ctr0, const T& ctr1) {
		{ auto(ctr0 == ctr1) } -> std::same_as<bool>;
	};

	template <typename T>
	concept UnorderedKeyType = StdHashable<T> && StdEqualComparable<T>;


	template <typename T>
	requires sneka::StdHashable<T>
	struct StdHash {
		auto operator()(const T& t) const noexcept { return t.hash(); }
	};

	template <typename T>
	requires std::is_trivial_v<T> && (! requires (T t0, T t1) { { auto(t0 == t1) } -> std::convertible_to<bool>; })
	struct StdEqualTo {
		auto operator()(const T& t0, const T& t1) const noexcept { return 0 == memcmp(&t0, &t1, sizeof(T)); }
	};


	template <std::integral Dst, std::integral T>
	constexpr Dst hashValues(T x) noexcept { return x; }

	template <std::integral Dst, std::integral T0, std::integral T1, std::integral... T>
	constexpr Dst hashValues(T0 x0, T1 x1, T... x) noexcept {
		using UnsignedDst = std::make_unsigned_t<Dst>;
		return Dst(
			UnsignedDst(  std::rotl<Dst>(x0, 4)) +
			UnsignedDst(~ std::rotr<Dst>(x0, 7)) +
			UnsignedDst(~ hashValues<Dst>(x1, x...)) );
	}


	template <std::integral T>
	struct Vec2 {
		T x;
		T y;
		constexpr auto hash() const noexcept { return hashValues<size_t>(x, y); }
		constexpr bool operator==(this auto& l, const Vec2& r) noexcept { return (l.x == r.x) && (l.y == r.y); }
	};


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
		float objCountRel = genFloat(0.4f, 0.7f);
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
	auto generateWorld(Logger&& logger, World& dst, Rng&& rng) {
		using comp_t = uint64_t;
		using fcomp_t = long double;
		using Pos = Vec2<comp_t>;
		const auto w = dst.width();
		const auto h = dst.height();
		if(w * h < 2) return Pos { };
		auto genFloat = [&](auto min, auto max) { return std::uniform_real_distribution<decltype(auto(min))>(min, max)(rng); };
		auto genInt   = [&](auto min, auto max) { return std::uniform_int_distribution<decltype(auto(min))>(min, max)(rng); };
		auto rollProb = [&](auto prob) { return (prob > genFloat(decltype(auto(prob))(0.0), 1.0f)); };
		auto junctionMap = std::unordered_map<comp_t, Pos         >(16);
		auto junctionSet = std::unordered_set<Pos   , StdHash<Pos>>(16);
		junctionMap.max_load_factor(4.0f);
		junctionSet.max_load_factor(4.0f);
		comp_t minPathTiles = genInt(comp_t(4), std::min(w, h) / comp_t(2));
		comp_t maxPathTiles = fcomp_t(w) * fcomp_t(h) / fcomp_t(2);

		float tJunctionProb = genFloat(0.05f, 0.3f);
		float xJunctionProb = genFloat(0.4f, 0.9f);
		float targetJunctionProb = genFloat(0.1f, 0.9f);
		float diagonalCompBias = genFloat(0.4f, 0.6f);
		comp_t stopAtTilesLeft = genInt(w / comp_t(2), (w * comp_t(3)) / comp_t(2));

		auto randomPos = [&]() -> auto {
			return Pos { genInt(comp_t(0), w-1), genInt(comp_t(0), h-1) };
		};

		auto randomJunction = [&]() -> auto {
			if(junctionMap.empty()) [[unlikely]] return randomPos();
			auto rndIdx = genInt(0, junctionMap.size() - 1);
			assert(junctionMap.contains(rndIdx));
			return junctionMap.find(rndIdx)->second;
		};

		auto carveAxisAligned = [&](Pos curPos, const Pos& endPos, bool vertical, GridObjectClass obj) {
			// `endPos` is not the idiomatic "end": the interval is [curPos, endPos] instead of [curPos, endPos)
			auto* curComp = vertical? (&curPos.y) : (&curPos.x);
			auto* endComp = vertical? (&endPos.y) : (&endPos.x);
			std::make_signed_t<comp_t> step = (*curComp < *endComp)? +1 : -1;
			auto move = [&]() {
				if(maxPathTiles > 0) [[likely]] {
					assert(curPos.x < w);
					assert(curPos.y < h);
					dst.tile(curPos.x, curPos.y) = obj;
					*curComp += step;
					-- maxPathTiles;
				}
			};
			auto cond = [&]() { return (*curComp != *endComp) && (maxPathTiles > 0); };
			if(cond()) {
				move();
			}
			while(cond()) {
				if(rollProb(tJunctionProb)) junctionSet.insert(curPos);
				move();
			}
			dst.tile(curPos.x, curPos.y) = obj;
			if(rollProb(xJunctionProb)) junctionSet.insert(curPos);
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
		auto startingPoint = randomPos();
		while((minPathTiles < stopAtTilesLeft) && (maxPathTiles > stopAtTilesLeft)) {
			bool targetJunction = rollProb(targetJunctionProb);
			auto target = targetJunction? randomJunction() : randomPos();
			startingPoint = carveDiagonal(startingPoint, target, GridObjectClass::eNoObject);
		}
		logger.info(
			"Generated world paths [{}ms]",
			float(timer.count<std::micro>()) / 1000.0f );
		return randomJunction();
	}

}
