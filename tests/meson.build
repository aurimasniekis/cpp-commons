gtest_dep      = dependency('gtest',      fallback: ['gtest', 'gtest_dep'])
gtest_main_dep = dependency('gtest_main', fallback: ['gtest', 'gtest_main_dep'])

commons_test_sources = [
    'test_fixed_string.cpp',
    'test_types.cpp',
    'test_commons.cpp',
    'test_color.cpp',
    'test_icon.cpp',
    'test_id.cpp',
    'test_literals.cpp',
    'test_display_info.cpp',
    'test_flag.cpp',
    'test_origin.cpp',
    'test_prioritized.cpp',
    'test_semver.cpp',
    'test_version_constraint.cpp',
]

if commons_with_json
    commons_test_sources += 'test_json.cpp'
endif

commons_tests = executable('commons_tests',
    sources: commons_test_sources,
    dependencies: [commons_dep, gtest_dep, gtest_main_dep],
)

test('commons', commons_tests, protocol: 'gtest')
