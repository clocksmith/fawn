# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
CC = gcc
CXX = g++
export TMPDIR := $(CURDIR)/build/tmp
export PYTHONDONTWRITEBYTECODE := 1
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wswitch-enum -Werror
CXXFLAGS = -std=c++17 -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
LIB_SOURCES = src/lex.c src/parse.c src/validate.c src/spirv.c src/compile.c
HEADERS = src/internal.h include/doe_compiler.h
SPIRV_VAL ?= spirv-val
.PHONY: all test sanitize clean
all: build/doe-compiler-c build/libdoe_compiler.so
build/.ready:
	mkdir -p build/tmp build/logs
	touch $@
build/doe-compiler-c: $(LIB_SOURCES) src/cli.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) src/cli.c -o $@
build/libdoe_compiler.so: $(LIB_SOURCES) $(HEADERS) src/exports.map | build/.ready
	$(CC) $(CFLAGS) -fPIC -shared $(LIB_SOURCES) -Wl,--version-script=src/exports.map -o $@
build/test_api: $(LIB_SOURCES) tests/test_api.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_api.c -o $@
build/test_boundaries: $(LIB_SOURCES) tests/test_boundaries.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) -pthread $(LIB_SOURCES) tests/test_boundaries.c -o $@
build/test_constants: $(LIB_SOURCES) tests/test_constants.c $(HEADERS) | build/.ready
	$(CC) $(CFLAGS) $(LIB_SOURCES) tests/test_constants.c -o $@
build/native_consumer: tests/native_consumer.c include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/native_consumer.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
build/test_layout: tests/test_layout.c include/doe_compiler.h | build/.ready
	$(CC) $(CFLAGS) tests/test_layout.c -o $@
build/cpp_consumer: tests/cpp_consumer.cpp include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CXX) $(CXXFLAGS) tests/cpp_consumer.cpp -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -o $@
build/interposition_consumer: tests/interposition_consumer.c include/doe_compiler.h build/libdoe_compiler.so | build/.ready
	$(CC) $(CFLAGS) tests/interposition_consumer.c -Lbuild -ldoe_compiler -Wl,-rpath,'$$ORIGIN' -Wl,--export-dynamic -o $@
test: all build/interposition_consumer build/test_api build/test_boundaries build/test_constants build/native_consumer build/test_layout build/cpp_consumer
	build/test_api
	build/test_boundaries
	build/test_constants
	build/test_layout
	build/cpp_consumer
	build/interposition_consumer
	python3 tests/test_exports.py
	build/native_consumer ../../../bench/inference-pipeline/kernels/residual.wgsl
	SPIRV_VAL="$(SPIRV_VAL)" python3 tests/test_cli.py
sanitize: | build/.ready
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_api.c -o build/test_api_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_api_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -pthread $(LIB_SOURCES) tests/test_boundaries.c -o build/test_boundaries_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_boundaries_sanitize
	$(CC) $(CFLAGS) -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined $(LIB_SOURCES) tests/test_constants.c -o build/test_constants_sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test_constants_sanitize
clean:
	rm -rf build
