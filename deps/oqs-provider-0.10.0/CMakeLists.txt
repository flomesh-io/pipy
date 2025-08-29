if (WIN32)
cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
else()
cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
endif()
project(oqs-provider LANGUAGES C)
set(OQSPROVIDER_VERSION_TEXT "0.10.0")
set(CMAKE_C_STANDARD 11)
set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS ON)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Creating Debug build with OQS-Debug env vars enabled")
else()
    message(STATUS "Creating Release build")
    add_definitions( -DNDEBUG )
endif()

if(MSVC)
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
  add_definitions(-DOQS_PROVIDER_NOATOMIC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  set(OQS_ADDL_SOCKET_LIBS ws2_32.lib gdi32.lib crypt32.lib)
else()
  add_compile_options(-Wunused-function)
  set(OQS_ADDL_SOCKET_LIBS "")
endif()

option(OQS_KEM_ENCODERS "Provide encoders (and decoders) for KEM algorithms " OFF)
if(${OQS_KEM_ENCODERS})
    message(STATUS "Build provides support for encoding KEMs")
    add_compile_definitions( OQS_KEM_ENCODERS )
endif()

option(NOPUBKEY_IN_PRIVKEY "Do not include public keys in private key structures/PKCS#8 " OFF)
if(${NOPUBKEY_IN_PRIVKEY})
    message(STATUS "Build will not store public keys alongside private keys in PKCS#8 structures")
    add_compile_definitions( NOPUBKEY_IN_PRIVKEY )
else()
    message(STATUS "Build will store public keys in PKCS#8 structures")
endif()

option(OQS_PROVIDER_BUILD_STATIC "Build a static library instead of a shared library" OFF)
if(OQS_PROVIDER_BUILD_STATIC AND BUILD_SHARED_LIBS)
  message(FATAL_ERROR "`OQS_PROVIDER_BUILD_STATIC` is not compatible with `BUILD_SHARED_LIBS`.")
endif()

include(CheckLibraryExists)
include(CheckFunctionExists)

# Add required includes and install locations for openssl
find_package(OpenSSL 3.0 REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})
if (WIN32)
# get_filename_component seems to fail when facing windows paths
# so use new(er) cmake_path instruction there
cmake_path(GET OPENSSL_CRYPTO_LIBRARY PARENT_PATH OQS_MODULES_PARENT_PATH)
cmake_path(APPEND OQS_MODULES_PARENT_PATH "ossl-modules" OUTPUT_VARIABLE OPENSSL_MODULES_PATH)
else()
get_filename_component(OPENSSL_LIB_DIR ${OPENSSL_CRYPTO_LIBRARY} DIRECTORY)
if(NOT DEFINED OPENSSL_MODULES_PATH)
    set(OPENSSL_MODULES_PATH ${OPENSSL_LIB_DIR}/ossl-modules CACHE PATH "Path to OpenSSL modules")
endif()
endif()

# Add required include for liboqs
find_package(liboqs REQUIRED)
get_target_property(LIBOQS_INCLUDE_DIR OQS::oqs INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(LIBOQS_LIBRARY_TYPE OQS::oqs TYPE)
message(STATUS "liboqs found: Include dir at ${LIBOQS_INCLUDE_DIR} (${LIBOQS_LIBRARY_TYPE})")
include_directories(${LIBOQS_INCLUDE_DIR})

# Hints the compiler on the fact that the provider is being compiled into a static library.
function(targets_set_static_provider)
  foreach(target ${ARGN})
    target_compile_definitions(${target} PRIVATE "OQS_PROVIDER_STATIC")
    if(NOT target STREQUAL oqsprovider)
      target_link_libraries(${target} PRIVATE oqsprovider)
    endif()
  endforeach()
endfunction()

# Provider module
add_subdirectory(oqsprov)

# Testing
include(CTest)
enable_testing()
add_subdirectory(test)

# Examples
add_subdirectory(examples)
