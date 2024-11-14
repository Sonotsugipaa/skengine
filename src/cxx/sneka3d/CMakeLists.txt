find_package(glm)

add_executable(sneka3d
	main.cpp )

target_link_libraries(sneka3d
	posixfio
	engine
	engine-util
	sflog fmt )
