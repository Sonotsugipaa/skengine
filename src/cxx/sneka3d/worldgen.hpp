#pragma once

#include <random>

#include <engine/types.hpp>

#include "world.hpp"



namespace sneka {

	Vec2<int64_t> generateWorld(
		ske::Logger&,
		World& dst,
		std::optional<Vec2<int64_t>> origin,
		std::minstd_rand::result_type seed );

}
