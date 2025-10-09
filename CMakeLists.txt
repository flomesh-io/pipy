if(PIPY_STATIC)
  cmake_minimum_required(VERSION 3.13)
else()
  cmake_minimum_required(VERSION 3.10)
endif()

project(pipy)

option(PIPY_STATICLIB "Build Pipy as static library" OFF)
option(PIPY_SHARED "Build Pipy as dynamic library" OFF)
option(PIPY_GUI "include builtin GUI" OFF)
option(PIPY_SAMPLE_CODEBASES "include sample codebases in the executable" ON)
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
  set(EXT_SHELL cmd)
  set(EXE_PACK "Release\\pack.exe")
else(WIN32)
  set(LIB_Z libz.a)
  set(EXT_SHELL sh)
  set(EXE_PACK pack)
endif(WIN32)

set(LIB_BROTLI libbrotlidec-static${CMAKE_STATIC_LIBRARY_SUFFIX})

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

  set(OPENSSL_SRC_DIR ${CMAKE_SOURCE_DIR}/deps/openssl-3.5.2)
  set(OPENSSL_LIB_DIR ${OPENSSL_SRC_DIR}/build)
  set(OPENSSL_INC_DIR ${OPENSSL_SRC_DIR}/include ${OPENSSL_LIB_DIR}/include)

  # Create the OpenSSL build/include directory early to avoid CMake configuration errors
  # This is needed because CMake validates INTERFACE_INCLUDE_DIRECTORIES paths during configuration,
  # but ExternalProject creates these directories only during build time
  file(MAKE_DIRECTORY ${OPENSSL_LIB_DIR}/include)

  include(ExternalProject)

  if(WIN32)
    set(configure_command perl
      <SOURCE_DIR>/Configure no-shared no-tests VC-WIN64A)
    set(make_command nmake)
  else()
    set(configure_command ${CMAKE_COMMAND} -E env
      CC=${CMAKE_C_COMPILER}
      CXX=${CMAKE_CXX_COMPILER}
      AR=${CMAKE_AR}
      RANLIB=${CMAKE_RANLIB}
      <SOURCE_DIR>/Configure no-shared no-tests)
    set(make_command make)
  endif()

  ExternalProject_Add(OpenSSL
    SOURCE_DIR ${OPENSSL_SRC_DIR}
    BINARY_DIR ${OPENSSL_LIB_DIR}
    CONFIGURE_COMMAND "${configure_command}"
    BUILD_COMMAND "${make_command}"
    BUILD_BYPRODUCTS ${OPENSSL_LIB_DIR}/libcrypto${CMAKE_STATIC_LIBRARY_SUFFIX} ${OPENSSL_LIB_DIR}/libssl${CMAKE_STATIC_LIBRARY_SUFFIX}
    INSTALL_COMMAND ""
  )
endif()

# OQS-Provider configuration for Post-Quantum Cryptography
option(PIPY_PQC "Enable Post-Quantum Cryptography support" ON)

