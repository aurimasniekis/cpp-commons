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

# Pin a published release tarball + its SHA256. Reproducible, no git required
# at configure time, fastest to fetch on CI. To compute SHA256 yourself:
#
#   curl -sL https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.0.tar.gz \
#     | shasum -a 256
#
# (No URL_HASH below until a release tag exists — the CI consumer job overrides
# the source dir with the in-tree checkout, so this snippet is exercised before
# any tag is published. See README.md.)
FetchContent_Declare(
        cpp-commons
        GIT_REPOSITORY https://github.com/aurimasniekis/cpp-commons.git
        GIT_TAG        v0.1.0
        GIT_SHALLOW    TRUE
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
