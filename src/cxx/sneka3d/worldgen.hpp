#pragma once

#include <random>

#include <engine/types.hpp>

#include "basic_unordered_sets.hpp"
#include "world.hpp"



namespace sneka {

	Vec2<uint64_t> generateWorld(
		ske::Logger&,
		World& dst, BasicUset<Vec2<uint64_t>>* dstPtObjs,
		std::optional<Vec2<uint64_t>> origin,
		std::minstd_rand::result_type seed );

}
