if(PIPY_STATIC)
  cmake_minimum_required(VERSION 3.13)
else()
  cmake_minimum_required(VERSION 3.10)
endif()

project(pipy)

option(PIPY_STATICLIB "Build Pipy as static library" OFF)
option(PIPY_SHARED "Build Pipy as dynamic library" OFF)
option(PIPY_GUI "include builtin GUI" OFF)
option(PIPY_CODEBASES "include builtin codebases in the executable" OFF)
option(PIPY_CUSTOM_CODEBASES "include custom codebases in the executable (<group>/<name>:<path>,<group>/<name>:<path>,...)" "")
option(PIPY_DEFAULT_OPTIONS "fixed command line options to insert before user options" OFF)
option(PIPY_BPF "enable eBPF support" ON)
option(PIPY_SOIL_FREED_SPACE "invalidate freed space for debugging" OFF)
option(PIPY_ASSERT_SAME_THREAD "enable assertions for strict inner-thread data access" OFF)
option(PIPY_ZLIB "external zlib location" "")
option(PIPY_OPENSSL "external libopenssl location" "")
option(PIPY_BROTLI "external brotli location" "")
option(PIPY_STATIC "statically link to libc" OFF)
option(PIPY_LTO "enable LTO" OFF)
option(PIPY_USE_NTLS, "Use externally compiled TongSuo Crypto library instead of OpenSSL. Used with PIPY_OPENSSL" OFF)
option(PIPY_USE_SYSTEM_ZLIB "Use system installed zlib" OFF)
option(PIPY_USE_SYSTEM_OPENSSL "Use system installed OpenSSL" OFF)
option(PIPY_USE_OPENSSL1, "Use openssl v1 when compiling pipy" OFF)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)

if(MSVC)
  add_compile_options(/W0
    $<$<CONFIG:>:/MT>
    $<$<CONFIG:Debug>:/MTd>
    $<$<CONFIG:Release>:/MT>
  )
endif(MSVC)

add_subdirectory(deps/yajl-2.1.0)
add_subdirectory(deps/libyaml-0.2.5)

option(BUILD_tools "build the xmlwf tool for expat library" OFF)
option(BUILD_examples "build the examples for expat library" OFF)
option(BUILD_tests "build the tests for expat library" OFF)
option(BUILD_shared "build a shared expat library" OFF)
option(BUILD_doc "build man page for xmlwf" OFF)
add_subdirectory(deps/libexpat-R_2_2_6/expat)

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
if(PIPY_LTO AND(NOT CMAKE_BUILD_TYPE STREQUAL "Debug"))
  message("Enabling LTO")
  include(link-time-optimization)
  link_time_optimization()
else()
  message("LTO is disabled")
endif()

if(WIN32)
  set(LIB_Z zlib.lib)
  set(LIB_CRYPTO libcrypto.lib)
  set(LIB_SSL libssl.lib)
  set(LIB_BROTLI libbrotlidec-static.lib)
  set(EXT_SHELL cmd)
else(WIN32)
  set(LIB_Z libz.a)
  set(LIB_CRYPTO libcrypto.a)
  set(LIB_SSL libssl.a)
  set(LIB_BROTLI libbrotlidec-static.a)
  set(EXT_SHELL sh)
endif(WIN32)

if(PIPY_ZLIB)
  set(ZLIB_INC_DIR ${PIPY_ZLIB}/include)
  set(ZLIB_LIB ${PIPY_ZLIB}/lib/${LIB_Z})
