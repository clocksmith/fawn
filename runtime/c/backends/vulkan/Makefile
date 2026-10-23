CC ?= cc
CXX ?= c++
AR ?= ar
OUT ?= /tmp/doe-c-vulkan
REPO ?= ../../../..
CFLAGS ?= -O2 -g
STRICT = -std=c11 -Wall -Wextra -Werror -Wpedantic
SOURCES = src/loader.c src/resources.c src/pipeline.c src/descriptors.c src/commands.c src/textures.c src/texture_commands.c src/render_pipeline.c src/render_commands.c src/queries.c
OBJECTS = $(patsubst src/%.c,$(OUT)/%.o,$(SOURCES))
HEADERS = $(wildcard src/*.h include/*.h)
.PHONY: all check abi native native-descriptors native-storage native-render native-queries native-texture probe sanitize clean
all: $(OUT)/libdoe_vk.a
$(OUT):
	mkdir -p $(OUT)
$(OUT)/%.o: src/%.c $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) -c $< -o $@
$(OUT)/libdoe_vk.a: $(OBJECTS)
	$(AR) rcs $@ $^
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
	python3 tools/check_abi.py --repo $(REPO) --cc $(CC)
$(OUT)/query_contract: tests/query_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/render_contract: tests/render_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/loader_contract: tests/loader_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/descriptor_contract: tests/descriptor_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/texture_contract: tests/texture_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
check: abi $(OUT)/ownership $(OUT)/texture_contract $(OUT)/descriptor_contract $(OUT)/loader_contract $(OUT)/render_contract $(OUT)/query_contract
	$(OUT)/ownership
	$(OUT)/loader_contract
	$(OUT)/render_contract
	$(OUT)/query_contract
	$(OUT)/texture_contract
	$(OUT)/descriptor_contract
	printf '#include "include/doe_vk.h"\n#include "include/doe_vk_texture.h"\n#include "include/doe_vk_bindings.h"\n#include "include/doe_vk_render.h"\n#include "include/doe_vk_queries.h"\n' | $(CXX) -std=c++17 -Wall -Wextra -Werror -Wpedantic -I. -x c++ -c -o $(OUT)/header-cxx.o -
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
	rm -f $(OBJECTS) $(OUT)/libdoe_vk.a $(OUT)/ownership $(OUT)/native_compute $(OUT)/probe $(OUT)/native_texture $(OUT)/native_descriptors $(OUT)/native_storage $(OUT)/native_render $(OUT)/texture_contract $(OUT)/descriptor_contract $(OUT)/descriptor_contract-sanitize $(OUT)/texture_contract-sanitize $(OUT)/ownership-sanitize $(OUT)/header-cxx.o $(OUT)/loader_contract $(OUT)/loader_contract-sanitize $(OUT)/render_contract $(OUT)/render_contract-sanitize $(OUT)/query_contract $(OUT)/query_contract-sanitize $(OUT)/native_queries
