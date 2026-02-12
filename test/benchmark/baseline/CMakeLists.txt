cmake_minimum_required (VERSION 3.14)
project(baseline)

if(NOT WIN32)
  set(CMAKE_CXX_FLAGS -std=c++11)
  add_definitions(-D_GNU_SOURCE)
endif()

include_directories(
  "${asio_SOURCE_DIR}/asio/include"
)

add_executable(baseline
  main.cpp
)

target_link_libraries(baseline -pthread)