else()
  if(PIPY_USE_SYSTEM_ZLIB)
    SET(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_package(ZLIB REQUIRED)
    set(ZLIB_LIB -lz)
  else(PIPY_USE_SYSTEM_ZLIB)
    add_subdirectory(deps/zlib-1.3.1)
    set(ZLIB_INC_DIR "${CMAKE_SOURCE_DIR}/deps/zlib-1.3.1" "${CMAKE_BINARY_DIR}/deps/zlib-1.3.1")
    set(ZLIB_LIB zlibstatic)

    if(PIPY_SHARED AND UNIX AND NOT APPLE)
      # Ensure position-independent code for static zlib
      set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
  endif(PIPY_USE_SYSTEM_ZLIB)
endif()

if(PIPY_USE_OPENSSL1)
  add_definitions(-DPIPY_USE_OPENSSL1)
endif()

if(PIPY_USE_SYSTEM_OPENSSL)
  find_package(OpenSSL REQUIRED)
  if(OPENSSL_FOUND)
    set(OPENSSL_INC_DIR ${OPENSSL_INCLUDE_DIR})
    message(STATUS "Using system-installed OpenSSL")
  else()
    message(FATAL_ERROR "System OpenSSL not found. Set PIPY_USE_SYSTEM_OPENSSL to OFF or install OpenSSL.")
  endif()
elseif(PIPY_OPENSSL)
  if(PIPY_USE_NTLS)
    add_definitions(-DPIPY_USE_NTLS)
  endif(PIPY_USE_NTLS)

  set(OPENSSL_INC_DIR ${PIPY_OPENSSL}/include)
  set(OPENSSL_LIB_DIR ${PIPY_OPENSSL}/lib)

else()
  if (DEFINED PIPY_USE_OPENSSL1)
      message(FATAL_ERROR 
          "Error: OpenSSL v1 detected (PIPY_USE_OPENSSL1 is set).\n"
          "To compile with OpenSSL v1, you must set one of the following options:\n"
          "  - PIPY_OPENSSL: Provide a custom OpenSSL installation path.\n"
          "  - PIPY_USE_SYSTEM_OPENSSL: Use the system-installed OpenSSL library.\n"
          "Please configure CMake with the correct option and try again."
      )
  endif()

  set(OPENSSL_SRC_DIR ${CMAKE_SOURCE_DIR}/deps/openssl-3.2.0)
  set(OPENSSL_LIB_DIR ${OPENSSL_SRC_DIR}/build)
  set(OPENSSL_INC_DIR ${OPENSSL_SRC_DIR}/include ${OPENSSL_LIB_DIR}/include)

  file(MAKE_DIRECTORY "${OPENSSL_SRC_DIR}/build")

  if(WIN32)
    add_custom_command(
      OUTPUT ${OPENSSL_LIB_DIR}/${LIB_CRYPTO} ${OPENSSL_LIB_DIR}/${LIB_SSL}
      WORKING_DIRECTORY ${OPENSSL_SRC_DIR}/build
      COMMAND cmd ARGS /C set
      COMMAND perl ARGS ../Configure no-shared no-tests VC-WIN64A
      COMMAND nmake
      VERBATIM
      COMMENT "Building OpenSSL"
    )
  else()
    add_custom_command(
      OUTPUT ${OPENSSL_LIB_DIR}/${LIB_CRYPTO} ${OPENSSL_LIB_DIR}/${LIB_SSL}
      WORKING_DIRECTORY ${OPENSSL_SRC_DIR}
      COMMAND mkdir -p ${OPENSSL_SRC_DIR}/build
      COMMAND cd ${OPENSSL_SRC_DIR}/build && CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} ../config no-shared no-tests && make -j2
      VERBATIM
      COMMENT "Building OpenSSL"
    )
  endif()
endif()

if(NOT PIPY_USE_SYSTEM_OPENSSL)
  add_custom_target(OpenSSL DEPENDS ${OPENSSL_LIB_DIR}/${LIB_CRYPTO} ${OPENSSL_LIB_DIR}/${LIB_SSL})
endif()

if(PIPY_BROTLI)
  set(BROTLI_INC_DIR ${PIPY_BROTLI}/include)
  set(BROTLI_LIB ${PIPY_BROTLI}/lib/${LIB_BROTLI})
else()
  set(BROTLI_BUNDLED_MODE ON CACHE BOOL "" FORCE)
  set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
  add_subdirectory(deps/brotli-1.0.9)
  set(BROTLI_INC_DIR "${CMAKE_SOURCE_DIR}/deps/brotli-1.0.9/c/include")
  set(BROTLI_LIB brotlidec-static)
endif(PIPY_BROTLI)

add_definitions(
  -DPIPY_HOST="${CMAKE_HOST_SYSTEM} ${CMAKE_HOST_SYSTEM_PROCESSOR}"
  -DXML_STATIC=1
)

if(WIN32)
  add_definitions(
    -D_WIN32_WINNT=0x0601
    -D_USE_MATH_DEFINES
  )
else()
  add_definitions(-D_GNU_SOURCE)
endif()

if(NOT MSVC)
  add_compile_options(
    -Wall
    -Wno-overloaded-virtual
    -Wno-delete-non-virtual-dtor
    -Wno-sign-compare
    -Wno-deprecated-declarations
  )
endif()