if(PIPY_PQC)
  message(STATUS "Enabling Post-Quantum Cryptography support")
  add_definitions(-DPIPY_USE_PQC)

  # Determine if we're using system OpenSSL or bundled OpenSSL
  set(USING_SYSTEM_OPENSSL ${PIPY_USE_SYSTEM_OPENSSL})
  
  if(USING_SYSTEM_OPENSSL)
    # Detect system OpenSSL version for intelligent PQC configuration
    find_package(OpenSSL REQUIRED)
    message(STATUS "Detected system OpenSSL version: ${OpenSSL_VERSION}")
    
    # Check OpenSSL version compatibility
    if(OpenSSL_VERSION VERSION_LESS "3.2.0")
      message(FATAL_ERROR "PQC support requires OpenSSL >= 3.2.0, but found ${OpenSSL_VERSION}. "
                         "Please upgrade OpenSSL or disable PIPY_PQC.")
    endif()
    
    # Determine PQC strategy based on OpenSSL version
    if(OpenSSL_VERSION VERSION_GREATER_EQUAL "3.5.0")
      message(STATUS "OpenSSL >= 3.5.0 detected: Using built-in PQC algorithms only")
      set(PIPY_PQC_BUILTIN_ONLY ON)
      add_definitions(-DPIPY_PQC_BUILTIN_ONLY)
    else()
      message(STATUS "OpenSSL 3.2.0-3.4.x detected: oqs-provider support available")
      set(PIPY_PQC_BUILTIN_ONLY OFF)
      add_definitions(-DPIPY_USE_OQS_PROVIDER)
    endif()
  else()
    # Using bundled OpenSSL 3.5.2 - always use built-in PQC
    message(STATUS "Using bundled OpenSSL 3.5.2: Using built-in PQC algorithms only")
    set(PIPY_PQC_BUILTIN_ONLY ON)
    add_definitions(-DPIPY_PQC_BUILTIN_ONLY)
  endif()

  # Only configure oqs-provider options when they're relevant
  if(NOT PIPY_PQC_BUILTIN_ONLY)
    include(CMakeDependentOption)
    
    # These options only make sense when using system OpenSSL 3.2-3.4
    cmake_dependent_option(PIPY_USE_SYSTEM_LIBOQS
      "Use system-installed liboqs instead of building from source"
      OFF "USING_SYSTEM_OPENSSL;NOT PIPY_PQC_BUILTIN_ONLY" OFF)
      
    cmake_dependent_option(PIPY_USE_SYSTEM_OQS_PROVIDER
      "Use system-installed oqs-provider instead of building from source" 
      OFF "USING_SYSTEM_OPENSSL;NOT PIPY_PQC_BUILTIN_ONLY" OFF)
    
    # Determine what needs to be built
    set(BUILD_LIBOQS NOT PIPY_USE_SYSTEM_LIBOQS)
    set(BUILD_OQS_PROVIDER NOT PIPY_USE_SYSTEM_OQS_PROVIDER)
    
    # Find system libraries if using them
    if(PIPY_USE_SYSTEM_LIBOQS)
      find_library(LIBOQS_LIB oqs REQUIRED)
      message(STATUS "Found system liboqs: ${LIBOQS_LIB}")
    endif()
    
    if(PIPY_USE_SYSTEM_OQS_PROVIDER)
      find_library(OQS_PROVIDER_LIB oqsprovider REQUIRED)
      message(STATUS "Found system oqs-provider: ${OQS_PROVIDER_LIB}")
    endif()
    
    # Build components if needed
    if(BUILD_LIBOQS OR BUILD_OQS_PROVIDER)
      include(ExternalProject)
      
      # Common CMake arguments for both projects
      set(COMMON_CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_AR=${CMAKE_AR}
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      )
      
      if(BUILD_LIBOQS)
        message(STATUS "Building liboqs from source")
        ExternalProject_Add(liboqs
          SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/liboqs-0.14.0
          INSTALL_DIR ${CMAKE_BINARY_DIR}/deps/liboqs
          CMAKE_ARGS
            ${COMMON_CMAKE_ARGS}
            -DOPENSSL_ROOT_DIR=${CMAKE_SOURCE_DIR}/deps/openssl-3.5.2
            -DOQS_USE_OPENSSL=ON
            -DOQS_BUILD_ONLY_LIB=ON
            -DOQS_USE_AES_INSTRUCTIONS=OFF
            -DOQS_DIST_BUILD=ON
            -DOQS_ALGS_ENABLED=STD
            -DOQS_USE_AES_OPENSSL=ON
            -DOQS_USE_SHA3_OPENSSL=ON
          DEPENDS OpenSSL
        )
      endif()
      
      if(BUILD_OQS_PROVIDER)
        message(STATUS "Building oqs-provider from source")
        set(OQS_PROVIDER_CMAKE_ARGS
          ${COMMON_CMAKE_ARGS}
          -DOPENSSL_ROOT_DIR=${OPENSSL_SRC_DIR}/build
          -DOQS_PROVIDER_BUILD_STATIC=OFF
          -DOQS_KEM_ENCODERS=ON
        )
        
        # Add liboqs dependency if we built it, otherwise use system
        if(BUILD_LIBOQS)
          list(APPEND OQS_PROVIDER_CMAKE_ARGS -Dliboqs_DIR=${CMAKE_BINARY_DIR}/deps/liboqs/lib/cmake/liboqs)
          set(OQS_PROVIDER_DEPENDS OpenSSL liboqs)
        else()
          set(OQS_PROVIDER_DEPENDS OpenSSL)
        endif()
        
        ExternalProject_Add(oqs-provider
          SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/oqs-provider-0.10.0
          INSTALL_DIR ${CMAKE_BINARY_DIR}/deps/oqs-provider
          CMAKE_ARGS ${OQS_PROVIDER_CMAKE_ARGS}
          DEPENDS ${OQS_PROVIDER_DEPENDS}
        )
        
        # Set oqs-provider library path for linking
        set(OQS_PROVIDER_LIB ${CMAKE_BINARY_DIR}/deps/oqs-provider/lib/oqsprovider${CMAKE_SHARED_LIBRARY_SUFFIX})
      endif()
    endif()
    
    # Status summary
    if(PIPY_USE_SYSTEM_LIBOQS AND PIPY_USE_SYSTEM_OQS_PROVIDER)
      message(STATUS "Using system-installed liboqs and oqs-provider")
    elseif(PIPY_USE_SYSTEM_LIBOQS)
      message(STATUS "Using system-installed liboqs, building oqs-provider from source")
    elseif(PIPY_USE_SYSTEM_OQS_PROVIDER)
      message(STATUS "Using system-installed oqs-provider, building liboqs from source")
    else()
      message(STATUS "Building liboqs and oqs-provider from source")
    endif()
  else()
    message(STATUS "oqs-provider not needed - using built-in OpenSSL PQC algorithms")
  endif()
