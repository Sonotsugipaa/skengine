#include "worldgen.hpp"

#include <deque>
#include <set>
#include <map>
#include <bit>
#include <limits>
#include <cassert>
#include <concepts>

#include "basic_unordered_sets.hpp"

#include <timer.tpp>

#include <glm/detail/func_exponential.inl>



namespace sneka {

	using ucomp_t = uint64_t;
	using scomp_t = std::make_signed_t<ucomp_t>;
	using fcomp_t = long double;
	using Pos = Vec2<scomp_t>;
	using Edge = std::pair<Pos, Pos>;
	using Rng = std::minstd_rand;


	template <typename T>
	struct SortedRefPair {
		T& v1;
		T& v2;
	};

	template <typename T>
	auto sortPair(T& a1, T& a2) {
		return (a1 < a2)? SortedRefPair { a1, a2 } : SortedRefPair { a2, a1 };
	}


	struct Ugraph {
		std::map<Pos, std::set<Pos>, std::less<>> edges;

		void insert(const Pos& p1, const Pos& p2) {
			if(p1 == p2) [[unlikely]] return;
			edges[p1].emplace(p2);
			edges[p2].emplace(p1);
		}

		void erase(const Pos& p1, const Pos& p2) {
			if(p1 == p2) [[unlikely]] return;
			auto vSet1 = edges.find(p1);
			auto vSet2 = edges.find(p2);
			assert((vSet1 == edges.end()) == (vSet2 == edges.end()));
			if(vSet1 == edges.end()) return;
			vSet1->second.erase(p2); if(vSet1->second.empty()) edges.erase(vSet1);
			vSet2->second.erase(p1); if(vSet2->second.empty()) edges.erase(vSet2);
		}

		bool hasEdge(const Pos& p1, const Pos& p2) {
			if(p1 == p2) [[unlikely]] return true;
			auto found = edges.find(p1);
			if(found != edges.end() && found->second.contains(p2)) return true;
			return false;
		}

		bool hasEdgeOrInverse(const Pos& p1, const Pos& p2) { return hasEdge(p1, p2); }

		void getAllEdges(std::vector<Edge>* dst) {
			for(auto&& [ s, dSet ] : edges) for(auto&& d : dSet) {
				if(s < d) dst->emplace_back(s, d); }
		}

		void findEdgesFrom(std::vector<Pos>* dst, const Pos& k) {
			assert(dst != nullptr);
			auto found = edges.find(k);
			if(found != edges.end()) for(auto&& v : found->second) dst->emplace_back(v);
		}

		void findEdgesTo(std::vector<Pos>* dst, const Pos& k) { findEdgesFrom(dst, k); }

		size_t countEdgesFrom(const Pos& k) {
			auto found = edges.find(k);
			if(found == edges.end()) return 0;
			size_t r = 0;
			for(auto&& v : found->second) if(k < v) ++ r;
			return r;
		}

		size_t countEdgesTo(const Pos& k) { return countEdgesFrom(k); }

		size_t countEdgesThrough(const Pos& k) { return countEdgesFrom(k); }
	};


	struct Dgraph {
		std::map<Pos, std::set<Pos>, std::less<>> fwd;
		std::map<Pos, std::set<Pos>, std::less<>> bwd;

		void insert(const Pos& src, const Pos& dst) {
			if(src == dst) [[unlikely]] return;
			fwd[src].emplace(dst);
			bwd[dst].emplace(src);
		}

		void erase(const Pos& src, const Pos& dst) {
			if(src == dst) [[unlikely]] return;
			auto erase = [](decltype(fwd)& map, const Pos& p1, const Pos& p2) {
				auto iter = map.find(p1);
				auto& [ k, v ] = *iter;
				v.erase(p2);
				if(v.empty()) map.erase(iter);
			};
			erase(fwd, src, dst);
			erase(bwd, dst, src);
		}

		bool hasEdge(const Pos& p1, const Pos& p2) {
			if(p1 == p2) [[unlikely]] return true;
			auto found = fwd.find(p1);
			if(found != fwd.end() && found->second.contains(p2)) return true;
			found = bwd.find(p2);
			if(found != bwd.end() && found->second.contains(p1)) return true;
			return false;
		}

