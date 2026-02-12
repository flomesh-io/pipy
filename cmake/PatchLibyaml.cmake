# Patch libyaml CMakeLists.txt for CMake 4.x compatibility
# libyaml 0.2.5 uses cmake_minimum_required(VERSION 3.0) which is rejected by CMake 4.x
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" content)
string(REPLACE
  "cmake_minimum_required(VERSION 3.0)"
  "cmake_minimum_required(VERSION 3.5)"
  content "${content}"
)
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt" "${content}")
