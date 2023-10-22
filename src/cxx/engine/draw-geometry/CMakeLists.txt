add_library(draw-geometry
	pipeline-init.cpp )

target_link_libraries(draw-geometry
	shader-compiler )

# Enable unity builds
set_property(TARGET draw-geometry PROPERTY UNITY_BUILD true)
