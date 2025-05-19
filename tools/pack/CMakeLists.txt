cmake_minimum_required (VERSION 2.8)
project(pack)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

if(NOT WIN32)
  set(CMAKE_CXX_FLAGS -std=c++11)
  add_definitions(-D_GNU_SOURCE)
endif()

include_directories(
  "${CMAKE_SOURCE_DIR}/../../deps/brotli-1.0.9/c/include"
)

add_executable(pack main.cpp)

target_link_libraries(pack brotlienc-static)
