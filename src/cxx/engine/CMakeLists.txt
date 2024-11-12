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
	renderprocess/render_process_rpass.cpp
	renderprocess/render_target_storage.cpp
	renderprocess/render_process.cpp
	world_renderer_pipeline.cpp
	world_renderer.cpp
	ui_renderer.cpp
	object_storage.cpp
	shader_cache.cpp
	gui.cpp
	engine.cpp
	engine_buffer.cpp
	engine_asset_supplier.cpp
	engine_asset_supplier_material.cpp
	engine_asset_supplier_texture.cpp )

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
	sflog )

set_property(TARGET engine PROPERTY UNITY_BUILD false)
