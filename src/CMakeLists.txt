cmake_minimum_required(VERSION 3.24)
project("game-engine-sketch" VERSION 0.0.1)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wall -Wextra -Wpedantic -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-Wall -Wextra -Wpedantic -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	# add_compile_options(\\Wall) # How...?
else()
	message(WARNING "Toolchain \"${CMAKE_CXX_COMPILER_ID}\" not recognized by the project")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories("${PROJECT_SOURCE_DIR}/")

add_compile_definitions(SKENGINE_VERSION="${PROJECT_VERSION}")
add_compile_definitions("SKENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}")
add_compile_definitions("SKENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}")
add_compile_definitions("SKENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH}")

add_subdirectory(third-party)

add_subdirectory(engine)
add_subdirectory(gridlogic)
add_subdirectory(vkutil)

add_subdirectory(main)
