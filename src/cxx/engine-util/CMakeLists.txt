find_package(posixfio)

add_library(engine-util
	basic_shader_cache.cpp
	basic_asset_source.cpp )

target_link_libraries(engine-util
	engine
	fmamdl )

# Enable unity builds
set_property(TARGET engine PROPERTY UNITY_BUILD false)
