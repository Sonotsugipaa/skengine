find_package(glslang)
find_package(SPIRV-Tools)
find_package(posixfio)
find_package(fmt)

add_library(shader-compiler
	shcmp.cpp )

target_link_libraries(shader-compiler
	vk-util
	fmt
	posixfio
	shaderc_combined SPIRV glslang SPIRV-Tools SPIRV-Tools-opt # shaderc + requirements
)

# Enable unity builds
set_property(TARGET shader-compiler PROPERTY UNITY_BUILD true)
