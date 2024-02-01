cmake_minimum_required (VERSION 2.8)
project(pjs)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_CXX_FLAGS -std=c++11)

add_executable(pjs
  builtin.cpp
  expr.cpp
  main.cpp
  parser.cpp
  stmt.cpp
  types.cpp
)
