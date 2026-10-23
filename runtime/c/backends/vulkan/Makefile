CC ?= cc
CXX ?= c++
AR ?= ar
OUT ?= /tmp/doe-c-vulkan
REPO ?= ../../../..
CFLAGS ?= -O2 -g
STRICT = -std=c11 -Wall -Wextra -Werror -Wpedantic
SOURCES = src/loader.c src/device_features.c src/resources.c src/pipeline.c src/descriptors.c src/commands.c src/textures.c src/texture_commands.c src/render_pipeline.c src/render_commands.c src/queries.c src/timestamp.c
OBJECTS = $(patsubst src/%.c,$(OUT)/%.o,$(SOURCES))
HEADERS = $(wildcard src/*.h include/*.h) $(REPO)/runtime/c/common/include/doe_copy_footprint.h
.PHONY: all check abi native native-descriptors native-storage native-render native-queries native-texture probe sanitize clean
all: $(OUT)/libdoe_vk.a
$(OUT):
	mkdir -p $(OUT)
$(OUT)/%.o: src/%.c $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) -c $< -o $@
$(OUT)/libdoe_vk.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(OUT)/feature_contract: tests/feature_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/native_features: tests/native_features.c $(OUT)/libdoe_vk.a $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $< $(OUT)/libdoe_vk.a -ldl -o $@
.PHONY: native-features
native-features: $(OUT)/native_features
	$(OUT)/native_features $(NATIVE_ARGS)
$(OUT)/ownership: tests/ownership.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/native_compute: tests/native_compute.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/native_queries: tests/native_queries.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/native_render: tests/native_render.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/native_storage: tests/native_storage.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/native_descriptors: tests/native_descriptors.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/native_texture: tests/native_texture.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/probe: tests/probe.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
abi:
	python3 tools/check_timestamp_source.py --repo $(REPO)
	python3 tools/check_abi.py --repo $(REPO) --cc $(CC)
$(OUT)/timestamp_lifetime: tests/timestamp_lifetime.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/timestamp_contract: tests/timestamp_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/query_contract: tests/query_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/pass_contract: tests/pass_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/render_contract: tests/render_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/loader_contract: tests/loader_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/descriptor_contract: tests/descriptor_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/texture_contract: tests/texture_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
check: abi $(OUT)/feature_contract $(OUT)/pass_contract $(OUT)/timestamp_lifetime $(OUT)/timestamp_contract $(OUT)/ownership $(OUT)/texture_contract $(OUT)/descriptor_contract $(OUT)/loader_contract $(OUT)/render_contract $(OUT)/query_contract
	$(OUT)/feature_contract
	$(OUT)/ownership
	$(OUT)/loader_contract
	$(OUT)/render_contract
	$(OUT)/pass_contract
	$(OUT)/query_contract
	$(OUT)/timestamp_contract
	$(OUT)/timestamp_lifetime
	$(OUT)/texture_contract
	$(OUT)/descriptor_contract
	printf '#include "include/doe_vk.h"\n#include "include/doe_vk_texture.h"\n#include "include/doe_vk_bindings.h"\n#include "include/doe_vk_render.h"\n#include "include/doe_vk_queries.h"\n#include "include/doe_vk_timestamp.h"\n' | $(CXX) -std=c++17 -Wall -Wextra -Werror -Wpedantic -I. -x c++ -c -o $(OUT)/header-cxx.o -
probe: $(OUT)/probe
	$(OUT)/probe $(PROBE_ARGS)
native: $(OUT)/native_compute
	test -n "$(SPIRV)"
	$(OUT)/native_compute $(SPIRV) $(NATIVE_ARGS)
native-queries: $(OUT)/native_queries
	$(OUT)/native_queries $(NATIVE_ARGS)
native-render: $(OUT)/native_render
	test -n "$(VERTEX_SPIRV)" && test -n "$(FRAGMENT_SPIRV)"
	$(OUT)/native_render $(VERTEX_SPIRV) $(FRAGMENT_SPIRV) $(NATIVE_ARGS)
native-storage: $(OUT)/native_storage
	test -n "$(SPIRV)"
	$(OUT)/native_storage $(SPIRV) $(NATIVE_ARGS)
native-descriptors: $(OUT)/native_descriptors
	test -n "$(SPIRV)"
	$(OUT)/native_descriptors $(SPIRV) $(NATIVE_ARGS)
native-texture: $(OUT)/native_texture
	$(OUT)/native_texture $(NATIVE_ARGS)
sanitize: | $(OUT)
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/pass_contract.c -ldl -o $(OUT)/pass_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/pass_contract-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/timestamp_lifetime.c -ldl -o $(OUT)/timestamp_lifetime-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/timestamp_lifetime-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/timestamp_contract.c -ldl -o $(OUT)/timestamp_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/timestamp_contract-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/query_contract.c -ldl -o $(OUT)/query_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/query_contract-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/render_contract.c -ldl -o $(OUT)/render_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/render_contract-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/loader_contract.c -ldl -o $(OUT)/loader_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/loader_contract-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/ownership.c -ldl -o $(OUT)/ownership-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/ownership-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/texture_contract.c -ldl -o $(OUT)/texture_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/texture_contract-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/descriptor_contract.c -ldl -o $(OUT)/descriptor_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/descriptor_contract-sanitize
clean:
	rm -f $(OBJECTS) $(OUT)/libdoe_vk.a $(OUT)/ownership $(OUT)/native_compute $(OUT)/probe $(OUT)/native_texture $(OUT)/native_descriptors $(OUT)/native_storage $(OUT)/native_render $(OUT)/texture_contract $(OUT)/descriptor_contract $(OUT)/descriptor_contract-sanitize $(OUT)/texture_contract-sanitize $(OUT)/ownership-sanitize $(OUT)/header-cxx.o $(OUT)/loader_contract $(OUT)/loader_contract-sanitize $(OUT)/render_contract $(OUT)/render_contract-sanitize $(OUT)/query_contract $(OUT)/query_contract-sanitize $(OUT)/native_queries $(OUT)/timestamp_contract $(OUT)/timestamp_contract-sanitize $(OUT)/timestamp_lifetime $(OUT)/timestamp_lifetime-sanitize $(OUT)/native_timestamp $(OUT)/pass_contract $(OUT)/pass_contract-sanitize $(OUT)/native_matrix $(OUT)/feature_contract $(OUT)/native_features

# Compiler/shader dependencies belong only to this optional integration test.
SHADER_ROOT ?= $(REPO)/runtime/c/shader
COMPILER_ROOT ?= $(REPO)/runtime/c/compiler
SHADER_LIBRARY ?= $(SHADER_ROOT)/build/libdoe_shader.a
COMPILER_LIBRARY ?= $(COMPILER_ROOT)/build/libdoe_compiler.so
$(OUT)/native_timestamp: tests/native_timestamp.c $(OUT)/libdoe_vk.a $(SHADER_LIBRARY) $(COMPILER_LIBRARY)
	$(CC) $(STRICT) $(CFLAGS) -I$(SHADER_ROOT)/include -I$(COMPILER_ROOT)/include $< $(OUT)/libdoe_vk.a $(SHADER_LIBRARY) $(COMPILER_LIBRARY) -Wl,-rpath,$(abspath $(dir $(COMPILER_LIBRARY))) -ldl -o $@
.PHONY: native-timestamp
native-timestamp: $(OUT)/native_timestamp
	$(OUT)/native_timestamp $(NATIVE_ARGS)

# Direct ABI8 matrix reflection is not projected through the older shader ABI.
$(OUT)/native_matrix: tests/native_matrix.c $(OUT)/libdoe_vk.a $(COMPILER_LIBRARY)
	$(CC) $(STRICT) $(CFLAGS) -Iinclude -I$(COMPILER_ROOT)/include $< $(OUT)/libdoe_vk.a $(COMPILER_LIBRARY) -Wl,-rpath,$(abspath $(dir $(COMPILER_LIBRARY))) -ldl -o $@
.PHONY: native-matrix
native-matrix: $(OUT)/native_matrix
	$(OUT)/native_matrix tests/matrix_tail.wgsl $(NATIVE_ARGS)
