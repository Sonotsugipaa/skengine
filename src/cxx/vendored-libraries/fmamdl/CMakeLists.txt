cmake_minimum_required(VERSION 3.26)
project("Fast Mappable Model")

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# add_compile_options(\\Wall) # How...?
else()
	message(WARNING "Toolchain \"${CMAKE_CXX_COMPILER_ID}\" not recognized by the project")
endif()

set(CMAKE_CXX_STANDARD 23)


find_package(posixfio)

add_library(fmamdl STATIC
	"impl/header.cpp"
	"impl/layout.cpp"
	"impl/material.cpp"
	"impl/string.cpp" )

target_include_directories(fmamdl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

set_property(TARGET fmamdl PROPERTY UNITY_BUILD true)


if(posixfio_FOUND)

	add_subdirectory(conv)

	if(FMAMDL_ENABLE_TESTS)
		enable_testing()
		add_subdirectory(test)
	endif(FMAMDL_ENABLE_TESTS)

else()

	message(WARNING "The converter requires the `posixfio` library, which has not been found.")

	if(FMAMDL_ENABLE_TESTS)
		message(WARNING "Unit testing requires the `posixfio` library, which has not been found.")
	endif(FMAMDL_ENABLE_TESTS)

endif(posixfio_FOUND)
