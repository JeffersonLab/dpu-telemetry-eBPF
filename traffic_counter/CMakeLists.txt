cmake_minimum_required(VERSION 3.10)
project(tc_egress_collector)

set(CMAKE_CXX_STANDARD 17)

include_directories(include)

add_executable(tc_egress_collector tc_egress_collector.cpp)
target_link_libraries(tc_egress_collector bpf pthread)
