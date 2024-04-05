find_package(spdlog)
find_package(posixfio)
find_package(glm)
find_package(Vulkan)
find_package(SDL2)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(shader-compiler)
add_subdirectory(draw-geometry)
add_subdirectory(ui-structure)
add_subdirectory(sys-resources)

add_library(engine
	init/device_sdl.cpp
	init/device.cpp
	init/rpass.cpp
	world_renderer.cpp
	gui.cpp
	engine.cpp
	engine_buffer.cpp
	engine_create_pipeline.cpp
	engine_asset_supplier.cpp
	engine_asset_supplier_material.cpp
	engine_asset_supplier_texture.cpp
	engine_shader_cache.cpp )

target_link_libraries(engine
	fmamdl
	idgen
	ui-structure
	posixfio
	tick-regulator
	vk-util
	SDL2::SDL2
	vulkan vma
	draw-geometry
	sys-resources
	spdlog::spdlog )

set_property(TARGET engine PROPERTY UNITY_BUILD true)
