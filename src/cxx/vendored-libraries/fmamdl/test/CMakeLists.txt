enable_testing()

find_package(spdlog)

add_executable("test-layout" "test-layout.cpp")
target_link_libraries("test-layout"
	fmamdl
	spdlog::spdlog fmt )

add_executable("test-header" "test-header.cpp")
target_link_libraries("test-header"
	fmamdl
	spdlog::spdlog fmt )


add_test(
	NAME "Test Layout s1"
	COMMAND "test-layout" "s1" "s1"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )

add_test(
	NAME "Test Layout <empty string>"
	COMMAND "test-layout" "" "u1"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )

add_test(
	NAME "Test Layout f248u1s2"
	COMMAND "test-layout" "f248u1s2" "f2" "f4" "f8" "u1" "s2" "s2"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )

add_test(
	NAME "Test Layout 8-bit float failure"
	COMMAND "test-layout" "f048u1s2" "fail"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )

add_test(
	NAME "Test Layout double type failure"
	COMMAND "test-layout" "uf2" "fail"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )

add_test(
	NAME "Test Layout same type twice failure"
	COMMAND "test-layout" "f22f44" "fail"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )


add_test(
	NAME "Test Header read from memory"
	COMMAND "test-header"
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )
