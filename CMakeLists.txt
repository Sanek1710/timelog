cmake_minimum_required(VERSION 3.0.0)
project(timelog VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(third-party)

add_executable(timelog main.cpp)
target_link_libraries(timelog pthread)

add_executable(debug-test debug_test.cpp)
target_link_libraries(debug-test pthread)



