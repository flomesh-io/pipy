cmake_minimum_required(VERSION 3.10)
project(pack)

if(CMAKE_CROSSCOMPILING)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    set(PACK_BIN_DIR "${CMAKE_BINARY_DIR}")
    set(HOST_BUILD_DIR "${CMAKE_BINARY_DIR}/host_pack")

    if(WIN32)
        set(PACK_EXE "${PACK_BIN_DIR}/Release/pack.exe")
    else()
        set(PACK_EXE "${PACK_BIN_DIR}/pack")
    endif()

    set(BROTLI_SOURCE_DIR "${PROJECT_ROOT}/deps/brotli-1.0.9")
    set(HOST_BROTLI_DIR "${HOST_BUILD_DIR}/brotli")

    file(WRITE "${HOST_BROTLI_DIR}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.10)\n"
        "project(host_brotli)\n"
        "set(BROTLI_BUNDLED_MODE ON)\n"
        "set(BROTLI_DISABLE_TESTS ON)\n"
        "add_subdirectory(\"${BROTLI_SOURCE_DIR}\" \"${HOST_BUILD_DIR}/brotli-bin\")\n"
    )

file(WRITE "${HOST_BUILD_DIR}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.10)\n"
    "project(host_pack_with_brotli)\n"
    "set(BROTLI_BUNDLED_MODE ON)\n"
    "set(BROTLI_DISABLE_TESTS ON)\n"
    "add_subdirectory(\"${BROTLI_SOURCE_DIR}\" \"${HOST_BUILD_DIR}/brotli-bin\")\n"
    "include_directories(\"${BROTLI_SOURCE_DIR}/c/include\")\n"
    "add_executable(pack \"${CMAKE_CURRENT_SOURCE_DIR}/main.cpp\")\n"
    "if(WIN32)\n"
    "  set(LIB_ENC \${CMAKE_CURRENT_BINARY_DIR}/brotli-bin/Release/libbrotlienc-static.lib)\n"
    "  set(LIB_COMMON \${CMAKE_CURRENT_BINARY_DIR}/brotli-bin/Release/libbrotlicommon-static.lib)\n"
    "  set(LIB_DEC \${CMAKE_CURRENT_BINARY_DIR}/brotli-bin/Release/libbrotlidec-static.lib)\n"
    "else()\n"
    "  set(LIB_ENC \${CMAKE_CURRENT_BINARY_DIR}/brotli-bin/libbrotlienc-static.a)\n"
    "  set(LIB_COMMON \${CMAKE_CURRENT_BINARY_DIR}/brotli-bin/libbrotlicommon-static.a)\n"
    "  set(LIB_DEC \${CMAKE_CURRENT_BINARY_DIR}/brotli-bin/libbrotlidec-static.a)\n"
    "endif()\n"
    "target_link_libraries(pack \${LIB_ENC} \${LIB_COMMON} \${LIB_DEC})\n"
    "set_target_properties(pack PROPERTIES RUNTIME_OUTPUT_DIRECTORY \"${HOST_BUILD_DIR}\")\n"
)

    add_custom_command(
        OUTPUT "${PACK_EXE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HOST_BROTLI_DIR}"
        COMMAND ${CMAKE_COMMAND} -S "${HOST_BROTLI_DIR}" -B "${HOST_BROTLI_DIR}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        COMMAND ${CMAKE_COMMAND} --build "${HOST_BROTLI_DIR}" --config Release
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HOST_BUILD_DIR}"
        COMMAND ${CMAKE_COMMAND} -S "${HOST_BUILD_DIR}" -B "${HOST_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        COMMAND ${CMAKE_COMMAND} --build "${HOST_BUILD_DIR}" --config Release
        COMMAND ${CMAKE_COMMAND} -E copy
        "$<$<BOOL:${WIN32}>:${HOST_BUILD_DIR}/Release/pack.exe>$<$<NOT:$<BOOL:${WIN32}>>:${HOST_BUILD_DIR}/pack>"
        "${PACK_EXE}"
        COMMENT "Building pack for host architecture"
        VERBATIM
    )

    add_custom_target(pack DEPENDS "${PACK_EXE}")
    set_target_properties(pack PROPERTIES IMPORTED_LOCATION "${PACK_EXE}")

else()
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

    if(NOT WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
        add_definitions(-D_GNU_SOURCE)
    endif()

    include_directories(
        "${CMAKE_SOURCE_DIR}/../../deps/brotli-1.0.9/c/include"
    )

    add_executable(pack main.cpp)
    target_link_libraries(pack brotlienc-static)
endif()