		bool hasEdgeOrInverse(const Pos& p1, const Pos& p2) { return hasEdge(p1, p2) || hasEdge(p2, p1); }

		void getAllEdges(std::vector<Edge>* dst) {
			for(auto&& [ s, dSet ] : fwd) for(auto&& d : dSet) dst->emplace_back(s, d);
			for(auto&& [ s, dSet ] : bwd) for(auto&& d : dSet) dst->emplace_back(s, d);
		}

		void findEdgesFrom(std::vector<Pos>* dstContainer, const Pos& dst) {
			assert(dstContainer != nullptr);
			auto found = fwd.find(dst);
			if(found != fwd.end()) for(auto&& v : found->second) dstContainer->emplace_back(v);
		}

		void findEdgesTo(std::vector<Pos>* dstContainer, const Pos& src) {
			assert(dstContainer != nullptr);
			auto found = bwd.find(src);
			if(found != bwd.end()) for(auto&& v : found->second) dstContainer->emplace_back(v);
		}

		size_t countEdgesFrom(const Pos& p) {
			auto found = fwd.find(p);
			if(found == fwd.end()) return 0;
			return found->second.size();
		}

		size_t countEdgesTo(const Pos& p) {
			auto found = bwd.find(p);
			if(found == bwd.end()) return 0;
			return found->second.size();
		}

		size_t countEdgesThrough(const Pos& k) { return countEdgesFrom(k) + countEdgesTo(k); }

	};


	struct Rect {
		scomp_t left;
		scomp_t bottom;
		scomp_t right;
		scomp_t top;
		ucomp_t width () const noexcept { return right - left + 1; }
		ucomp_t height() const noexcept { return top - bottom + 1; }
	};


	template <std::floating_point T> auto genFloat(Rng& rng, T min, auto max) { return std::uniform_real_distribution<decltype(auto(min))>(min, max)(rng); };
	template <std::integral       T> auto genInt  (Rng& rng, T min, auto max) { return std::uniform_int_distribution<decltype(auto(min))>(min, max)(rng); };
	template <std::floating_point T> auto rollProb(Rng& rng, T prob) { return (prob > genFloat(rng, decltype(auto(prob))(0.0), 1.0f)); };


	template <typename Graph>
	void randomGraphFromGrid(Graph* dst, Rng& rng, const Rect& bounds) {
		std::vector<Pos> foundNeighbors; foundNeighbors.reserve(4);
		std::set<Pos> unexplored;
		auto findNeighbors = [&](const Pos& p) {
			foundNeighbors.clear();
			if(p.x > bounds.left)   [[likely]] foundNeighbors.emplace_back(p.x - 1, p.y);
			if(p.y > bounds.bottom) [[likely]] foundNeighbors.emplace_back(p.x, p.y - 1);
			if(p.x < bounds.right)  [[likely]] foundNeighbors.emplace_back(p.x + 1, p.y);
			if(p.y < bounds.top)    [[likely]] foundNeighbors.emplace_back(p.x, p.y + 1);
			assert(foundNeighbors.size() >= 2);
			std::shuffle(foundNeighbors.begin(), foundNeighbors.end(), rng);
		};
		for(scomp_t x = bounds.left;   x <= bounds.right; ++ x)
		for(scomp_t y = bounds.bottom; y <= bounds.top;   ++ y) {
			unexplored.emplace(x, y);
		}
		while(! unexplored.empty()) {
			std::set<Pos>::iterator p0iter; do {
				p0iter = unexplored.lower_bound(Pos(
					genInt(rng, bounds.left, bounds.right),
					genInt(rng, bounds.bottom, bounds.top) ));
			} while(p0iter == unexplored.end());
			auto& p0 = *p0iter;
			findNeighbors(p0);
			for(auto&& p1 : foundNeighbors) {
				if(! dst->hasEdgeOrInverse(p0, p1)) {
					dst->insert(p0, p1);
					if(rollProb(rng, 0.6f)) break;
				}
			}
			unexplored.erase(p0iter);
		}
		for(scomp_t x = bounds.left;   x <= bounds.right; ++ x)
		for(scomp_t y = bounds.bottom; y <= bounds.top;   ++ y) {
			Pos p = { x, y };
			auto incidence = dst->countEdgesThrough(p);
			if(incidence > 2) continue;
			assert(incidence >= 1);
			findNeighbors(p);
			for(auto&& n : foundNeighbors) {
				if(! dst->hasEdgeOrInverse(p, n)) {
					dst->insert(p, n);
					break;
				}
			}
			assert(dst->countEdgesThrough(p) >= 2);
		}
		#warning "TODO: use a proper visit-based algorithm for this, as it is now it's very inefficient"
	}


