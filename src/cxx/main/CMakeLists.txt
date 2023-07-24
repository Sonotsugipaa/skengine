find_package(spdlog)
find_package(glm)

add_executable(sketch-sim
	main.cpp )

target_link_libraries(sketch-sim
	posixfio
	engine
	nlohmann_json::nlohmann_json
	spdlog::spdlog )

# Enable unity builds
set_property(TARGET sketch-sim PROPERTY UNITY_BUILD true)
