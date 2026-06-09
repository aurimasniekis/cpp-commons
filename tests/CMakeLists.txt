include(GoogleTest)

set(_commons_test_sources
    test_fixed_string.cpp
    test_types.cpp
    test_commons.cpp
    test_color.cpp
    test_icon.cpp
    test_id.cpp
    test_ability.cpp
    test_identity.cpp
    test_audit_record.cpp
    test_change_audit_record.cpp
    test_audit_records.cpp
    test_lifecycle.cpp
    test_literals.cpp
    test_lock_free_queue.cpp
    test_display_info.cpp
    test_flag.cpp
    test_origin.cpp
    test_reason.cpp
    test_prioritized.cpp
    test_semver.cpp
    test_version_constraint.cpp
    test_metadata_value.cpp
    test_metadata_object.cpp
    test_metadata_helpers.cpp
    test_metadata_path.cpp
    test_metadata_merge.cpp
    test_metadata_compare.cpp
    test_metadata_hash.cpp
    test_metadata_ostream.cpp
    test_metadata_format.cpp
)

if(COMMONS_WITH_NLOHMANN_JSON AND TARGET nlohmann_json::nlohmann_json)
    list(APPEND _commons_test_sources test_json.cpp test_metadata_json.cpp test_lifecycle_json.cpp)
endif()

add_executable(commons_tests ${_commons_test_sources})

# LockFreeQueue's concurrency tests need a threading runtime.
find_package(Threads REQUIRED)

target_link_libraries(commons_tests
    PRIVATE
        commons::commons
        GTest::gtest_main
        Threads::Threads
)

# On x86-64, -mcx16 makes the Dynamic mode's 16-byte tagged-pointer atomic
# lock-free (cmpxchg16b) so the tests exercise it on the lock-free path.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT MSVC)
    target_compile_options(commons_tests PRIVATE -mcx16)
endif()

commons_set_warnings(commons_tests PRIVATE)
commons_enable_sanitizers(commons_tests)
commons_enable_tsan(commons_tests)
commons_enable_coverage(commons_tests)

gtest_discover_tests(commons_tests)
