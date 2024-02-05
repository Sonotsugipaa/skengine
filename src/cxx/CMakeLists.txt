if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-Wall -Wextra -Wpedantic -Wno-comment -fconcepts-diagnostics-depth=2)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	add_compile_options(/Wall)
else()
	message(WARNING "Toolchain \"${CMAKE_CXX_COMPILER_ID}\" not recognized by the project")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

add_compile_definitions(SKENGINE_VERSION="${PROJECT_VERSION}")
add_compile_definitions("SKENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}")
add_compile_definitions("SKENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}")
add_compile_definitions("SKENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH}")

add_subdirectory(third-party)
add_subdirectory(vendored-libraries)

add_subdirectory(engine)
add_subdirectory(engine-util)
add_subdirectory(gridlogic)
add_subdirectory(input)

add_subdirectory(main)
