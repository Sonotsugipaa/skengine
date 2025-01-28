find_package(posixfio)

add_library(engine-util
	basic_shader_cache.cpp
	basic_asset_cache.cpp
	basic_render_process.cpp
	engine_asset_supplier.cpp
	engine_asset_supplier_material.cpp
	engine_asset_supplier_texture.cpp
	object_storage_sorting.cpp
	object_storage.cpp
	world_renderer_pipeline.cpp
	world_renderer_prepare.cpp
	world_renderer.cpp
	ui_renderer.cpp
	gui.cpp )

target_link_libraries(engine-util
	engine shader-compiler
	fmamdl )

# Enable unity builds
set_property(TARGET engine-util PROPERTY UNITY_BUILD false)
