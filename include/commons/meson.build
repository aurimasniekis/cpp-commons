# Generate commons/version.hpp from the template. Output lands in the build
# tree at <build>/include/commons/version.hpp, which is on the include path via
# the top-level include_directories('include') (Meson also searches the build
# dir counterpart), so <commons/version.hpp> resolves without touching the
# source tree.

version_parts = meson.project_version().split('.')

version_conf = configuration_data()
version_conf.set('PROJECT_VERSION', meson.project_version())
version_conf.set('PROJECT_VERSION_MAJOR', version_parts[0])
version_conf.set('PROJECT_VERSION_MINOR', version_parts[1])
version_conf.set('PROJECT_VERSION_PATCH', version_parts[2])

configure_file(
    input: 'version.hpp.in',
    output: 'version.hpp',
    configuration: version_conf,
    install: true,
    install_dir: join_paths(get_option('includedir'), 'commons'),
)
