find_package(spdlog)

add_subdirectory("${PROJECT_SOURCE_DIR}/submodules/rapidobj" "${PROJECT_BINARY_DIR}/rapidobj")

add_executable("fmaconv" "main.cpp")
configure_file("sample.obj" "sample.obj")
configure_file("sample.mtl" "sample.mtl")
target_link_libraries("fmaconv"
	fmamdl
	posixfio
	rapidobj::rapidobj
	spdlog::spdlog fmt )
