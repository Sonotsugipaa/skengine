add_library(sys-resources sys-resources.cpp)

if(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
	target_compile_definitions(sys-resources PRIVATE "OS_LINUX" "OS=LINUX")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL Windows)
	target_compile_definitions(sys-resources PRIVATE "OS_WINDOWS" "OS=WINDOWS")
else()
	target_compile_definitions(sys-resources PRIVATE "OS_UNKNOWN" "OS=UNKNOWN")
endif()

target_include_directories(sys-resources PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
