find_package(glm)

add_executable(sneka3d
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
	posixfio
	engine
	engine-util
	input
	sflog fmt )
