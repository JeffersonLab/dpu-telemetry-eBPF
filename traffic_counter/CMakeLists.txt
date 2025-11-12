cmake_minimum_required(VERSION 3.10)
project(tc_collector)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

add_executable(tc_collector tc_userspace.cpp)
target_link_libraries(tc_collector bpf pthread)
