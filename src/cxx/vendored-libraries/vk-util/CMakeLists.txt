find_package(Vulkan)

add_library(vk-util STATIC
	"dev-select.cpp"
	"device.cpp"
	"memory.cpp"
	"memory_bufferduplex.cpp"
	"error.cpp" )

target_link_libraries(vk-util
	posixfio
	rll-alloc
	Vulkan::Vulkan vma )

set_property(TARGET vk-util PROPERTY UNITY_BUILD true)
