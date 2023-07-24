#pragma once

#include <cstdint>



namespace grid {

	using direction_t   = int;
	using grid_coord_t  = uint_fast64_t;
	using plane_coord_t = double;


	class GeometryInterface {
	public:
		GeometryInterface(direction_t max_angle):
				geometry_mAngleValues(max_angle + 1)
		{ }

		virtual void geometry_translate(
			direction_t  direction,
			grid_coord_t coordinates[2]
		) noexcept = 0;

		virtual void geometry_gridToPlane(
			const grid_coord_t src[2],
			plane_coord_t      dst[2]
		) noexcept = 0;

	private:
		direction_t geometry_mAngleValues;
	};

}
