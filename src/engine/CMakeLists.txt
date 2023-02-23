find_package(spdlog)
find_package(posixfio)
find_package(glm)
find_package(Vulkan)
find_package(SDL2)

add_library(engine
	init/device.cpp
	init/rpass.cpp
	engine.cpp
	engine_shader_cache.cpp )

target_link_libraries(engine
	posixfio
	tick-regulator
	game-asset-package
	vkutil
	SDL2::SDL2
	vulkan vma
	nlohmann_json::nlohmann_json
	spdlog::spdlog )

# Enable unity builds
set_property(TARGET engine PROPERTY UNITY_BUILD true)
