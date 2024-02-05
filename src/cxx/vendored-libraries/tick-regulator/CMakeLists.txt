# Declare the project

cmake_minimum_required(VERSION 3.24)
project("tick-regulator")

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

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(spdlog)

add_library(tick-regulator STATIC "tick-regulator.cpp")
target_include_directories(tick-regulator INTERFACE .)

add_executable(tick-regulator-test EXCLUDE_FROM_ALL "test/tick-regulator-test.cpp")
target_link_libraries(tick-regulator-test tick-regulator spdlog::spdlog)
