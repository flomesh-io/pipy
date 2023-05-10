cmake_minimum_required (VERSION 2.8)
project(baseline)

set(CMAKE_CXX_FLAGS -std=c++11)

add_definitions(
  -D_GNU_SOURCE
)

include_directories(
  "${CMAKE_SOURCE_DIR}/../../../deps/asio-1.28.0/include"
)

add_executable(baseline
  main.cpp
)

target_link_libraries(baseline -pthread)
