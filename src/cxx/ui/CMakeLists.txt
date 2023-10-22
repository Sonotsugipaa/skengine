find_package(glm)

add_library(ui ui.cpp)
target_link_libraries(ui
	idgen )

# Enable unity builds
set_property(TARGET ui PROPERTY UNITY_BUILD true)
