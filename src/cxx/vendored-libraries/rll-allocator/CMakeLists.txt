cmake_minimum_required(VERSION 3.24)
project("rll-alloc")

# Set warning levels
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wall -Wextra -Wpedantic -Winline)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-Wall -Wextra -Wpedantic -Winline)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# add_compile_options(\\Wall) # How...?
else()
	message(WARNING "Toolchain \"${CMAKE_CXX_COMPILER_ID}\" not recognized by the project")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(rll-alloc STATIC "rll-alloc/rll-alloc.cpp")
target_include_directories(rll-alloc INTERFACE .)
