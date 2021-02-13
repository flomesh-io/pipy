cmake_minimum_required (VERSION 2.8)
project(pipy)

add_subdirectory(deps/yajl-2.1.0)

option(BUILD_shared "build a shared expat library" OFF)
add_subdirectory(deps/libexpat-R_2_2_6/expat)

add_subdirectory(deps/openssl-1.1.1g)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_CXX_FLAGS -std=c++11)

add_definitions(
  -D_GNU_SOURCE
  -DCONFIG_VERSION="2020-11-08"
  -DPIPY_HOST="${CMAKE_HOST_SYSTEM} ${CMAKE_HOST_SYSTEM_PROCESSOR}"
)

include_directories(
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/deps/asio-1.12.2/include"
  "${CMAKE_BINARY_DIR}/deps"
  "${CMAKE_BINARY_DIR}/deps/yajl-2.1.0/yajl-2.1.0/include"
  "${CMAKE_BINARY_DIR}/deps/libexpat-R_2_2_6/expat/lib"
  "${CMAKE_SOURCE_DIR}/deps/libexpat-R_2_2_6/expat/lib"
  "${CMAKE_SOURCE_DIR}/deps/quickjs-2020-11-08"
)

add_executable(pipy
  deps/quickjs-2020-11-08/cutils.c
  deps/quickjs-2020-11-08/libregexp.c
  deps/quickjs-2020-11-08/libunicode.c
  deps/quickjs-2020-11-08/quickjs.c
  src/config.cpp
  src/context.cpp
  src/crypto.cpp
  src/inbound.cpp
  src/js.cpp
  src/listener.cpp
  src/logging.cpp
  src/main.cpp
  src/match.cpp
  src/metrics.cpp
  src/module.cpp
  src/modules/clone.cpp
  src/modules/counter.cpp
  src/modules/dubbo.cpp
  src/modules/dummy.cpp
  src/modules/erase.cpp
  src/modules/filter.cpp
  src/modules/hello.cpp
  src/modules/hessian2.cpp
  src/modules/http.cpp
  src/modules/insert.cpp
  src/modules/json.cpp
  src/modules/message.cpp
  src/modules/print.cpp
  src/modules/prometheus.cpp
  src/modules/proxy.cpp
  src/modules/queue.cpp
  src/modules/script.cpp
  src/modules/serve-static.cpp
  src/modules/tap.cpp
  src/modules/timestamp.cpp
  src/modules/xml.cpp
  src/options.cpp
  src/outbound.cpp
  src/pipeline.cpp
  src/session.cpp
  src/utils.cpp
)

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/deps/version.h
  COMMAND ${CMAKE_SOURCE_DIR}/generate_version_h.sh
  ARGS ${CMAKE_BINARY_DIR}/deps/version.h
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_target(GenVerH DEPENDS ${CMAKE_BINARY_DIR}/deps/version.h)

add_dependencies(pipy yajl_s expat crypto ssl GenVerH)

target_link_libraries(pipy yajl_s expat crypto ssl -pthread)
