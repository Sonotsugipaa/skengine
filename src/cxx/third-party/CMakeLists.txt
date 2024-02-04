add_library(vma STATIC vma/vma/impl.cpp)
target_include_directories(vma PUBLIC SYSTEM vma/)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	target_compile_options(vma PRIVATE -Wno-unused-variable -Wno-parentheses -Wno-implicit-fallthrough -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

add_subdirectory(tick-regulator)

add_subdirectory(nlohmann_json)

add_subdirectory(rll-allocator)

add_subdirectory(vk-util)
target_include_directories(vk-util PUBLIC SYSTEM ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(fmamdl)

add_subdirectory(id-generator/src)

add_library(ctpg INTERFACE)
target_include_directories(ctpg INTERFACE .)

#set(POSIXFIO_LOCAL true)
#add_subdirectory(posixfio)
#target_include_directories(posixfio INTERFACE posixfio/include/unix)
