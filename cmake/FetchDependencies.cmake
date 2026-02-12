include(FetchContent)

# Disable the update step for already-populated dependencies.
# FetchContent's default update step runs "git pull --rebase" which fails
# when GIT_TAG is a tag (not a branch) because origin/<tag> does not exist
# as a remote-tracking branch.  Since we pin exact versions in
# DependencyVersions.cmake, there is no need to re-pull after the initial
# clone.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

#-------------------------------------------------------------------------------
# 1. LibYAML
#-------------------------------------------------------------------------------
FetchContent_Declare(
  libyaml
  GIT_REPOSITORY ${DEP_LIBYAML_REPO}
  GIT_TAG        ${DEP_LIBYAML_VERSION}
  GIT_SHALLOW    TRUE
  PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchLibyaml.cmake
)
set(YAML_STATIC_LIB_NAME "yaml" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(libyaml)

#-------------------------------------------------------------------------------
# 2. Expat
#-------------------------------------------------------------------------------
set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(EXPAT_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  libexpat
  GIT_REPOSITORY ${DEP_EXPAT_REPO}
  GIT_TAG        ${DEP_EXPAT_VERSION}
  GIT_SHALLOW    TRUE
  # Do NOT use SOURCE_SUBDIR here — it requires CMake 3.18+
  # and CentOS 7 ships cmake3 3.17.x.  We manually add_subdirectory below.
)
FetchContent_GetProperties(libexpat)
if(NOT libexpat_POPULATED)
  FetchContent_Populate(libexpat)
  add_subdirectory(${libexpat_SOURCE_DIR}/expat ${libexpat_BINARY_DIR})
endif()

#-------------------------------------------------------------------------------
# 3. LevelDB
#-------------------------------------------------------------------------------
set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  leveldb
  GIT_REPOSITORY ${DEP_LEVELDB_REPO}
  GIT_TAG        ${DEP_LEVELDB_VERSION}
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(leveldb)

if(PIPY_SHARED AND UNIX AND NOT APPLE)
  set_target_properties(leveldb PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

#-------------------------------------------------------------------------------
# 4. Zlib
#-------------------------------------------------------------------------------
if(NOT PIPY_USE_SYSTEM_ZLIB AND NOT PIPY_ZLIB)
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    zlib
    GIT_REPOSITORY ${DEP_ZLIB_REPO}
    GIT_TAG        ${DEP_ZLIB_VERSION}
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(zlib)

  set(ZLIB_INC_DIR "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
  set(ZLIB_LIB zlibstatic)

  if(PIPY_SHARED AND UNIX AND NOT APPLE)
    set_target_properties(zlibstatic PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

#-------------------------------------------------------------------------------
# 5. Brotli
#-------------------------------------------------------------------------------
if(NOT PIPY_BROTLI)
  set(BROTLI_BUNDLED_MODE ON CACHE BOOL "" FORCE)
  set(BROTLI_DISABLE_TESTS ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    brotli
    GIT_REPOSITORY ${DEP_BROTLI_REPO}
    GIT_TAG        ${DEP_BROTLI_VERSION}
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(brotli)

  set(BROTLI_INC_DIR "${brotli_SOURCE_DIR}/c/include")
  set(BROTLI_LIB brotlidec)
endif()

#-------------------------------------------------------------------------------
# 6. ASIO (Header-only)
#-------------------------------------------------------------------------------
FetchContent_Declare(
  asio
  GIT_REPOSITORY ${DEP_ASIO_REPO}
  GIT_TAG        ${DEP_ASIO_VERSION}
  GIT_SHALLOW    TRUE
)
# ASIO has no root CMakeLists.txt, MakeAvailable just populates it
FetchContent_MakeAvailable(asio)

#-------------------------------------------------------------------------------
# 7. RapidJSON (Header-only)
#-------------------------------------------------------------------------------
# RapidJSON is header-only; its CMakeLists.txt uses cmake_minimum_required 2.8
# which fails on CMake 4.x, so we skip add_subdirectory entirely.
FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY ${DEP_RAPIDJSON_REPO}
  GIT_TAG        ${DEP_RAPIDJSON_VERSION}
  GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
  FetchContent_Populate(rapidjson)
endif()
