commons_examples = {
    'commons_hello': 'hello.cpp',
    'commons_color': 'color.cpp',
    'commons_icon': 'icon.cpp',
    'commons_display_info': 'display_info.cpp',
    'commons_flag': 'flag.cpp',
    'commons_id': 'id.cpp',
    'commons_audit_record': 'audit_record.cpp',
    'commons_metadata_basic': 'metadata/basic.cpp',
    'commons_metadata_object_helpers': 'metadata/object_helpers.cpp',
    'commons_metadata_nested': 'metadata/nested.cpp',
    'commons_metadata_merge': 'metadata/merge.cpp',
    'commons_metadata_path_lookup': 'metadata/path_lookup.cpp',
    'commons_metadata_format_output': 'metadata/format_output.cpp',
    'commons_origin': 'origin.cpp',
    'commons_reason': 'reason.cpp',
    'commons_prioritized': 'prioritized.cpp',
    'commons_semver': 'semver.cpp',
}

if commons_with_json
    commons_examples += {'commons_json_integration': 'json_integration.cpp'}
    commons_examples += {'commons_metadata_json_integration': 'metadata/json_integration.cpp'}
endif

foreach name, src : commons_examples
    executable(name, src, dependencies: [commons_dep])
endforeach
