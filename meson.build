project('commons', 'cpp',
    version: '0.1.8',
    license: 'MIT',
    default_options: [
        'cpp_std=c++23',
        'warning_level=3',
        'werror=false',
    ],
    meson_version: '>=1.3.0',
)

commons_inc = include_directories('include')

# Generate commons/version.hpp into the build tree (see include/commons/meson.build).
subdir('include/commons')

commons_deps = []
commons_with_json = get_option('json')
commons_with_ulid = get_option('ulid')

# Optional integrations default OFF: the headers auto-detect nlohmann/json /
# ulid via __has_include, so the base library forces no dependency. Setting
# `json=true` / `ulid=true` fetches the dependency and hard-defines its gate.
if commons_with_json
    json_dep = dependency('nlohmann_json', version: '>=3.12.0',
        fallback: ['nlohmann_json', 'nlohmann_json_dep'])
    commons_deps += json_dep
    add_project_arguments('-DCOMMONS_WITH_NLOHMANN_JSON=1', language: 'cpp')
endif

if commons_with_ulid
    ulid_dep = dependency('ulid', fallback: ['ulid', 'ulid_dep'])
    commons_deps += ulid_dep
    add_project_arguments('-DCOMMONS_WITH_ULID=1', language: 'cpp')
endif

# Emit a -D only for the Prioritized sentinels the build actually overrode.
# Affects this project's own targets; downstream consumers override by
# predefining the macro (the header default stays canonical).
prioritized_overrides = {
    'prioritized_highest_precedence': 'COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE',
    'prioritized_lowest_precedence': 'COMMONS_PRIORITIZED_LOWEST_PRECEDENCE',
    'prioritized_default_priority': 'COMMONS_PRIORITIZED_DEFAULT_PRIORITY',
    'audit_records_capacity': 'COMMONS_AUDIT_RECORDS_CAPACITY',
    'lock_free_queue_default_capacity': 'COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY',
}
foreach optname, macroname : prioritized_overrides
    optval = get_option(optname)
    if optval != ''
        add_project_arguments('-D' + macroname + '=' + optval, language: 'cpp')
    endif
endforeach

# comms::LockFreeQueue's Dynamic mode uses a 16-byte std::atomic<TaggedPtr> (and
# std::atomic<T> for a 16-byte T). On toolchains where the __atomic_*_16 entry
# points are out-of-line (notably GCC/x86-64) these resolve from libatomic, so a
# bare link fails; probe for it and add libatomic transitively. On macOS/arm64
# the ops are inline lock-free and the bare link succeeds, so nothing is added.
cc = meson.get_compiler('cpp')
dwcas_probe = '''
#include <atomic>
#include <cstdint>
struct Big { void* a; std::uintptr_t b; };
int main() {
    std::atomic<Big> g;
    Big v{};
    g.store(v);
    Big e = g.load();
    return g.compare_exchange_strong(e, v) ? 0 : 1;
}
'''
if not cc.links(dwcas_probe, name: '16-byte atomics without libatomic')
    libatomic = cc.find_library('atomic', required: false)
    if libatomic.found() and cc.links(dwcas_probe, dependencies: libatomic, name: '16-byte atomics with libatomic')
        commons_deps += libatomic
    else
        warning('16-byte atomics link with neither the bare toolchain nor -latomic; LockFreeQueue Dynamic mode may fail to link here.')
    endif
endif

commons_dep = declare_dependency(
    include_directories: commons_inc,
    dependencies: commons_deps,
    version: meson.project_version(),
)

meson.override_dependency('commons', commons_dep)

# The template and this directory's build script must not ship; the generated
# version.hpp is installed by include/commons/meson.build instead.
install_subdir('include/commons',
    install_dir: get_option('includedir'),
    exclude_files: ['version.hpp.in', 'meson.build'],
)

pkg_requires = []
if commons_with_json
    pkg_requires += 'nlohmann_json'
endif
if commons_with_ulid
    pkg_requires += 'ulid'
endif

import('pkgconfig').generate(
    name: 'commons',
    description: 'Header-only C++23 library of common/shared types',
    version: meson.project_version(),
    requires: pkg_requires,
)

if get_option('tests')
    subdir('tests')
endif
if get_option('examples')
    subdir('examples')
endif
