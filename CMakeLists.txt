if(PIPY_STATIC)
  cmake_minimum_required (VERSION 3.13)
else()
  cmake_minimum_required (VERSION 3.10)
endif()

project(pipy)

option(PIPY_GUI "include builtin GUI" ON)
option(PIPY_SAMPLES "include builtin sample scripts" ON)
option(PIPY_SOIL_FREED_SPACE "invalidate freed space for debugging" OFF)
option(PIPY_ASSERT_SAME_THREAD "enable assertions for strict inner-thread data access" OFF)
option(PIPY_ZLIB "external zlib location" "")
option(PIPY_OPENSSL "external libopenssl location" "")
option(PIPY_BROTLI "external brotli location" "")
option(PIPY_STATIC "statically link to libc" OFF)
option(PIPY_LTO "enable LTO" ON)
option(PIPY_RFC8998 "enable support for RFC 9889: SM Cipher Suite for TLS" OFF)

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
include(sanitizers)

# Require C++ 11
cxx_11()

# enable LTO/IPO for all targets. Doesn't work with GCC.
if(PIPY_LTO AND (NOT CMAKE_BUILD_TYPE STREQUAL "Debug"))
  message("Enabling LTO")
  include(link-time-optimization)
  link_time_optimization()
else()
  message("LTO is disabled")
endif()

if (PIPY_ZLIB)
  set(ZLIB_INC_DIR ${PIPY_ZLIB}/include)
  set(ZLIB_LIB ${PIPY_ZLIB}/lib/libz.a)
else()
  add_subdirectory(deps/zlib-1.2.11)
  set(ZLIB_INC_DIR "${CMAKE_SOURCE_DIR}/deps/zlib-1.2.11" "${CMAKE_BINARY_DIR}/deps/zlib-1.2.11")
  set(ZLIB_LIB zlibstatic)
endif()

if(PIPY_OPENSSL)
  set(OPENSSL_INC_DIR ${PIPY_OPENSSL}/include)
  set(OPENSSL_LIB_DIR ${PIPY_OPENSSL}/lib)
else()
  set(OPENSSL_SRC_DIR ${CMAKE_SOURCE_DIR}/deps/openssl-1.1.1q)
  set(OPENSSL_LIB_DIR ${OPENSSL_SRC_DIR}/build)
  set(OPENSSL_INC_DIR ${OPENSSL_SRC_DIR}/include ${OPENSSL_LIB_DIR}/include)

  add_custom_command(
    OUTPUT ${OPENSSL_LIB_DIR}/libcrypto.a ${OPENSSL_LIB_DIR}/libssl.a
    WORKING_DIRECTORY ${OPENSSL_SRC_DIR}
    COMMAND mkdir -p ${OPENSSL_SRC_DIR}/build
    COMMAND cd ${OPENSSL_SRC_DIR}/build && ../config no-shared no-tests && make
    VERBATIM
    COMMENT "Building OpenSSL"
  )
endif()

add_custom_target(OpenSSL DEPENDS ${OPENSSL_LIB_DIR}/libcrypto.a ${OPENSSL_LIB_DIR}/libssl.a)

if(PIPY_BROTLI)
  set(BROTLI_INC_DIR ${PIPY_BROTLI}/include)
  set(BROTLI_LIB ${PIPY_BROTLI}/lib/libbrotlidec-static.a)
else()
  set(BROTLI_BUNDLED_MODE OFF CACHE BOOL "" FORCE)
  set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
  add_subdirectory(deps/brotli-1.0.9)
  set(BROTLI_INC_DIR "${CMAKE_SOURCE_DIR}/deps/brotli-1.0.9/c/include")
  set(BROTLI_LIB brotlidec-static)
endif(PIPY_BROTLI)

add_definitions(
  -D_GNU_SOURCE
  -DPIPY_HOST="${CMAKE_HOST_SYSTEM} ${CMAKE_HOST_SYSTEM_PROCESSOR}"
)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-Wdelete-non-abstract-non-virtual-dtor"
  COMPILER_SUPPORTS_DELETE_NON_ABSTRACT_NON_VIRTUAL_DTOR)
if(COMPILER_SUPPORTS_DELETE_NON_ABSTRACT_NON_VIRTUAL_DTOR)
  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-delete-non-abstract-non-virtual-dtor>)
endif()
add_compile_options(
  -Wall
  -Wno-overloaded-virtual
  -Wno-delete-non-virtual-dtor
  -Wno-sign-compare
  -Wno-deprecated-declarations
)

include_directories(
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/deps/asio-1.24.0/include"
  "${CMAKE_BINARY_DIR}/deps"
  "${CMAKE_BINARY_DIR}/deps/yajl-2.1.0/yajl-2.1.0/include"
  "${CMAKE_BINARY_DIR}/deps/libexpat-R_2_2_6/expat/lib"
  "${CMAKE_SOURCE_DIR}/deps/libexpat-R_2_2_6/expat/lib"
  "${CMAKE_SOURCE_DIR}/deps/leveldb-1.23/include"
  "${ZLIB_INC_DIR}"
  "${OPENSSL_INC_DIR}"
  "${BROTLI_INC_DIR}"
)

add_subdirectory(test/benchmark/baseline)

