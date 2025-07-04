#include "worldgen.hpp"

#include <deque>
#include <set>
#include <map>
#include <bit>
#include <limits>
#include <cassert>
#include <concepts>

#include <timer.tpp>

#include <glm/detail/func_exponential.inl>



namespace sneka {

	using ucomp_t = uint64_t;
	using scomp_t = std::make_signed_t<ucomp_t>;
	using fcomp_t = long double;
	using Pos = Vec2<ucomp_t>;
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
			if(found != fwd.end() && found->second.contains(p1)) return true;
			return false;
		}

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
	};


	struct Rect {
		ucomp_t top;
		ucomp_t left;
		ucomp_t bottom;
		ucomp_t right;
		ucomp_t width () const noexcept { return right - left + 1; }
		ucomp_t height() const noexcept { return bottom - top + 1; }
	};


	template <typename Graph>
	std::set<Pos> randomDfsTree(Graph* dst, Rng& rng, const Rect& bounds, const Pos& src) {
		std::deque<Pos> q;
		std::set<Pos> deadEnds;
		std::set<Pos> visited;
		std::vector<Pos> vec4; vec4.reserve(4);
		auto visitNeighbors = [&](const Pos& p, bool leaveOne) {
			if(p.x > bounds.left)   [[likely]] vec4.emplace_back(p.x - 1, p.y);
			if(p.y > bounds.top)    [[likely]] vec4.emplace_back(p.x, p.y - 1);
			if(p.x < bounds.right)  [[likely]] vec4.emplace_back(p.x + 1, p.y);
			if(p.y < bounds.bottom) [[likely]] vec4.emplace_back(p.x, p.y + 1);
			std::shuffle(vec4.begin(), vec4.end(), rng);
			unsigned char visitCount = 0;
			for(auto&& ins : vec4) {
				if(leaveOne) {
					leaveOne = false;
					continue;
				}
				bool notAlreadyVisited = visited.emplace(ins).second;
				if(notAlreadyVisited) {
					dst->insert(p, ins);
					q.emplace_back(ins);
					++ visitCount;
				}
			}
			if(visitCount == 0) [[unlikely]] deadEnds.emplace(p);
			vec4.clear();
		};
		visitNeighbors(src, true);
		while(! q.empty()) {
			Pos p = q.front();
			q.pop_front();
			visitNeighbors(p, false);
		}
		return deadEnds;
	}


	template <typename Graph>
	Pos connectRandomNeighbor(Graph* dst, Rng& rng, const Rect& bounds, const Pos& src) {
		std::vector<Pos> vec4; vec4.reserve(4);
		if(src.x > bounds.left)   [[likely]] vec4.emplace_back(src.x - 1, src.y);
		if(src.y > bounds.top)    [[likely]] vec4.emplace_back(src.x, src.y - 1);
		if(src.x < bounds.right)  [[likely]] vec4.emplace_back(src.x + 1, src.y);
		if(src.y < bounds.bottom) [[likely]] vec4.emplace_back(src.x, src.y + 1);
		assert(vec4.size() >= 2);
		std::shuffle(vec4.begin(), vec4.end(), rng);
		for(auto&& candidate : vec4) {
			if(! dst->hasEdge(candidate, src)) {
				dst->insert(src, candidate);
				dst->insert(candidate, src);
				return candidate;
			}
		}
		auto& lastCandidate = vec4.back();
		dst->insert(src, lastCandidate);
		return lastCandidate;
	}


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
	void generateWorldNoise(Logger& logger, World& dst, Rng&& rng) {
		util::SteadyTimer<> timer;
		auto genFloat = [&](auto min, auto max) { return std::uniform_real_distribution<float>(min, max)(rng); };
		auto genUint  = [&](auto min, auto max) { return std::uniform_int_distribution<unsigned>(min, max)(rng); };
		auto w = dst.width();
		auto h = dst.height();
		assert(w * h > 0);
		float objCountRel = genFloat(0.3f, 0.95f);
		float wallToObstRatio = genFloat(0.3f, 1.2f);
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


	Pos generateWorld(ske::Logger& logger, World& dst, BasicUset<Pos>* dstPtObjs, std::optional<Pos> origin, std::minstd_rand::result_type seed) {
		const auto w = dst.width();
		const auto h = dst.height();
		assert(w >= 2);
		assert(h >= 2);
		auto rng = Rng(seed);
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
		auto worldBounds = Rect { 0, 0, w-1, h-1 };
		auto worldArea = fcomp_t(w) * fcomp_t(h);

		ucomp_t minPathTiles = genInt(ucomp_t(4), std::min(w, h) / ucomp_t(2));
		ucomp_t maxPathTiles = genInt(ucomp_t(widthHeightAvgSq / fcomp_t(8)), ucomp_t(widthHeightAvgSq / fcomp_t(3)));
		fcomp_t tJunctionProb      = genFloat(fcomp_t(  0.3), fcomp_t(0.6)) / ((fcomp_t(minPathTiles) + widthHeightAvg) / fcomp_t(2.0));
		fcomp_t xJunctionProb      = genFloat(fcomp_t(  0.4), fcomp_t(0.9));
		fcomp_t targetJunctionProb = genFloat(fcomp_t( 0.05), fcomp_t(0.3));
		fcomp_t deadEndProb        = genFloat(fcomp_t(0.005), fcomp_t(0.5));
		fcomp_t diagonalCompBias   = genFloat(fcomp_t(  0.3), fcomp_t(0.7));
		fcomp_t zoneLogBase        = genFloat(fcomp_t(  3.0), fcomp_t(10.0));
		fcomp_t pointObjProb       = genFloat(widthHeightAvg / fcomp_t(12), widthHeightAvg / fcomp_t(5));
		fcomp_t pointObjContProb   = genFloat(fcomp_t(0.1), fcomp_t(0.7));
		fcomp_t maxDiagonalDist    = genFloat(glm::sqrt(std::min<fcomp_t>(w, h)), widthHeightAvg);

		auto randomPos = [&]() {
			return Pos { genInt(ucomp_t(0), w-1), genInt(ucomp_t(0), h-1) };
		};
		auto randomPosWithinRect = [&](const Rect& rect) {
			return Pos { genInt(rect.left, rect.right), genInt(rect.top, rect.bottom) };
		};
		auto randomPosAroundPos = [&](Pos src, fcomp_t maxDistSq) {
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

		auto randomNeighbor = [&](Pos origin, const Pos& boundTl, const Pos& boundBr) {
			bool vertical = rollProb(0.5f);
			auto* comp       = &(vertical? origin.y : origin.x);
			auto  lowerbound =  (vertical? boundTl.y : boundTl.x);
			auto  upperbound =  (vertical? boundBr.y : boundBr.x);
			ucomp_t inc;
			if     (*comp <= lowerbound) inc = +1;
			else if(*comp >= upperbound) inc = -1;
			else                         inc = rollProb(0.5f)? +1 : -1;
			*comp += inc;
			return origin;
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

		auto carveAxisAligned = [&](Pos curPos, const Pos& endPos, bool vertical, bool doDrawPoints, unsigned& skipPoints, unsigned& pointCtr) {
			// `endPos` is not the idiomatic "end": the interval is [curPos, endPos] instead of [curPos, endPos)
			auto* curComp = vertical? (&curPos.y) : (&curPos.x);
			auto* endComp = vertical? (&endPos.y) : (&endPos.x);
			scomp_t step = (*curComp < *endComp)? +1 : -1;
			auto setTile = [&](ucomp_t x, ucomp_t y) {
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
						if(dstPtObjs != nullptr) dstPtObjs->insert({ x, y });
						-- pointCtr;
					}
				}
			};
			auto move = [&]() {
				if(maxPathTiles > 0) [[likely]] {
					assert(curPos.x < w);
					assert(curPos.y < h);
					setTile(curPos.x, curPos.y);
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
			setTile(curPos.x, curPos.y);
			if(rollProb(xJunctionProb)) addJunction(curPos);
			return curPos;
		};

		auto drawDiagonal = [&](Pos curPos, const Pos& endPos, bool verticalFirst, bool doDrawPoints) {
			// See comment in `carveAxisAligned`
			unsigned pathLen = std::abs(scomp_t(curPos.x - endPos.x)) + std::abs(scomp_t(curPos.y - endPos.y));
			unsigned skipPoints = genInt(0u, std::max(1u, pathLen)-1);
			unsigned pointCtr = genInt(1, std::max(std::max(1u, skipPoints), pathLen) - skipPoints);
			curPos = carveAxisAligned(curPos, endPos,   verticalFirst, doDrawPoints, skipPoints, pointCtr);
			curPos = carveAxisAligned(curPos, endPos, ! verticalFirst, doDrawPoints, skipPoints, pointCtr);
			return curPos;
		};

		auto mkPosRng = [&](const Pos& p) { return Rng(p.x ^ std::rotl(p.y, 5) ^ std::rotl(seed, 10)); };

		generateWorldNoise(logger, dst, std::forward<Rng>(rng));
		util::SteadyTimer<> timer;

		ucomp_t zoneCountHoz = std::log2<ucomp_t>(w * 4);
		ucomp_t zoneCountVrt = std::log2<ucomp_t>(h * 4);
		ucomp_t avgZoneWidth  = std::ceil(fcomp_t(w) / fcomp_t(zoneCountHoz));
		ucomp_t avgZoneHeight = std::ceil(fcomp_t(h) / fcomp_t(zoneCountVrt));
		auto zoneGridBounds = Rect { 0, 0, zoneCountHoz-1, zoneCountVrt-1 };
		logger.debug("Generating {}x{} zones with avg. size {}x{}", zoneCountHoz, zoneCountVrt, avgZoneWidth, avgZoneHeight);
		auto getZoneRect = [&](const Pos& zonePos) {
			Pos tl = { zonePos.x * avgZoneWidth, zonePos.y * avgZoneHeight };
			assert(tl.x < w);
			assert(tl.y < h);
			auto r = Rect {
				tl.y,
				tl.x,
				std::min(tl.y + avgZoneHeight, h-1),
				std::min(tl.x + avgZoneWidth,  w-1) };
			assert(r.top <= r.bottom);
			assert(r.left <= r.right);
			return r;
		};

		auto randomPointBetweenZones = [&](const Rect& mzRect, const Pos& mainZonePos, const Pos& otherZonePos) {
			auto& mzp = mainZonePos;
			auto& ozp = otherZonePos;
			auto zoneRng = mkPosRng((mainZonePos < otherZonePos)? mainZonePos : otherZonePos);
			bool diffIsHoz = (mainZonePos.y == otherZonePos.y);
			assert(diffIsHoz != (mainZonePos.x == otherZonePos.x));
			ucomp_t sideOffset;
			ucomp_t sideComp;
			if(diffIsHoz) {
				sideOffset = std::uniform_int_distribution<ucomp_t>(
					mzRect.top , mzRect.bottom )(zoneRng);
				sideComp = (mzp.x < ozp.x)? mzRect.right : mzRect.left;
			} else {
				sideOffset = std::uniform_int_distribution<ucomp_t>(
					mzRect.left, mzRect.right  )(zoneRng);
				sideComp = (mzp.y < ozp.y)? mzRect.bottom : mzRect.top;
			}
			return diffIsHoz?
				Pos(sideOffset, sideComp) :
				Pos(sideComp, sideOffset);
		};

		Pos actualOrigin = origin.has_value()?
			origin.value() :
			Pos(
				genInt(zoneGridBounds.left,  zoneGridBounds.top),
				genInt(zoneGridBounds.right, zoneGridBounds.bottom) );
		Dgraph zoneGraph; { // Select zones
			auto deadEnds = randomDfsTree(
				&zoneGraph, rng,
				zoneGridBounds,
				actualOrigin );
			while(! deadEnds.empty()) {
				auto& deadEnd = *deadEnds.begin();
				auto connectedTo = connectRandomNeighbor(&zoneGraph, rng, zoneGridBounds, deadEnd);
				deadEnds.erase(deadEnd);
				deadEnds.erase(connectedTo);
			}
			#ifndef NDEBUG // Thoroughly search the graph for SCC zones (which shouldn't exist)
				for(ucomp_t x = 0; x < zoneCountHoz; ++ x)
				for(ucomp_t y = 0; y < zoneCountVrt; ++ y) {
					auto p = Pos(x, y);
					assert(zoneGraph.countEdgesFrom(p) > 0);
					assert(zoneGraph.countEdgesTo  (p) > 0);
				}
			#endif
		}

		{ // Connect each zone as determined by the graph
			std::vector<Edge> srcEdges; srcEdges.reserve(std::min(w, h));
			std::vector<Pos>  dstPoints; dstPoints.reserve(std::min(w, h));
			zoneGraph.getAllEdges(&srcEdges);
			bool lastPathHadPoints = false;
			auto drawPath = [&](const Pos& src, const Pos& dst, bool verticalFirst) {
				bool doDrawPoints = rollProb(lastPathHadPoints? pointObjContProb : pointObjProb);
				lastPathHadPoints = doDrawPoints;
				drawDiagonal(src, dst, verticalFirst, doDrawPoints);
			};
			for(auto&& [ srcZone, midZone ] : srcEdges) {
				zoneGraph.findEdgesFrom(&dstPoints, midZone);
				for(auto&& dstZone : dstPoints) {
					//auto zoneRng = mkPosRng(midZone);
					auto mzRect = getZoneRect(midZone);
					auto xDist = std::uniform_int_distribution(mzRect.left, mzRect.right);
					auto yDist = std::uniform_int_distribution(mzRect.top, mzRect.bottom);
					//auto midpoint = Pos(xDist(zoneRng), yDist(zoneRng));
					auto entrance = randomPointBetweenZones(mzRect, midZone, srcZone);
					auto exit     = randomPointBetweenZones(mzRect, midZone, dstZone);
					bool verticalFirst = ! rollProb(diagonalCompBias);
					//drawPath(entrance, midpoint, verticalFirst);
					//drawPath(midpoint, exit,   ! verticalFirst);
					drawPath(entrance, exit, verticalFirst);
					#warning "TODO: fix the choice of waypoints being... not gay enough"
				}
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
