find_package(glm)

add_library(config-parser config/config.cpp)
target_link_libraries(config-parser ctpg sflog)
target_compile_options(config-parser PUBLIC -Wno-abi)

add_executable(sneka3d
	worldgen.cpp
	world_v1.cpp
	main.cpp )

if(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
	target_compile_definitions(sneka3d PRIVATE "OS_LINUX" "OS=LINUX")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL Windows)
	target_compile_definitions(sneka3d PRIVATE "OS_WINDOWS" "OS=WINDOWS")
else()
	target_compile_definitions(sneka3d PRIVATE "OS_UNKNOWN" "OS=UNKNOWN")
endif()

target_link_libraries(sneka3d
	config-parser
	posixfio
	engine
	engine-util
	input
	sflog fmt )
