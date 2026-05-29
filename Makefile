# Convenience wrapper around CMake/CTest workflows for commons.
# Real build system is CMake — this just memorizes common invocations.

BUILD_DIR        ?= build
BUILD_TYPE       ?= Debug
JOBS             ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
GENERATOR        ?=
CMAKE            ?= cmake
CTEST            ?= ctest
LLVM_PROFDATA    ?= xcrun llvm-profdata
LLVM_COV         ?= xcrun llvm-cov

CMAKE_GEN_FLAG   := $(if $(GENERATOR),-G "$(GENERATOR)",)

.DEFAULT_GOAL := help

.PHONY: help
help:
	@echo "commons — make targets"
	@echo ""
	@echo "  make configure         Configure $(BUILD_DIR)/ ($(BUILD_TYPE))"
	@echo "  make build             Build everything in $(BUILD_DIR)/"
	@echo "  make test              Run ctest in $(BUILD_DIR)/ (base library, no forced integrations)"
	@echo "  make example           Run the commons_hello example"
	@echo "  make examples          Build and run every commons_* example (fails on any non-zero exit)"
	@echo "  make integrations      Configure+build+test in build-integrations/ with COMMONS_WITH_NLOHMANN_JSON=ON and COMMONS_WITH_ULID=ON"
	@echo "  make all               configure + build + test"
	@echo ""
	@echo "  make sanitize          Configure+build+test in build-san/ with ASan+UBSan"
	@echo "  make tidy              Configure+build in build-tidy/ with clang-tidy"
	@echo "  make tidy-fix          Like tidy, but let clang-tidy apply fixes in place"
	@echo "  make release           Configure+build in build-release/ (Release)"
	@echo "  make coverage          Configure+build+test in build-coverage/ with Clang coverage"
	@echo "  make docs              Configure+build Doxygen HTML in build-docs/"
	@echo ""
	@echo "  make format            Run clang-format -i over project sources"
	@echo "  make format-check      Verify formatting without writing"
	@echo ""
	@echo "  make ci                Run the full pre-push gate: format-check + tidy + test + sanitize + release + integrations"
	@echo ""
	@echo "  make clean             Remove $(BUILD_DIR)/"
	@echo "  make distclean         Remove all build-* directories"
	@echo ""
	@echo "Variables: BUILD_DIR=$(BUILD_DIR) BUILD_TYPE=$(BUILD_TYPE) JOBS=$(JOBS)"

.PHONY: configure
configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

.PHONY: build
build: configure
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

.PHONY: test
test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

.PHONY: example
example: build
	$(BUILD_DIR)/examples/commons_hello

.PHONY: examples
examples: build
	@set -e; \
	failures=""; \
	for ex in $(BUILD_DIR)/examples/commons_*; do \
		if [ -x "$$ex" ] && [ ! -d "$$ex" ]; then \
			name=$$(basename "$$ex"); \
			echo "=== $$name ==="; \
			if ! "$$ex" >/dev/null; then \
				echo "FAIL: $$name (exit $$?)"; \
				failures="$$failures $$name"; \
			fi; \
		fi; \
	done; \
	if [ -n "$$failures" ]; then \
		echo "Examples that failed:$$failures"; \
		exit 1; \
	fi

# Force every optional integration ON (fetches each dependency) and run the
# full suite, including the conditional integration tests/examples.
.PHONY: integrations
integrations:
	$(CMAKE) -S . -B build-integrations $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug \
		-DCOMMONS_WITH_NLOHMANN_JSON=ON -DCOMMONS_WITH_ULID=ON
	$(CMAKE) --build build-integrations -j $(JOBS)
	$(CTEST) --test-dir build-integrations --output-on-failure

.PHONY: all
all: test

.PHONY: ci
ci: format-check tidy test sanitize release integrations
	@echo ""
	@echo "ci: all checks passed"

.PHONY: sanitize
sanitize:
	$(CMAKE) -S . -B build-san $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCOMMONS_ENABLE_SANITIZERS=ON
	$(CMAKE) --build build-san -j $(JOBS)
	$(CTEST) --test-dir build-san --output-on-failure

.PHONY: tidy
tidy:
	$(CMAKE) -S . -B build-tidy $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCOMMONS_ENABLE_CLANG_TIDY=ON
	$(CMAKE) --build build-tidy -j $(JOBS)

.PHONY: tidy-fix
tidy-fix:
	$(CMAKE) -S . -B build-tidy $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Debug -DCOMMONS_ENABLE_CLANG_TIDY=ON -DCOMMONS_CLANG_TIDY_FIX=ON
	$(CMAKE) --build build-tidy -j $(JOBS)

.PHONY: release
release:
	$(CMAKE) -S . -B build-release $(CMAKE_GEN_FLAG) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build build-release -j $(JOBS)
	$(CTEST) --test-dir build-release --output-on-failure

.PHONY: coverage
coverage:
	$(CMAKE) -S . -B build-coverage $(CMAKE_GEN_FLAG) \
		-DCMAKE_BUILD_TYPE=Debug -DCOMMONS_ENABLE_COVERAGE=ON
	$(CMAKE) --build build-coverage -j $(JOBS)
	rm -f build-coverage/*.profraw build-coverage/commons.profdata
	LLVM_PROFILE_FILE="$(CURDIR)/build-coverage/commons-%p.profraw" \
		$(CTEST) --test-dir build-coverage --output-on-failure
	$(LLVM_PROFDATA) merge -sparse build-coverage/*.profraw \
		-o build-coverage/commons.profdata
	$(LLVM_COV) report build-coverage/tests/commons_tests \
		-instr-profile=build-coverage/commons.profdata \
		-ignore-filename-regex='(_deps|tests)/'
	$(LLVM_COV) show build-coverage/tests/commons_tests \
		-instr-profile=build-coverage/commons.profdata \
		-ignore-filename-regex='(_deps|tests)/' \
		-format=html -output-dir=build-coverage/coverage-html \
		-show-line-counts-or-regions
	@echo "HTML report: build-coverage/coverage-html/index.html"

.PHONY: docs
docs:
	$(CMAKE) -S . -B build-docs $(CMAKE_GEN_FLAG) -DCOMMONS_BUILD_DOCS=ON
	$(CMAKE) --build build-docs --target commons_docs -j $(JOBS)
	@echo "HTML report: build-docs/docs/html/index.html"

FORMAT_FILES := $(shell find include tests examples -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cc' \) 2>/dev/null)

.PHONY: format
format:
	@if [ -z "$(FORMAT_FILES)" ]; then echo "no source files found"; else clang-format -i $(FORMAT_FILES); fi

.PHONY: format-check
format-check:
	@if [ -z "$(FORMAT_FILES)" ]; then echo "no source files found"; else clang-format --dry-run --Werror $(FORMAT_FILES); fi

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

.PHONY: distclean
distclean:
	rm -rf build build-san build-tidy build-release build-coverage build-docs build-integrations cmake-build-*
