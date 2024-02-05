cmake_minimum_required(VERSION 3.27)
project("id-generator" VERSION 1.0.0)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# add_compile_options(\\Wall) # How...?
else()
	message(WARNING "Toolchain \"${CMAKE_CXX_COMPILER_ID}\" not supported by the project")
endif()


add_subdirectory(cxx)
