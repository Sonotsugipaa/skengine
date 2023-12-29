find_package(Freetype)

# Some `noexcept` functions throw errors, which is intentional and meant to
# opaquely work around some library functions that may fail - e.g. `FT_Done_Face`.
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
	add_compile_options(-Wno-terminate)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	add_compile_options(-Wno-terminate)
endif()

add_library(draw-geometry
	pipeline-init.cpp
	shape.cpp
	text.cpp )

target_link_libraries(draw-geometry
	shader-compiler
	idgen
	${FREETYPE_LIBRARIES} )

target_include_directories(draw-geometry PUBLIC ${FREETYPE_INCLUDE_DIRS})

# Enable unity builds
set_property(TARGET draw-geometry PROPERTY UNITY_BUILD true)