include_directories(
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/deps/asio-1.28.0/include"
  "${CMAKE_BINARY_DIR}/deps"
  "${CMAKE_BINARY_DIR}/deps/yajl-2.1.0/yajl-2.1.0/include"
  "${CMAKE_BINARY_DIR}/deps/libyaml-0.2.5/include"
  "${CMAKE_BINARY_DIR}/deps/libexpat-R_2_2_6/expat/lib"
  "${CMAKE_SOURCE_DIR}/deps/libexpat-R_2_2_6/expat/lib"
  "${CMAKE_SOURCE_DIR}/deps/sqlite-3.43.2"
  "${OPENSSL_INC_DIR}"
  "${BROTLI_INC_DIR}"
)

if(NOT PIPY_USE_SYSTEM_ZLIB)
  include_directories("${ZLIB_INC_DIR}")
endif()

add_subdirectory(test/benchmark/baseline)

SET(PIPY_SRC
  src/api/algo.cpp
  src/api/bgp.cpp
  src/api/bpf.cpp
  src/api/console.cpp
  src/api/crypto.cpp
  src/api/c-struct.cpp
  src/api/dns.cpp
  src/api/hessian.cpp
  src/api/http.cpp
  src/api/ip.cpp
  src/api/json.cpp
  src/api/logging.cpp
  src/api/os.cpp
  src/api/pipeline-api.cpp
  src/api/pipy.cpp
  src/api/print.cpp
  src/api/protobuf.cpp
  src/api/resp.cpp
  src/api/stats.cpp
  src/api/sqlite.cpp
  src/api/swap.cpp
  src/api/thrift.cpp
  src/api/timeout.cpp
  src/api/url.cpp
  src/api/xml.cpp
  src/api/yaml.cpp
  src/api/zlib.cpp
  src/buffer.cpp
  src/codebase.cpp
  src/codebase-store.cpp
  src/compressor.cpp
  src/context.cpp
  src/data.cpp
  src/deframer.cpp
  src/elf.cpp
  src/event.cpp
  src/event-queue.cpp
  src/fetch.cpp
  src/file.cpp
  src/filter.cpp
  src/filters/bgp.cpp
  src/filters/compress.cpp
  src/filters/connect.cpp
  src/filters/decompress.cpp
  src/filters/deframe.cpp
  src/filters/demux.cpp
  src/filters/detect-protocol.cpp
  src/filters/dubbo.cpp
  src/filters/dummy.cpp
  src/filters/dump.cpp
  src/filters/exec.cpp
  src/filters/fcgi.cpp
  src/filters/fork.cpp
  src/filters/handle.cpp
  src/filters/http.cpp
  src/filters/http2.cpp
  src/filters/insert.cpp
  src/filters/loop.cpp
  src/filters/mime.cpp
  src/filters/mqtt.cpp
  src/filters/mux.cpp
  src/filters/netlink.cpp
  src/filters/on-body.cpp
  src/filters/on-event.cpp
  src/filters/on-message.cpp
  src/filters/on-start.cpp
  src/filters/pack.cpp
  src/filters/pipe.cpp
  src/filters/print.cpp
  src/filters/proxy-protocol.cpp
  src/filters/read.cpp
  src/filters/repeat.cpp
  src/filters/replace.cpp
  src/filters/replace-body.cpp
  src/filters/replace-event.cpp
  src/filters/replace-message.cpp
  src/filters/replace-start.cpp
  src/filters/replay.cpp
  src/filters/resp.cpp
  src/filters/socks.cpp
  src/filters/split.cpp
  src/filters/swap.cpp
  src/filters/tee.cpp
  src/filters/thrift.cpp
  src/filters/throttle.cpp
  src/filters/tls.cpp
  src/filters/wait.cpp
  src/filters/websocket.cpp
  src/fs.cpp
  src/fstream.cpp
  src/gui-tarball.cpp
  src/inbound.cpp
  src/input.cpp
  src/kmp.cpp
  src/listener.cpp
  src/log.cpp
  src/main.cpp
  src/main-options.cpp
  src/message.cpp
  src/net.cpp
  src/options.cpp
  src/os-platform.cpp
  src/outbound.cpp
  src/pipeline.cpp
  src/pjs/builtin.cpp
  src/pjs/expr.cpp
  src/pjs/module.cpp
  src/pjs/parser.cpp
  src/pjs/stmt.cpp
  src/pjs/tree.cpp
  src/pjs/types.cpp
  src/signal.cpp
  src/socket.cpp
  src/status.cpp
  src/store.cpp
  src/str-map.cpp
  src/table.cpp
  src/tar.cpp
  src/thread.cpp
  src/timer.cpp
  src/utils.cpp
  src/worker.cpp
  src/worker-thread.cpp
  deps/sqlite-3.43.2/sqlite3.c
)

