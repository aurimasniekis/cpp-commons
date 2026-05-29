commons_examples = {
    'commons_hello': 'hello.cpp',
    'commons_color': 'color.cpp',
    'commons_icon': 'icon.cpp',
    'commons_display_info': 'display_info.cpp',
    'commons_flag': 'flag.cpp',
    'commons_id': 'id.cpp',
    'commons_origin': 'origin.cpp',
    'commons_prioritized': 'prioritized.cpp',
    'commons_semver': 'semver.cpp',
}

if commons_with_json
    commons_examples += {'commons_json_integration': 'json_integration.cpp'}
endif

foreach name, src : commons_examples
    executable(name, src, dependencies: [commons_dep])
endforeach
