add_subdirectory(fmamdl)

add_subdirectory(id-generator/src)

add_subdirectory(tick-regulator)

add_subdirectory(rll-allocator)

add_subdirectory(sflog/src)

add_subdirectory(vk-util)
target_include_directories(vk-util PUBLIC SYSTEM ${CMAKE_CURRENT_SOURCE_DIR})
