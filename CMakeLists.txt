cmake_minimum_required (VERSION 2.8)
project(pipy)

option(PIPY_GUI "include builtin GUI" OFF)

add_subdirectory(deps/yajl-2.1.0)

option(BUILD_shared "build a shared expat library" OFF)
add_subdirectory(deps/libexpat-R_2_2_6/expat)

add_subdirectory(deps/openssl-1.1.1g)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_CXX_FLAGS -std=c++11)

add_definitions(
  -D_GNU_SOURCE
  -DPIPY_HOST="${CMAKE_HOST_SYSTEM} ${CMAKE_HOST_SYSTEM_PROCESSOR}"
)

include_directories(
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/deps/asio-1.12.2/include"
  "${CMAKE_BINARY_DIR}/deps"
  "${CMAKE_BINARY_DIR}/deps/yajl-2.1.0/yajl-2.1.0/include"
  "${CMAKE_BINARY_DIR}/deps/libexpat-R_2_2_6/expat/lib"
  "${CMAKE_SOURCE_DIR}/deps/libexpat-R_2_2_6/expat/lib"
)

add_executable(pipy
  src/api/algo.cpp
  src/api/configuration.cpp
  src/api/console.cpp
  src/api/crypto.cpp
  src/api/hessian.cpp
  src/api/http.cpp
  src/api/json.cpp
  src/api/os.cpp
  src/api/url.cpp
  src/api/xml.cpp
  src/context.cpp
  src/data.cpp
  src/event.cpp
  src/filter.cpp
  src/filters/connect.cpp
  src/filters/decompress-body.cpp
  src/filters/demux.cpp
  src/filters/dubbo.cpp
  src/filters/dummy.cpp
  src/filters/dump.cpp
  src/filters/exec.cpp
  src/filters/fork.cpp
  src/filters/http.cpp
  src/filters/link.cpp
  src/filters/mux.cpp
  src/filters/on-body.cpp
  src/filters/on-event.cpp
  src/filters/on-message.cpp
  src/filters/on-start.cpp
  src/filters/print.cpp
  src/filters/replace-body.cpp
  src/filters/replace-event.cpp
  src/filters/replace-message.cpp
  src/filters/replace-start.cpp
  src/filters/tap.cpp
  src/filters/use.cpp
  src/filters/wait.cpp
  src/fstream.cpp
  src/graph.cpp
  src/gui.cpp
  src/inbound.cpp
  src/listener.cpp
  src/logging.cpp
  src/main.cpp
  src/message.cpp
  src/module.cpp
  src/net.cpp
  src/options.cpp
  src/outbound.cpp
  src/pipeline.cpp
  src/pjs/builtin.cpp
  src/pjs/expr.cpp
  src/pjs/parser.cpp
  src/pjs/types.cpp
  src/session.cpp
  src/tar.cpp
  src/task.cpp
  src/utils.cpp
  src/worker.cpp
)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/deps/version.h
  COMMAND ${CMAKE_SOURCE_DIR}/generate_version_h.sh
  ARGS ${CMAKE_BINARY_DIR}/deps/version.h
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_target(GenVer DEPENDS ${CMAKE_BINARY_DIR}/deps/version.h)
add_dependencies(pipy yajl_s expat crypto ssl GenVer)

if(PIPY_GUI)
  add_definitions(-DPIPY_USE_GUI)

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/deps/gui.tar.h
    COMMAND node
    ARGS pack.js ${CMAKE_BINARY_DIR}/deps/gui.tar.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/gui
    DEPENDS gui/pack.js ${CMAKE_SOURCE_DIR}/gui/public/index.html
  )

  add_custom_target(GenGui DEPENDS ${CMAKE_BINARY_DIR}/deps/gui.tar.h)
  add_dependencies(pipy GenGui)

endif()

target_link_libraries(pipy z yajl_s expat crypto ssl -pthread)
