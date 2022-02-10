cmake_minimum_required (VERSION 3.10)
project(pipy)

option(PIPY_GUI "include builtin GUI" ON)
option(PIPY_TUTORIAL "include builtin tutorial scripts" ON)
option(PIPY_SOIL_FREED_SPACE "invalidate freed space for debugging" OFF)
option(PIPY_ZLIB "external zlib location" "")
option(PIPY_OPENSSL "external libopenssl location" "")
option(PIPY_STATIC "statically link to libc" OFF)

add_subdirectory(deps/yajl-2.1.0)

option(BUILD_tools "build the xmlwf tool for expat library" OFF)
option(BUILD_examples "build the examples for expat library" OFF)
option(BUILD_tests "build the tests for expat library" OFF)
option(BUILD_shared "build a shared expat library" OFF)
option(BUILD_doc "build man page for xmlwf" OFF)
add_subdirectory(deps/libexpat-R_2_2_6/expat)

option(LEVELDB_BUILD_TESTS "Build LevelDB's unit tests" OFF)
option(LEVELDB_BUILD_BENCHMARKS "Build LevelDB's benchmarks" OFF)
option(LEVELDB_INSTALL "Install LevelDB's header and library" OFF)
add_subdirectory(deps/leveldb-1.23)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Default build type is Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

# Ignore COMPILE_DEFINITIONS_<Config> properties
cmake_policy(SET CMP0043 NEW)

# Link libraries by full path even in implicit directories.
cmake_policy(SET CMP0060 NEW)

# ``INTERPROCEDURAL_OPTIMIZATION`` is enforced when enabled.
cmake_policy(SET CMP0069 NEW)

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
mark_as_advanced(FORCE CCACHE_PROGRAM)

if(CCACHE_PROGRAM)
   set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

include(c++-standards)
include(compiler-options)
include(link-time-optimization)
include(sanitizers)

# Require C++ 11
cxx_11()

# enable LTO/IPO for all targets. Doesn't work with GCC.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  link_time_optimization()
endif()

set(ZLIB_SRC_DIR ${CMAKE_SOURCE_DIR}/deps/zlib-1.2.11)
set(ZLIB_INC_DIR ${ZLIB_SRC_DIR})
set(ZLIB_LIB_DIR ${ZLIB_SRC_DIR}/build)

if (PIPY_ZLIB)
  set(ZLIB_INC_DIR ${PIPY_ZLIB}/include)
  set(ZLIB_LIB_DIR ${PIPY_ZLIB}/lib)
else()
  add_custom_command(
    OUTPUT ${ZLIB_LIB_DIR}/libz.a
    WORKING_DIRECTORY ${ZLIB_SRC_DIR}
    COMMAND mkdir -p build
    COMMAND cd build && ../configure && make
    COMMENT "Building zlib"
  )
endif()

add_custom_target(zlib DEPENDS ${ZLIB_LIB_DIR}/libz.a)

if(PIPY_OPENSSL)
  set(OPENSSL_INC_DIR ${PIPY_OPENSSL}/include)
  set(OPENSSL_LIB_DIR ${PIPY_OPENSSL}/lib)
else()
  set(OPENSSL_SRC_DIR ${CMAKE_SOURCE_DIR}/deps/openssl-1.1.1m)
  set(OPENSSL_LIB_DIR ${OPENSSL_SRC_DIR}/build)
  set(OPENSSL_INC_DIR ${OPENSSL_SRC_DIR}/include ${OPENSSL_LIB_DIR}/include)

  add_custom_command(
    OUTPUT ${OPENSSL_LIB_DIR}/libcrypto.a ${OPENSSL_LIB_DIR}/libssl.a
    WORKING_DIRECTORY ${OPENSSL_SRC_DIR}
    COMMAND mkdir -p build
    COMMAND cd build && ../config no-shared && make
    COMMENT "Building OpenSSL"
  )
endif()

add_custom_target(OpenSSL DEPENDS ${OPENSSL_LIB_DIR}/libcrypto.a ${OPENSSL_LIB_DIR}/libssl.a)

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
  "${CMAKE_SOURCE_DIR}/deps/leveldb-1.23/include"
  "${ZLIB_INC_DIR}"
  "${OPENSSL_INC_DIR}"
)

