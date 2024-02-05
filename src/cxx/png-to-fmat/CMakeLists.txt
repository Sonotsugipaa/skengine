find_package(posixfio)

add_executable(fmat main.cpp)
target_link_libraries(fmat posixfio)
