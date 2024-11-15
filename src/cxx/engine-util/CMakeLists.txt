find_package(posixfio)

add_library(engine-util
	basic_asset_source.cpp
	basic_render_process.cpp
	basic_shader_cache.cpp
	engine_asset_supplier.cpp
	engine_asset_supplier_material.cpp
	engine_asset_supplier_texture.cpp
	object_storage.cpp
	world_renderer_pipeline.cpp
	world_renderer.cpp
	ui_renderer.cpp
	gui.cpp )

target_link_libraries(engine-util
	engine
	fmamdl )

# Enable unity builds
set_property(TARGET engine-util PROPERTY UNITY_BUILD false)