	template <typename Logger, typename Rng>
	void generateWorldNoise(Logger& logger, World& dst, Rng&& rng) {
		util::SteadyTimer<> timer;
		auto w = dst.width();
		auto h = dst.height();
		assert(w * h > 0);
		float objCountRel = genFloat(rng, 0.3f, 0.95f);
		float wallToObstRatio = genFloat(rng, 0.3f, 1.2f);
		unsigned objCount = float(w * h) * objCountRel;
		const unsigned attemptLimitInit = objCount * (1.0f / (1.0f - objCountRel));
		unsigned attemptLimit = attemptLimitInit;
		for(unsigned i = 1; i <= objCount; ++ i) {
			assert(i > 0 /* `--i` needs to be possible */);
			auto xGen = genInt(rng, 0, w-1);
			auto yGen = genInt(rng, 0, h-1);
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
				float typeRoll = genFloat(rng, 0.0f, 1.0f + wallToObstRatio);
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


	template <std::integral T>
	auto mkRngFromPos(Rng::result_type seed, const Vec2<T>& p) {
		using Ut = std::make_unsigned_t<T>;
		return Rng(std::rotr(Ut(p.x), 5) ^ std::rotl(Ut(p.y), 5) ^ seed);
	};


	auto getZoneRect(const Pos& zonePos, const Vec2<ucomp_t>& avgZoneSize, const Rect& bounds) {
		Pos tl = { zonePos.x * scomp_t(avgZoneSize.x), zonePos.y * scomp_t(avgZoneSize.y) };
		assert(tl.x >= bounds.left);
		assert(tl.y >= bounds.bottom);
		assert(tl.x <= bounds.right);
		assert(tl.y <= bounds.top);
		auto r = Rect {
			std::max<scomp_t>(tl.x, scomp_t(bounds.left)),
			std::max<scomp_t>(tl.y, scomp_t(bounds.bottom)),
			std::min<scomp_t>(tl.x + scomp_t(avgZoneSize.x), scomp_t(bounds.right)),
			std::min<scomp_t>(tl.y + scomp_t(avgZoneSize.y), scomp_t(bounds.top)) };
		assert(r.bottom <= r.top);
		assert(r.left <= r.right);
		return r;
	};


	auto randomPointBetweenZones(Rng::result_type seed, const Rect& mzRect, const Pos& mainZonePos, const Pos& otherZonePos) {
		auto& mzp = mainZonePos;
		auto& ozp = otherZonePos;
		auto zoneRng = mkRngFromPos(seed, (mainZonePos < otherZonePos)? mainZonePos : otherZonePos);
		bool diffIsHoz = (mainZonePos.y == otherZonePos.y);
		assert(diffIsHoz != (mainZonePos.x == otherZonePos.x));
		scomp_t sideOffset;
		scomp_t sideComp;
		if(diffIsHoz) {
			sideOffset = std::uniform_int_distribution<scomp_t>(
				mzRect.bottom , mzRect.top )(zoneRng);
			sideComp = (mzp.x < ozp.x)? mzRect.right : mzRect.left;
		} else {
			sideOffset = std::uniform_int_distribution<scomp_t>(
				mzRect.left, mzRect.right  )(zoneRng);
			sideComp = (mzp.y < ozp.y)? mzRect.top : mzRect.bottom;
		}
		auto r = diffIsHoz?
			Pos(sideComp, sideOffset) :
			Pos(sideOffset, sideComp);
		return r;
	};


	struct ZoneGenParams {
		Vec2<ucomp_t> avgZoneSize;
		Rect bounds;
		fcomp_t pointObjProb;
		fcomp_t pointObjContProb;
	};


	template <std::ranges::input_range SrcRange, std::ranges::input_range DstRange>
	void generateAtomicZone(
		ske::Logger& logger, World& dst, Rng& rng, Rng::result_type seed,
		const ZoneGenParams& params,
		const Pos& zonePos, const SrcRange& srcPoints, const DstRange& dstPoints
	) {
		(void) logger;

		auto zoneRng = mkRngFromPos(seed, zonePos);
		auto mzRect = getZoneRect(zonePos, params.avgZoneSize, params.bounds);
		auto xDist = std::uniform_int_distribution(mzRect.left, mzRect.right);
		auto yDist = std::uniform_int_distribution(mzRect.bottom, mzRect.top);
		auto midPoint = Pos(xDist(zoneRng), yDist(zoneRng));
		bool lastPathHadPoints = false;

		auto carveAxisAligned = [&](Pos curPos, const Pos& endPos, bool vertical, bool doDrawPoints, unsigned& skipPoints, unsigned& pointCtr) {
			// `endPos` is not the idiomatic "end": the interval is [curPos, endPos] instead of [curPos, endPos)
			auto* curComp = vertical? (&curPos.y) : (&curPos.x);
			auto* endComp = vertical? (&endPos.y) : (&endPos.x);
			scomp_t step = (*curComp < *endComp)? +1 : -1;
			auto setTile = [&](scomp_t x, scomp_t y) {
				assert(x >= 0);
				assert(y >= 0);
				assert(ucomp_t(x) < dst.width());
				assert(ucomp_t(y) < dst.height());
				if(! doDrawPoints) {
					dst.tile(x, y) = GridObjectClass::eNoObject;
				} else {
					if(skipPoints > 0) {
						-- skipPoints;
						dst.tile(x, y) = GridObjectClass::eNoObject;
					} else if(pointCtr < 1) {
						dst.tile(x, y) = GridObjectClass::eNoObject;
					} else {
						dst.tile(x, y) = GridObjectClass::ePoint;
						-- pointCtr;
					}
				}
			};
			auto move = [&]() {
				assert(curPos.x <= params.bounds.right);
				assert(curPos.y <= params.bounds.top);
				setTile(curPos.x, curPos.y);
				*curComp += step;
			};
			auto cond = [&]() { return (*curComp != *endComp); };
			if(cond()) {
				move();
			}
			while(cond()) {
				move();
			}
			setTile(curPos.x, curPos.y);
			return curPos;
		};

		auto drawDiagonal = [&](Pos curPos, const Pos& endPos, bool verticalFirst, bool doDrawPoints) {
			// See comment in `carveAxisAligned`
			unsigned pathLen = std::abs(scomp_t(curPos.x - endPos.x)) + std::abs(scomp_t(curPos.y - endPos.y));
			unsigned skipPoints = genInt(rng, 0u, std::max(1u, pathLen)-1);
			unsigned pointCtr = genInt(rng, 1, std::max(std::max(1u, skipPoints), pathLen) - skipPoints);
			curPos = carveAxisAligned(curPos, endPos,   verticalFirst, doDrawPoints, skipPoints, pointCtr);
			curPos = carveAxisAligned(curPos, endPos, ! verticalFirst, doDrawPoints, skipPoints, pointCtr);
			return curPos;
		};

		auto drawPath = [&](const Pos& src, const Pos& dst, bool verticalFirst) {
			bool doDrawPoints = rollProb(rng, lastPathHadPoints? params.pointObjContProb : params.pointObjProb);
			lastPathHadPoints = doDrawPoints;
			drawDiagonal(src, dst, verticalFirst, doDrawPoints);
		};

		for(auto&& srcZone : srcPoints) {
			auto entrance = randomPointBetweenZones(seed, mzRect, zonePos, srcZone);
			drawPath(entrance, midPoint, true);
		}
		for(auto&& dstZone : dstPoints) {
			auto exit = randomPointBetweenZones(seed, mzRect, zonePos, dstZone);
			drawPath(midPoint, exit, false);
		}
	}


	Pos generateWorld(ske::Logger& logger, World& dst, std::optional<Pos> origin, Rng::result_type seed) {
		const scomp_t w = dst.width();
		const scomp_t h = dst.height();
		assert(w >= 2);
		assert(h >= 2);
		auto rng = Rng(seed);
		if(w * h < 2) return Pos { };
		fcomp_t widthHeightAvg = fcomp_t(w) + fcomp_t(h) / fcomp_t(2.0);
		auto worldBounds = Rect { 0, 0, w-1, h-1 };
		auto worldArea = fcomp_t(w) * fcomp_t(h);
		scomp_t zoneCountHoz = std::log2<scomp_t>(w);
		scomp_t zoneCountVrt = std::log2<scomp_t>(h);
		fcomp_t pointObjProb     = genFloat(rng, worldArea / fcomp_t(20), worldArea / fcomp_t(10));
		fcomp_t pointObjContProb = genFloat(rng, fcomp_t(0.1), fcomp_t(0.7));

		generateWorldNoise(logger, dst, std::forward<Rng>(rng));
		util::SteadyTimer<> timer;

		assert(zoneCountHoz <= w/2);
		assert(zoneCountVrt <= h/2);
		auto avgZoneSize = Vec2<ucomp_t>(
			std::floor(fcomp_t(w) / fcomp_t(zoneCountHoz)),
			std::floor(fcomp_t(h) / fcomp_t(zoneCountVrt)) );
		auto zoneGridBounds = Rect { 0, 0, zoneCountHoz-1, zoneCountVrt-1 };
		logger.debug("Generating {}x{} zones with avg. size {}x{}", zoneCountHoz, zoneCountVrt, avgZoneSize.x, avgZoneSize.y);

		Pos actualOrigin = origin.has_value()?
			origin.value() :
			Pos(
				genInt(rng, zoneGridBounds.left,  zoneGridBounds.bottom),
				genInt(rng, zoneGridBounds.right, zoneGridBounds.top) );
		Dgraph zoneGraph; { // Select zones
			randomGraphFromGrid(
				&zoneGraph, rng,
				zoneGridBounds );
			//#ifndef NDEBUG // Thoroughly search the graph for SCC zones (which shouldn't exist)
			//	for(scomp_t x = 0; x < zoneCountHoz; ++ x)
			//	for(scomp_t y = 0; y < zoneCountVrt; ++ y) {
			//		auto p = Pos(x, y);
			//		assert(zoneGraph.countEdgesFrom(p) > 0);
			//		assert(zoneGraph.countEdgesTo  (p) > 0);
			//	}
			//#endif
		}

		{ // Connect each zone as determined by the graph
			std::vector<Pos> srcPoints; srcPoints.reserve(4);
			std::vector<Pos> dstPoints; dstPoints.reserve(4);
			ZoneGenParams zgParams = {
				.avgZoneSize = avgZoneSize,
				.bounds = worldBounds,
				.pointObjProb = pointObjProb,
				.pointObjContProb = pointObjContProb
			};
			{ // Find the starting point
				auto originZone = Pos(actualOrigin.x / w, actualOrigin.y / h);
				auto zoneRng = mkRngFromPos(seed, originZone);
				auto mzRect = getZoneRect(originZone, avgZoneSize, worldBounds);
				auto xDist = std::uniform_int_distribution(mzRect.left, mzRect.right);
				auto yDist = std::uniform_int_distribution(mzRect.bottom, mzRect.top);
				actualOrigin = Pos(xDist(zoneRng), yDist(zoneRng));
			}
			for(scomp_t x = 0; x < zoneCountHoz; ++x)
			for(scomp_t y = 0; y < zoneCountVrt; ++y) {
				auto midZone = Pos(x, y);
				zoneGraph.findEdgesTo(&srcPoints, midZone);
				zoneGraph.findEdgesFrom(&dstPoints, midZone);
				generateAtomicZone(logger, dst, rng, seed, zgParams, midZone, srcPoints, dstPoints);
				srcPoints.clear();
				dstPoints.clear();
			}
		}

		logger.info(
			"Generated world paths [{}ms]",
			float(timer.count<std::micro>()) / 1000.0f );

		dst.entryPointX() = actualOrigin.x;
		dst.entryPointY() = actualOrigin.y;

		return actualOrigin;
	}

}