add_executable(pipy
  src/admin-link.cpp
  src/admin-proxy.cpp
  src/admin-service.cpp
  src/api/algo.cpp
  src/api/bgp.cpp
  src/api/configuration.cpp
  src/api/console.cpp
  src/api/crypto.cpp
  src/api/dns.cpp
  src/api/hessian.cpp
  src/api/http.cpp
  src/api/json.cpp
  src/api/logging.cpp
  src/api/netmask.cpp
  src/api/os.cpp
  src/api/pipy.cpp
  src/api/protobuf.cpp
  src/api/resp.cpp
  src/api/stats.cpp
  src/api/thrift.cpp
  src/api/timeout.cpp
  src/api/url.cpp
  src/api/xml.cpp
  src/buffer.cpp
  src/codebase.cpp
  src/codebase-store.cpp
  src/compress.cpp
  src/context.cpp
  src/data.cpp
  src/deframer.cpp
  src/event.cpp
  src/fetch.cpp
  src/file.cpp
  src/filter.cpp
  src/filters/bgp.cpp
  src/filters/branch.cpp
  src/filters/chain.cpp
  src/filters/compress-message.cpp
  src/filters/connect.cpp
  src/filters/decompress-message.cpp
  src/filters/deframe.cpp
  src/filters/demux.cpp
  src/filters/deposit-message.cpp
  src/filters/detect-protocol.cpp
  src/filters/dubbo.cpp
  src/filters/dummy.cpp
  src/filters/dump.cpp
  src/filters/exec.cpp
  src/filters/fork.cpp
  src/filters/handle.cpp
  src/filters/http.cpp
  src/filters/http2.cpp
  src/filters/link.cpp
  src/filters/link-input.cpp
  src/filters/link-output.cpp
  src/filters/mime.cpp
  src/filters/mqtt.cpp
  src/filters/mux.cpp
  src/filters/on-body.cpp
  src/filters/on-event.cpp
  src/filters/on-message.cpp
  src/filters/on-start.cpp
  src/filters/pack.cpp
  src/filters/print.cpp
  src/filters/proxy-protocol.cpp
  src/filters/read.cpp
  src/filters/replace.cpp
  src/filters/replace-body.cpp
  src/filters/replace-event.cpp
  src/filters/replace-message.cpp
  src/filters/replace-start.cpp
  src/filters/replay.cpp
  src/filters/resp.cpp
  src/filters/socks.cpp
  src/filters/split.cpp
  src/filters/tee.cpp
  src/filters/thrift.cpp
  src/filters/throttle.cpp
  src/filters/tls.cpp
  src/filters/use.cpp
  src/filters/wait.cpp
  src/filters/websocket.cpp
  src/fs.cpp
  src/fstream.cpp
  src/graph.cpp
  src/gui-tarball.cpp
  src/inbound.cpp
  src/input.cpp
  src/kmp.cpp
  src/listener.cpp
  src/log.cpp
  src/main.cpp
  src/main-options.cpp
  src/message.cpp
  src/module.cpp
  src/net.cpp
  src/nmi.cpp
  src/options.cpp
  src/outbound.cpp
  src/output.cpp
  src/pipeline.cpp
  src/pjs/builtin.cpp
  src/pjs/expr.cpp
  src/pjs/parser.cpp
  src/pjs/types.cpp
  src/status.cpp
  src/store.cpp
  src/str-map.cpp
  src/table.cpp
  src/tar.cpp
  src/task.cpp
  src/thread.cpp
  src/timer.cpp
  src/utils.cpp
  src/watch.cpp
  src/worker.cpp
  src/worker-thread.cpp
)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/deps/version.h
  COMMAND ${CMAKE_SOURCE_DIR}/generate_version_h.sh
  ARGS ${CMAKE_BINARY_DIR}/deps/version.h
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_custom_target(GenVer DEPENDS ${CMAKE_BINARY_DIR}/deps/version.h)

add_dependencies(pipy yajl_s expat ${ZLIB_LIB} OpenSSL ${BROTLI_LIB} GenVer)

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

if(PIPY_SAMPLES)
  add_definitions(-DPIPY_USE_SAMPLES)

  file(GLOB_RECURSE SAMPLE_FILES LIST_DIRECTORIES false
    ${CMAKE_SOURCE_DIR}/tutorial/*
    ${CMAKE_SOURCE_DIR}/samples/*
  )

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/deps/samples.tar.gz.h
    COMMAND node
    ARGS pack-samples.js ${CMAKE_BINARY_DIR}/deps/samples.tar.gz.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/gui
    DEPENDS gui/pack-samples.js ${SAMPLE_FILES}
  )

  add_custom_target(PackSamples DEPENDS ${CMAKE_BINARY_DIR}/deps/samples.tar.gz.h)
  add_dependencies(pipy PackSamples)
endif()

if(PIPY_SOIL_FREED_SPACE)
  add_definitions(-DPIPY_SOIL_FREED_SPACE)
endif()

if(PIPY_ASSERT_SAME_THREAD)
  add_definitions(-DPIPY_ASSERT_SAME_THREAD)
endif()

if(PIPY_RFC8998)
  add_definitions(-DPIPY_USE_RFC8998)
endif(PIPY_RFC8998)

if(APPLE)
  set(CMAKE_EXE_LINKER_FLAGS "-Wl,-exported_symbols_list ${CMAKE_SOURCE_DIR}/include/pipy/nmi-exports.lst")
else()
  set(CMAKE_EXE_LINKER_FLAGS "-Wl,--dynamic-list ${CMAKE_SOURCE_DIR}/include/pipy/nmi-exports.txt")
endif()

if(PIPY_STATIC)
  target_link_options(pipy PRIVATE -static)
endif()

target_link_libraries(
  pipy
  yajl_s
  expat
  ${ZLIB_LIB}
  ${OPENSSL_LIB_DIR}/libssl.a
  ${OPENSSL_LIB_DIR}/libcrypto.a
  ${BROTLI_LIB}
  leveldb
  -pthread
  -ldl
)
