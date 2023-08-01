cmake_minimum_required(VERSION 3.25)

project(udp2udp)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS " -O3 -march=native ")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

add_executable(udp2udp src/main.cpp src/UdpProxy.cpp src/Utils.cpp)
target_link_libraries(udp2udp uv)
