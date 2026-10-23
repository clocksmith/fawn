# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
CC = gcc
CXX = g++
export TMPDIR := $(CURDIR)/build/tmp
export PYTHONDONTWRITEBYTECODE := 1
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum -Werror
CXXFLAGS = -std=c++17 -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
LIB_SOURCES = src/numeric.c src/lex.c src/parse.c src/validate.c src/reflection.c src/spirv.c src/compile.c
HEADERS = src/internal.h include/doe_compiler.h include/doe_compiler_v2.h src/cli_json.h
SPIRV_VAL ?= spirv-val
CLI_FAULT_FLAGS = -Wl,--wrap=write,--wrap=close,--wrap=link,--wrap=mkstemp,--wrap=malloc,--wrap=free,--wrap=doe_compiler_workspace_requirements
.PHONY: all test test-cli-modes test-uniform sanitize sanitize-cli sanitize-uniform clean
all: build/doe-compiler-c build/libdoe_compiler.so
build/.ready:
	mkdir -p build/tmp build/logs
	touch $@
build/doe-compiler-c: $(LIB_SOURCES) src/cli.c src/cli_json.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) src/cli.c src/cli_json.c -o $@
build/test_cli_faults: $(LIB_SOURCES) src/cli.c src/cli_json.c tests/cli_io_faults.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) src/cli.c src/cli_json.c tests/cli_io_faults.c $(CLI_FAULT_FLAGS) -o $@
test-cli-modes: all build/test_cli_faults
	cd ../../.. && SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_cli_modes
build/libdoe_compiler.so: $(LIB_SOURCES) $(HEADERS) src/exports.map | build/.ready
	$(CC) $(CFLAGS) -fPIC -shared $(LIB_SOURCES) -Wl,--version-script=src/exports.map -o $@
build/test_api: $(LIB_SOURCES) tests/test_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_api.c -o $@
build/test_boundaries: $(LIB_SOURCES) tests/test_boundaries.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_boundaries.c -o $@
build/test_constants: $(LIB_SOURCES) tests/test_constants.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_constants.c -o $@
build/test_expression_api: $(LIB_SOURCES) tests/test_expression_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_expression_api.c -lm -o $@
build/test_control_api: $(LIB_SOURCES) tests/test_control_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_control_api.c -o $@
build/native_consumer: tests/native_consumer.c include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/native_consumer.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
build/test_layout: tests/test_layout.c include/doe_compiler.h | build/.ready
	$(CC) $(CFLAGS) tests/test_layout.c -o $@
build/cpp_consumer: tests/cpp_consumer.cpp include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/cpp_consumer.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
build/interposition_consumer: tests/interposition_consumer.c include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/interposition_consumer.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -Wl,--export-dynamic -o $@
test: all test-compute test-uniform test-cli-modes build/interposition_consumer build/test_api build/test_boundaries build/test_constants build/test_control_api build/test_expression_api build/native_consumer build/test_layout build/cpp_consumer
	build/test_api
	build/test_boundaries
	build/test_constants
	build/test_control_api
	build/test_expression_api
	build/test_layout
	build/cpp_consumer
	build/interposition_consumer
	python3 tests/test_exports.py
	build/native_consumer ../../../bench/inference-pipeline/kernels/residual.wgsl
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_cli.py
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_control.py
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_expressions.py
sanitize: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_api.c -o build/test_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_api_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_boundaries.c -o build/test_boundaries_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_boundaries_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_constants.c -o build/test_constants_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_constants_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_control_api.c -o build/test_control_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_control_api_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_expression_api.c -lm -o build/test_expression_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_expression_api_sanitize
sanitize-cli: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) src/cli.c src/cli_json.c -o build/doe-compiler-c-sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) src/cli.c src/cli_json.c tests/cli_io_faults.c $(CLI_FAULT_FLAGS) -o build/test_cli_faults_sanitize
	cd ../../.. && ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c-sanitize" DOE_COMPILER_FAULT_CLI="$(CURDIR)/build/test_cli_faults_sanitize" SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_cli_modes
clean:
	rm -rf build

build/test_uniform_api: $(LIB_SOURCES) tests/test_uniform_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_uniform_api.c -o $@
build/cpp_consumer_v2: tests/cpp_consumer_v2.cpp include/doe_compiler_v2.h build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/cpp_consumer_v2.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
test-uniform: all build/test_uniform_api build/cpp_consumer_v2 build/native_consumer_v2 build/test_layout_v2
	build/test_uniform_api
	build/cpp_consumer_v2
	build/test_layout_v2
	build/native_consumer_v2 ../../../bench/kernels/package_vector_scale_add.wgsl
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_uniforms.py
sanitize-uniform: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_uniform_api.c -o build/test_uniform_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_uniform_api_sanitize

build/native_consumer_v2: tests/native_consumer_v2.c include/doe_compiler_v2.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/native_consumer_v2.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
build/test_layout_v2: tests/test_layout_v2.c include/doe_compiler_v2.h | build/.ready
	$(CC) $(CFLAGS) tests/test_layout_v2.c -o $@

.PHONY: test-compute
test-compute: all build/test_compute_api
	build/test_compute_api
	cd ../../.. && SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_compute

build/test_compute_api: $(LIB_SOURCES) tests/test_compute_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_compute_api.c -o $@
.PHONY: sanitize-compute
sanitize-compute: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_compute_api.c -o build/test_compute_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_compute_api_sanitize

	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) src/cli.c src/cli_json.c -o build/doe-compiler-c-compute-sanitize
	cd ../../.. && ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 DOE_COMPILER_CLI="$(CURDIR)/build/doe-compiler-c-compute-sanitize" SPIRV_VAL="$(SPIRV_VAL)" python3 -m unittest runtime.c.compiler.tests.test_compute
