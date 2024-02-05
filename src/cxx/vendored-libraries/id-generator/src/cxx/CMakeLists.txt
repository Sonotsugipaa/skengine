set(CMAKE_CXX_STANDARD 23)


add_library(idgen INTERFACE)
target_include_directories(idgen INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")


if(IDGEN_ENABLE_TESTS)

	find_package(posixfio)

	if(posixfio_FOUND)
		add_executable(test test.cpp)
		target_link_libraries(test idgen posixfio fmt)
		enable_testing()
		add_test(
			NAME "test"
			COMMAND "test"
			WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )
	else()
		message(WARNING "Unit testing requires the `posixfio` library, which has not been found.")
	endif(posixfio_FOUND)

endif(IDGEN_ENABLE_TESTS)
