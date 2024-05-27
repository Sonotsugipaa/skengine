find_package(spdlog)
find_package(glm)

add_library(config-parser config/config.cpp)
target_link_libraries(config-parser ctpg)
target_compile_options(config-parser PUBLIC -Wno-abi)

add_executable(sketch-sim
	main.cpp )

target_link_libraries(sketch-sim
	posixfio
	engine
	engine-util
	config-parser
	spdlog::spdlog )

# Enable unity builds
set_property(TARGET sketch-sim PROPERTY UNITY_BUILD true)