endif()

foreach(component Crypto SSL)
  string(TOLOWER ${component} lower_c)
  set(lib OpenSSL::${component})
  if(NOT TARGET ${lib})
    add_library(${lib} STATIC IMPORTED)
    set_target_properties(${lib} PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INC_DIR}"
      IMPORTED_LINK_INTERFACE_LANGUAGES "C"
      IMPORTED_LOCATION ${OPENSSL_LIB_DIR}/lib${lower_c}${CMAKE_STATIC_LIBRARY_SUFFIX})
    if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
      set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
    endif()
    add_dependencies(${lib} OpenSSL)
  endif()
endforeach()

set(BROTLI_BUNDLED_MODE ON CACHE BOOL "" FORCE)
set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(deps/brotli-1.0.9)

if(PIPY_BROTLI)
  set(BROTLI_INC_DIR ${PIPY_BROTLI}/include)
  set(BROTLI_LIB ${PIPY_BROTLI}/lib/${LIB_BROTLI})
else()
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

set(PIPY_SRC
  src/api/algo.cpp
  src/api/bgp.cpp
  src/api/bpf.cpp
  src/api/codebase-api.cpp
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
  src/compressor.cpp
  src/context.cpp
  src/data.cpp
  src/deframer.cpp
  src/elf.cpp
  src/event.cpp
  src/event-queue.cpp
  src/file.cpp
  src/filter.cpp
  src/filters/bgp.cpp
  src/filters/compress.cpp
  src/filters/connect.cpp
  src/filters/decompress.cpp
  src/filters/deframe.cpp
  src/filters/demux.cpp
  src/filters/detect-idler.cpp
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
add_custom_target(BuiltinCodebases DEPENDS ${CMAKE_BINARY_DIR}/deps/codebases.br.h)

add_dependencies(pipy yajl_s expat ${BROTLI_LIB} GenVer BuiltinCodebases)

if(NOT PIPY_USE_SYSTEM_ZLIB)
  add_dependencies(pipy ${ZLIB_LIB})
endif()

add_subdirectory(tools/pack)

set(CODEBASES "pipy/repo:src/scripts/repo,pipy/node:src/scripts/node")
if(PIPY_SAMPLE_CODEBASES)
  set(CODEBASES "${CODEBASES},samples:samples")
endif()
if(PIPY_CUSTOM_CODEBASES)
  set(CODEBASES "${CODEBASES},${PIPY_CUSTOM_CODEBASES}")
endif()

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/deps/codebases.br.h
  COMMAND ${CMAKE_BINARY_DIR}/${EXE_PACK}
  ARGS ${CMAKE_BINARY_DIR}/deps/codebases.br.h ${CODEBASES} /samples/nmi/,/samples/bpf/
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  DEPENDS pack ${CMAKE_SOURCE_DIR}/src/scripts ${CMAKE_SOURCE_DIR}/samples
)

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
  OpenSSL::SSL
  ${ZLIB_LIB}
  ${BROTLI_LIB}
)

if(WIN32)
  target_link_libraries(pipy crypt32 userenv)
elseif(ANDROID)
  target_link_libraries(pipy -pthread -ldl)
else()
  target_link_libraries(pipy -pthread -ldl -lutil)
endif()
