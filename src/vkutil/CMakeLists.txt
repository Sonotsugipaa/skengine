find_package(Vulkan)

add_library(vkutil
	buffer.cpp
	command_pool.cpp
	desc_proxy.cpp )

target_link_libraries(vkutil
	posixfio
	rll-alloc
	vulkan vma )

# Enable unity builds
set_property(TARGET vkutil PROPERTY UNITY_BUILD true)
