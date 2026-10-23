CC ?= cc
CXX ?= c++
AR ?= ar
OUT ?= /tmp/doe-c-vulkan
REPO ?= ../../../..
CFLAGS ?= -O2 -g
STRICT = -std=c11 -Wall -Wextra -Werror -Wpedantic
SOURCES = src/loader.c src/resources.c src/pipeline.c src/commands.c src/textures.c src/texture_commands.c
OBJECTS = $(patsubst src/%.c,$(OUT)/%.o,$(SOURCES))
HEADERS = $(wildcard src/*.h include/*.h)
.PHONY: all check abi native probe sanitize clean
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
$(OUT)/native_texture: tests/native_texture.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
$(OUT)/probe: tests/probe.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
abi:
	python3 tools/check_abi.py --repo $(REPO) --cc $(CC)
$(OUT)/texture_contract: tests/texture_contract.c $(SOURCES) $(HEADERS) | $(OUT)
	$(CC) $(STRICT) $(CFLAGS) $(SOURCES) $< -ldl -o $@
check: abi $(OUT)/ownership $(OUT)/texture_contract
	$(OUT)/ownership
	$(OUT)/texture_contract
	printf '#include "include/doe_vk.h"\n#include "include/doe_vk_texture.h"\n' | $(CXX) -std=c++17 -Wall -Wextra -Werror -Wpedantic -I. -x c++ -c -o $(OUT)/header-cxx.o -
probe: $(OUT)/probe
	$(OUT)/probe $(PROBE_ARGS)
native: $(OUT)/native_compute
	test -n "$(SPIRV)"
	$(OUT)/native_compute $(SPIRV) $(NATIVE_ARGS)
native-texture: $(OUT)/native_texture
	$(OUT)/native_texture $(NATIVE_ARGS)
sanitize: | $(OUT)
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/ownership.c -ldl -o $(OUT)/ownership-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/ownership-sanitize
	$(CC) $(STRICT) -O1 -g -fsanitize=address,undefined $(SOURCES) tests/texture_contract.c -ldl -o $(OUT)/texture_contract-sanitize
	ASAN_OPTIONS=detect_leaks=0 $(OUT)/texture_contract-sanitize
clean:
	rm -f $(OBJECTS) $(OUT)/libdoe_vk.a $(OUT)/ownership $(OUT)/native_compute $(OUT)/probe $(OUT)/native_texture $(OUT)/texture_contract $(OUT)/texture_contract-sanitize $(OUT)/ownership-sanitize $(OUT)/header-cxx.o
