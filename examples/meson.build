commons_examples = {
    'commons_hello': 'hello.cpp',
    'commons_color': 'color.cpp',
    'commons_icon': 'icon.cpp',
    'commons_display_info': 'display_info.cpp',
    'commons_flag': 'flag.cpp',
    'commons_id': 'id.cpp',
    'commons_identity_ability': 'identity_ability.cpp',
    'commons_audit_record': 'audit_record.cpp',
    'commons_lifecycle': 'lifecycle.cpp',
    'commons_lock_free_queue': 'lock_free_queue.cpp',
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

# The lock-free queue demo needs threads; on x86-64 -mcx16 keeps Dynamic mode
# on the lock-free path (cmpxchg16b).
commons_examples_threads_dep = dependency('threads')
commons_examples_x86_args = []
if host_machine.cpu_family() == 'x86_64' and meson.get_compiler('cpp').get_id() != 'msvc'
    commons_examples_x86_args += '-mcx16'
endif

foreach name, src : commons_examples
    if name == 'commons_lock_free_queue'
        executable(name, src,
            dependencies: [commons_dep, commons_examples_threads_dep],
            cpp_args: commons_examples_x86_args,
        )
    else
        executable(name, src, dependencies: [commons_dep])
    endif
endforeach
