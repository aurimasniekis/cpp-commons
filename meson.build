project('commons', 'cpp',
    version: '0.1.0',
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

# Optional integration defaults OFF: the headers auto-detect nlohmann/json via
# __has_include, so the base library forces no dependency. json=true fetches
# the dependency and hard-defines the gate macro.
if commons_with_json
    json_dep = dependency('nlohmann_json', version: '>=3.12.0',
        fallback: ['nlohmann_json', 'nlohmann_json_dep'])
    commons_deps += json_dep
    add_project_arguments('-DCOMMONS_WITH_NLOHMANN_JSON=1', language: 'cpp')
endif

# Emit a -D only for the Prioritized sentinels the build actually overrode.
# Affects this project's own targets; downstream consumers override by
# predefining the macro (the header default stays canonical).
prioritized_overrides = {
    'prioritized_highest_precedence': 'COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE',
    'prioritized_lowest_precedence': 'COMMONS_PRIORITIZED_LOWEST_PRECEDENCE',
    'prioritized_default_priority': 'COMMONS_PRIORITIZED_DEFAULT_PRIORITY',
}
foreach optname, macroname : prioritized_overrides
    optval = get_option(optname)
    if optval != ''
        add_project_arguments('-D' + macroname + '=' + optval, language: 'cpp')
    endif
endforeach

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
