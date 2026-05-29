cmake_minimum_required(VERSION 3.25)

project(commons_consumer_fetch_content
        VERSION 0.1.0
        DESCRIPTION "Standalone downstream demo: pull commons via FetchContent"
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Pin the published release tarball + its SHA256. Reproducible, no git required
# at configure time, fastest to fetch on CI. To recompute the SHA256 yourself:
#
#   curl -sL https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.5.tar.gz \
#     | shasum -a 256
#
# (The CI consumer job overrides the source dir with the in-tree checkout, so
# this snippet is also exercised without hitting the network. See README.md.)
FetchContent_Declare(
        cpp-commons
        URL      https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.5.tar.gz
        URL_HASH SHA256=1c98ee66a8ac5bfd8b8580ddf775e5d34018ce21670c9e14f9cc2179901f521f
)

# Local-validation tip: to test this snippet against an in-tree checkout,
# point CMake at the source tree:
#
#   cmake -S examples/consumers/fetch_content \
#         -B /tmp/commons-consumer-build \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DFETCHCONTENT_SOURCE_DIR_CPP-COMMONS=$PWD
#
# That makes FetchContent skip the network and use $PWD instead.

FetchContent_MakeAvailable(cpp-commons)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE commons::commons)
