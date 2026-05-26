cmake_minimum_required(VERSION 3.25)

project(commons
    VERSION     0.1.3
    DESCRIPTION "Header-only C++23 library of common/shared types for the C++ libraries"
    LANGUAGES   CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(COMMONS_BUILD_TESTS         "Build commons tests"                  ${PROJECT_IS_TOP_LEVEL})
option(COMMONS_BUILD_EXAMPLES      "Build commons examples"               ${PROJECT_IS_TOP_LEVEL})
option(COMMONS_BUILD_DOCS          "Build Doxygen API documentation"      OFF)
option(COMMONS_ENABLE_CLANG_TIDY   "Run clang-tidy during build"          OFF)
option(COMMONS_CLANG_TIDY_FIX      "Let clang-tidy apply fixes in place"  OFF)
option(COMMONS_ENABLE_SANITIZERS   "Build with ASan + UBSan (Debug)"      OFF)
option(COMMONS_ENABLE_COVERAGE     "Build with Clang source-based coverage" OFF)
option(COMMONS_WARNINGS_AS_ERRORS  "Treat compiler warnings as errors"    ${PROJECT_IS_TOP_LEVEL})
option(COMMONS_INSTALL             "Generate install rules for commons"   ${PROJECT_IS_TOP_LEVEL})

# Optional integrations default OFF: the headers auto-detect nlohmann/json via
# __has_include, so the base library forces no dependency. Turning this ON
# additionally fetches/links the dependency and hard-defines the gate macro.
option(COMMONS_WITH_NLOHMANN_JSON  "Force-enable nlohmann/json integration (fetches the dependency)" OFF)

# Override seams for comms::Prioritized's sentinel values. The defaults are
# numeric_limits-based and have no CMake literal, so the C++ header default is
# canonical: an empty value leaves it alone, a non-empty value is force-defined
# as the matching macro. STRING (not option()) because these carry integers.
set(COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE "" CACHE STRING "Override comms::Prioritized::HIGHEST_PRECEDENCE")
set(COMMONS_PRIORITIZED_LOWEST_PRECEDENCE  "" CACHE STRING "Override comms::Prioritized::LOWEST_PRECEDENCE")
set(COMMONS_PRIORITIZED_DEFAULT_PRIORITY   "" CACHE STRING "Override comms::Prioritized::DEFAULT_PRIORITY")

include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(Dependencies)

# Generate commons/version.hpp from the template, using the project version as
# the single source of truth. It lands in the build tree's include dir (below),
# so the source tree stays clean.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/commons/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/commons/version.hpp"
    @ONLY
)

add_library(commons INTERFACE)
add_library(commons::commons ALIAS commons)

target_include_directories(commons
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(commons INTERFACE cxx_std_23)

if(COMMONS_WITH_NLOHMANN_JSON AND TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(commons INTERFACE nlohmann_json::nlohmann_json)
    target_compile_definitions(commons INTERFACE COMMONS_WITH_NLOHMANN_JSON=1)
endif()

# Emit a -D only for the Prioritized sentinels the consumer actually overrode.
foreach(_v
    COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE
    COMMONS_PRIORITIZED_LOWEST_PRECEDENCE
    COMMONS_PRIORITIZED_DEFAULT_PRIORITY)
    if(NOT "${${_v}}" STREQUAL "")
        target_compile_definitions(commons INTERFACE ${_v}=${${_v}})
    endif()
endforeach()

if(COMMONS_ENABLE_CLANG_TIDY)
    find_program(COMMONS_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(_commons_tidy_cmd
        "${COMMONS_CLANG_TIDY_EXE}"
        "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
    )
    if(COMMONS_CLANG_TIDY_FIX)
        list(APPEND _commons_tidy_cmd "--fix")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-search-dirs
            OUTPUT_VARIABLE _commons_gcc_search_dirs
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_commons_gcc_search_dirs MATCHES "install: ([^\n]+)")
            list(APPEND _commons_tidy_cmd
                "--extra-arg-before=--gcc-install-dir=${CMAKE_MATCH_1}")
            message(STATUS
                "commons: pinning clang-tidy to gcc install dir ${CMAKE_MATCH_1}")
        endif()
    endif()
    set(CMAKE_CXX_CLANG_TIDY "${_commons_tidy_cmd}")
    message(STATUS "commons: clang-tidy enabled (${COMMONS_CLANG_TIDY_EXE})")
endif()

if(COMMONS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(COMMONS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(COMMONS_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    set(COMMONS_DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )
    add_custom_target(commons_docs
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating Doxygen documentation"
        VERBATIM
    )
endif()

if(COMMONS_INSTALL AND DEFINED nlohmann_json_SOURCE_DIR)
    message(STATUS
        "commons: COMMONS_INSTALL disabled — a fetched dependency (nlohmann_json) "
        "cannot be re-exported. Install it via system package or find_package, or "
        "leave COMMONS_WITH_NLOHMANN_JSON OFF, to re-enable install rules."
    )
    set(COMMONS_INSTALL OFF)
endif()

if(COMMONS_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS commons
        EXPORT  commonsTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/commons
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    # The generated version.hpp lives in the build tree, so install it alongside.
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/include/commons/version.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/commons
    )

    install(EXPORT commonsTargets
        FILE        commonsTargets.cmake
        NAMESPACE   commons::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/commons
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/commonsConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/commonsConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/commonsConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/commons
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/commonsConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/commonsConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/commons
    )
endif()
