set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)



if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# add_compile_options(\\Wall) # How...?
else()
	message(WARNING "Toolchain \"${CMAKE_CXX_COMPILER_ID}\" not supported by the project")
endif()



find_package(fmt)
find_package(posixfio)

add_library(sflog INTERFACE)
add_dependencies(sflog fmt)
target_include_directories(sflog INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_precompile_headers(sflog INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/sflog.hpp")

if(posixfio_FOUND)
	target_compile_definitions(sflog INTERFACE "SFLOG_ENABLE_POSIXFIO")
endif(posixfio_FOUND)

if(SFLOG_BUILD_TEST)
	add_executable(sflog_test test.cpp)
	target_link_libraries(sflog_test sflog posixfio fmt)
endif(SFLOG_BUILD_TEST)