add_executable(pipy
  src/admin-proxy.cpp
  src/admin-service.cpp
  src/api/algo.cpp
  src/api/configuration.cpp
  src/api/console.cpp
  src/api/crypto.cpp
  src/api/hessian.cpp
  src/api/http.cpp
  src/api/json.cpp
  src/api/mqtt.cpp
  src/api/netmask.cpp
  src/api/os.cpp
  src/api/pipy.cpp
  src/api/stats.cpp
  src/api/url.cpp
  src/api/xml.cpp
  src/codebase.cpp
  src/codebase-store.cpp
  src/compress.cpp
  src/context.cpp
  src/data.cpp
  src/event.cpp
  src/fetch.cpp
  src/filter.cpp
  src/filters/connect.cpp
  src/filters/decompress-message.cpp
  src/filters/demux.cpp
  src/filters/detect-protocol.cpp
  src/filters/dubbo.cpp
  src/filters/dummy.cpp
  src/filters/dump.cpp
  src/filters/exec.cpp
  src/filters/fork.cpp
  src/filters/http.cpp
  src/filters/http2.cpp
  src/filters/link.cpp
  src/filters/merge.cpp
  src/filters/mqtt.cpp
  src/filters/mux.cpp
  src/filters/on-body.cpp
  src/filters/on-event.cpp
  src/filters/on-message.cpp
  src/filters/on-start.cpp
  src/filters/pack.cpp
  src/filters/print.cpp
  src/filters/replace-body.cpp
  src/filters/replace-event.cpp
  src/filters/replace-message.cpp
  src/filters/replace-start.cpp
  src/filters/socks.cpp
  src/filters/split.cpp
  src/filters/throttle.cpp
  src/filters/tls.cpp
  src/filters/use.cpp
  src/filters/wait.cpp
  src/fs.cpp
  src/fstream.cpp
  src/graph.cpp
  src/gui-tarball.cpp
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
  src/status.cpp
  src/store.cpp
  src/tar.cpp
  src/task.cpp
  src/timer.cpp
  src/utils.cpp
  src/worker.cpp
)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/deps/version.h
  COMMAND ${CMAKE_SOURCE_DIR}/generate_version_h.sh
  ARGS ${CMAKE_BINARY_DIR}/deps/version.h
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_target(GenVer DEPENDS ${CMAKE_BINARY_DIR}/deps/version.h)

add_dependencies(pipy yajl_s expat zlib OpenSSL GenVer)

if(PIPY_GUI)
  add_definitions(-DPIPY_USE_GUI)

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/deps/gui.tar.h
    COMMAND node
    ARGS pack-gui.js ${CMAKE_BINARY_DIR}/deps/gui.tar.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/gui
    DEPENDS gui/pack-gui.js ${CMAKE_SOURCE_DIR}/public/index.html
  )

  add_custom_target(PackGui DEPENDS ${CMAKE_BINARY_DIR}/deps/gui.tar.h)
  add_dependencies(pipy PackGui)
endif()

if(PIPY_TUTORIAL)
  add_definitions(-DPIPY_USE_TUTORIAL)

  file(GLOB_RECURSE TUTORIAL_FILES LIST_DIRECTORIES false ${CMAKE_SOURCE_DIR}/tutorial/*)

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/deps/tutorial.tar.gz.h
    COMMAND node
    ARGS pack-tutorial.js ${CMAKE_BINARY_DIR}/deps/tutorial.tar.gz.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/gui
    DEPENDS gui/pack-tutorial.js ${TUTORIAL_FILES}
  )

  add_custom_target(PackTutorial DEPENDS ${CMAKE_BINARY_DIR}/deps/tutorial.tar.gz.h)
  add_dependencies(pipy PackTutorial)
endif()

if(PIPY_SOIL_FREED_SPACE)
  add_definitions(-DPIPY_SOIL_FREED_SPACE)
endif()

if(PIPY_STATIC)
  target_link_options(pipy PRIVATE -static)
endif()

target_link_libraries(
  pipy
  yajl_s
  expat
  ${ZLIB_LIB_DIR}/libz.a
  ${OPENSSL_LIB_DIR}/libssl.a
  ${OPENSSL_LIB_DIR}/libcrypto.a
  leveldb
  -pthread
  -ldl
)
