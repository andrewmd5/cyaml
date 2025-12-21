cmake_minimum_required(VERSION 3.14)
project(cyaml
    VERSION 0.0.0
    DESCRIPTION "Ultralightweight YAML parser in C11"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_RESULT
    )
    if(GIT_RESULT EQUAL 0 AND GIT_VERSION MATCHES "^v?[0-9]+\\.[0-9]+")
        string(REGEX REPLACE "^v" "" CYAML_VERSION_STR "${GIT_VERSION}")
    else()
        set(CYAML_VERSION_STR "${PROJECT_VERSION}-dev")
    endif()
else()
    set(CYAML_VERSION_STR "${PROJECT_VERSION}-dev")
endif()
message(STATUS "CYAML version: ${CYAML_VERSION_STR}")

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${CYAML_VERSION_STR}")
set(CYAML_VERSION_MAJOR ${CMAKE_MATCH_1})
set(CYAML_VERSION_MINOR ${CMAKE_MATCH_2})
set(CYAML_VERSION_PATCH ${CMAKE_MATCH_3})
set(CYAML_VERSION "${CYAML_VERSION_MAJOR}.${CYAML_VERSION_MINOR}.${CYAML_VERSION_PATCH}")

option(CYAML_BUILD_TESTS "Build test suite" ON)
option(CYAML_BUILD_SHARED "Build shared library" ON)
option(CYAML_BUILD_STATIC "Build static library" ON)
option(CYAML_ENABLE_LTO "Enable Link Time Optimization for Release" ON)
option(CYAML_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(CYAML_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
if(MSVC)
    option(CYAML_STATIC_CRT "Use static C runtime on Windows" OFF)
endif()

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -fvisibility=hidden)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3 -DNDEBUG)
        if(CYAML_ENABLE_LTO)
            include(CheckIPOSupported)
            check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
            if(LTO_SUPPORTED)
                set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
                message(STATUS "LTO enabled")
            else()
                message(WARNING "LTO not supported: ${LTO_ERROR}")
            endif()
        endif()
    endif()
    if(CYAML_ENABLE_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
        message(STATUS "AddressSanitizer enabled")
    endif()
    if(CYAML_ENABLE_UBSAN)
        add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=undefined)
        message(STATUS "UndefinedBehaviorSanitizer enabled")
    endif()
elseif(MSVC)
    add_compile_options(/W4 /WX /utf-8)
    if(CYAML_STATIC_CRT)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/O2 /DNDEBUG)
        if(CYAML_ENABLE_LTO)
            add_compile_options(/GL)
            add_link_options(/LTCG)
            message(STATUS "MSVC whole program optimization enabled")
        endif()
    endif()
endif()

set(CYAML_SOURCES
    src/cyaml.c
    src/cyaml_utf8.c
    src/cyaml_parser.c
    src/cyaml_emitter.c
    src/cyaml_events.c
    src/cyaml_json.c
    src/cyaml_path.c
    src/cyaml_modify.c
)

set(CYAML_HEADERS src/cyaml.h)

include(cmake/GenerateExports.cmake)
cyaml_generate_exports("${CMAKE_CURRENT_SOURCE_DIR}/src/cyaml.h" "${CMAKE_CURRENT_BINARY_DIR}")

if(CYAML_BUILD_STATIC)
    add_library(cyaml_static STATIC ${CYAML_SOURCES})
    target_include_directories(cyaml_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(cyaml_static PRIVATE CYAML_VERSION_STR="${CYAML_VERSION_STR}")
    set_target_properties(cyaml_static PROPERTIES OUTPUT_NAME cyaml PUBLIC_HEADER "${CYAML_HEADERS}")
    if(UNIX AND NOT APPLE)
        target_link_libraries(cyaml_static PRIVATE m)
    endif()
endif()

if(CYAML_BUILD_SHARED)
    add_library(cyaml_shared SHARED ${CYAML_SOURCES})
    target_include_directories(cyaml_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(cyaml_shared PRIVATE CYAML_VERSION_STR="${CYAML_VERSION_STR}")
    set_target_properties(cyaml_shared PROPERTIES
        OUTPUT_NAME cyaml
        VERSION ${CYAML_VERSION}
        SOVERSION ${CYAML_VERSION_MAJOR}
        PUBLIC_HEADER "${CYAML_HEADERS}"
    )
    if(APPLE)
        set_target_properties(cyaml_shared PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR ON)
    endif()
    if(WIN32)
        target_compile_definitions(cyaml_shared PRIVATE CYAML_BUILDING_DLL)
        configure_file(cmake/cyaml.rc.in ${CMAKE_CURRENT_BINARY_DIR}/cyaml.rc @ONLY)
        target_sources(cyaml_shared PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/cyaml.def ${CMAKE_CURRENT_BINARY_DIR}/cyaml.rc)
    elseif(APPLE)
        set_target_properties(cyaml_shared PROPERTIES LINK_FLAGS "-Wl,-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/exports_darwin.txt")
    else()
        set_target_properties(cyaml_shared PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports_linux.map")
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(cyaml_shared PRIVATE m)
    endif()
endif()

if(CYAML_BUILD_STATIC)
    add_library(cyaml::cyaml ALIAS cyaml_static)
elseif(CYAML_BUILD_SHARED)
    add_library(cyaml::cyaml ALIAS cyaml_shared)
endif()

if(CYAML_BUILD_TESTS)
    enable_testing()

    add_library(unity STATIC third-party/Unity/src/unity.c)
    target_include_directories(unity PUBLIC third-party/Unity/src)
    target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

    set(CYAML_LIB $<IF:$<BOOL:${CYAML_BUILD_STATIC}>,cyaml_static,cyaml_shared>)

    add_executable(test_api tests/test_api.c)
    target_link_libraries(test_api PRIVATE unity ${CYAML_LIB})
    if(UNIX AND NOT APPLE)
        target_link_libraries(test_api PRIVATE m)
    endif()
    add_test(NAME test_api COMMAND test_api)

    foreach(TOOL cyaml_suite dump_debug parse_debug emit_debug json_debug results_query ypath_test ypath_debug summary_gen)
        if(TOOL STREQUAL "cyaml_suite")
            set(TOOL_SRC tests/suite_runner.c)
        else()
            set(TOOL_SRC tests/${TOOL}.c)
        endif()
        add_executable(${TOOL} ${TOOL_SRC})
        target_link_libraries(${TOOL} PRIVATE ${CYAML_LIB})
    endforeach()

    option(CYAML_BUILD_BENCHMARK "Build benchmark tool (Unix only)" OFF)
    if(CYAML_BUILD_BENCHMARK AND NOT WIN32)
        add_executable(benchmark tests/benchmark.c)
        target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(benchmark PRIVATE ${CYAML_LIB})
        target_compile_definitions(benchmark PRIVATE CYAML_VERSION_STR="${CYAML_VERSION_STR}")
    endif()
endif()

include(GNUInstallDirs)

if(CYAML_BUILD_STATIC)
    install(TARGETS cyaml_static EXPORT cyamlTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if(CYAML_BUILD_SHARED)
    install(TARGETS cyaml_shared EXPORT cyamlTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

install(EXPORT cyamlTargets FILE cyamlTargets.cmake NAMESPACE cyaml:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyaml)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cyamlConfigVersion.cmake"
    VERSION ${CYAML_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_file(cmake/cyamlConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/cyamlConfig.cmake" @ONLY)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cyamlConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cyamlConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyaml
)
