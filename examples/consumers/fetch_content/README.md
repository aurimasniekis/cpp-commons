# consumer — `FetchContent` integration demo

This is a **standalone downstream project** that depends on commons. It is not
built by the parent project; it exists to document the integration story
end-to-end.

The `CMakeLists.txt` in this directory pins the published release tarball with
its `URL_HASH` checksum — the most reproducible form (no git needed at configure
time, and the bytes are verified):

```cmake
FetchContent_Declare(
    cpp-commons
    URL      https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.0.tar.gz
    URL_HASH SHA256=7bfe899c0c2f25fe18f2526473b9cd7aee917ab8dd8d93eec64ee87fecd00ede
)
```

commons is a header-only INTERFACE target with no forced dependencies, so the
fetch is fast and nothing else is downloaded.

## Run it

```sh
cmake -S examples/consumers/fetch_content -B /tmp/commons-consumer-build
cmake --build /tmp/commons-consumer-build
/tmp/commons-consumer-build/consumer
```

Expected output (something close to):

```
commons version: 0.1.0
tag            : downstream
count          : 3
```

## Validate against a local checkout

To exercise the snippet against the in-tree source tree (skipping the network
fetch), override the source directory at configure time —
`FETCHCONTENT_SOURCE_DIR_<NAME>` makes `FetchContent` use the path you provide:

```sh
# from the repo root
cmake -S examples/consumers/fetch_content \
      -B /tmp/commons-consumer-build \
      -DCMAKE_BUILD_TYPE=Release \
      -DFETCHCONTENT_SOURCE_DIR_CPP-COMMONS=$PWD
cmake --build /tmp/commons-consumer-build
/tmp/commons-consumer-build/consumer
```
