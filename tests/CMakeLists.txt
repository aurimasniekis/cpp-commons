include(GoogleTest)

set(_commons_test_sources
    test_fixed_string.cpp
    test_types.cpp
    test_commons.cpp
    test_color.cpp
    test_icon.cpp
    test_literals.cpp
    test_display_info.cpp
    test_flag.cpp
    test_origin.cpp
    test_prioritized.cpp
    test_semver.cpp
    test_version_constraint.cpp
)

if(COMMONS_WITH_NLOHMANN_JSON AND TARGET nlohmann_json::nlohmann_json)
    list(APPEND _commons_test_sources test_json.cpp)
endif()

add_executable(commons_tests ${_commons_test_sources})

target_link_libraries(commons_tests
    PRIVATE
        commons::commons
        GTest::gtest_main
)

commons_set_warnings(commons_tests PRIVATE)
commons_enable_sanitizers(commons_tests)
commons_enable_coverage(commons_tests)

gtest_discover_tests(commons_tests)