if(IOS)
  add_definitions(-D__APPLE__ -DTARGET_OS_IOS)
endif(IOS)

if(PIPY_SHARED)
  add_definitions(-DPIPY_SHARED)
  add_library(pipy SHARED ${PIPY_SRC})
elseif(PIPY_STATICLIB)
  add_definitions(-DPIPY_SHARED)
  add_library(pipy STATIC ${PIPY_SRC})
else()
  add_executable(pipy ${PIPY_SRC})
endif()

execute_process(
  COMMAND ${CMAKE_SOURCE_DIR}/generate_version_h.${EXT_SHELL} ${CMAKE_BINARY_DIR}/deps/version.h
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_target(GenVer DEPENDS ${CMAKE_BINARY_DIR}/deps/version.h)

add_dependencies(pipy yajl_s expat ${BROTLI_LIB} GenVer)

if(NOT PIPY_USE_SYSTEM_OPENSSL)
  add_dependencies(pipy OpenSSL)
endif()

if(NOT PIPY_USE_SYSTEM_ZLIB)
  add_dependencies(pipy ${ZLIB_LIB})
endif()

find_program(NODEJS_EXECUTABLE NAMES node nodejs
  HINTS
  $ENV{NODE_DIR}
  PATH_SUFFIXES bin $ENV{HOMEBREW_PREFIX}
  DOC "Node.js interpreter")

if(NODEJS_EXECUTABLE)
  message("NodeJS found at: ${NODEJS_EXECUTABLE}")
endif()

if(PIPY_GUI)
  add_definitions(-DPIPY_USE_GUI)

  if(NOT NODEJS_EXECUTABLE)
    message(FATAL_ERROR "Nodejs installation not found.")
  endif()

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/deps/gui.tar.h
    COMMAND ${NODEJS_EXECUTABLE}
    ARGS pack-gui.js ${CMAKE_BINARY_DIR}/deps/gui.tar.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/gui
    DEPENDS gui/pack-gui.js ${CMAKE_SOURCE_DIR}/public/index.html
  )
  add_custom_target(PackGui DEPENDS ${CMAKE_BINARY_DIR}/deps/gui.tar.h)
  add_dependencies(pipy PackGui)
endif()

if(PIPY_CODEBASES)
  add_definitions(-DPIPY_USE_CODEBASES)

  if(NOT NODEJS_EXECUTABLE)
    message(FATAL_ERROR "Nodejs installation not found.")
  endif()

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/deps/codebases.tar.gz.h
    COMMAND ${NODEJS_EXECUTABLE}
    ARGS pack-codebases.js ${CMAKE_BINARY_DIR}/deps/codebases.tar.gz.h ${PIPY_CUSTOM_CODEBASES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/gui
    DEPENDS gui/pack-codebases.js ${CMAKE_SOURCE_DIR}/tutorial ${CMAKE_SOURCE_DIR}/samples
  )

  add_custom_target(PackCodebases DEPENDS ${CMAKE_BINARY_DIR}/deps/codebases.tar.gz.h)
  add_dependencies(pipy PackCodebases)
endif()

if(PIPY_DEFAULT_OPTIONS)
  add_definitions(-DPIPY_DEFAULT_OPTIONS="${PIPY_DEFAULT_OPTIONS}")
endif()

if(PIPY_BPF)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_VERSION VERSION_GREATER_EQUAL 4.18)
      add_definitions(-DPIPY_USE_BPF)
      message("eBPF is enabled")
    endif()
  endif()
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

if(PIPY_STATIC)
  target_link_options(pipy PRIVATE -static)
endif()

target_link_libraries(
  pipy
  yajl_s
  yaml
  expat
  ${ZLIB_LIB}
  ${BROTLI_LIB}
)

if(PIPY_USE_SYSTEM_OPENSSL)
  target_link_libraries(pipy ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
else()
  target_link_libraries(pipy ${OPENSSL_LIB_DIR}/${LIB_SSL} ${OPENSSL_LIB_DIR}/${LIB_CRYPTO})
endif()

if(WIN32)
  target_link_libraries(pipy crypt32 userenv)
elseif(ANDROID)
  target_link_libraries(pipy -pthread -ldl)
else()
  target_link_libraries(pipy -pthread -ldl -lutil)
endif()
