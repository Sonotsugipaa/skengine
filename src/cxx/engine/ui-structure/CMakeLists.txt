find_package(glm)

add_library(ui-structure ui.cpp)
target_link_libraries(ui-structure
	idgen )

# Enable unity builds
set_property(TARGET ui-structure PROPERTY UNITY_BUILD true)
