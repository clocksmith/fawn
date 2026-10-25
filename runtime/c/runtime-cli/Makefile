# SPDX-License-Identifier: MIT
# doe-c-runtime: the C command-stream runtime CLI over libwebgpu_doe_c.
CC = gcc
PYTHON ?= python3
include $(dir $(lastword $(MAKEFILE_LIST)))../build-root.mk
BUILD ?= $(DOE_C_TMP)/runtime-cli
NATIVE_BUILD ?= $(BUILD)/native
NATIVE_LIBRARY_NAME := $(shell $(MAKE) --no-print-directory -s -C ../native native-library-name)
NATIVE_LIBRARY = $(NATIVE_BUILD)/$(NATIVE_LIBRARY_NAME)
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc -I../../zig/vendor/webgpu-headers
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread
CORE = src/json.c src/stream.c src/trace.c src/sha256.c
CORE_HEADERS = src/json.h src/stream.h src/trace.h src/sha256.h
RUNTIME = src/execute.c src/kernel.c
RUNTIME_HEADERS = src/execute.h src/runtime_internal.h ../../zig/vendor/webgpu-headers/webgpu.h
.PHONY: all bench-bin check check-host native
all: $(BUILD)/doe-c-runtime
$(BUILD):
	mkdir -p $@
# The C runtime library is built by its owner; this owner only links it.
native:
	$(MAKE) --no-print-directory -C ../native BUILD='$(abspath $(NATIVE_BUILD))' all
$(NATIVE_LIBRARY): native
$(BUILD)/doe-c-runtime: src/main.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c $(CORE) $(RUNTIME) -L'$(abspath $(NATIVE_BUILD))' \
	  -l:$(NATIVE_LIBRARY_NAME) '-Wl,-rpath,$(abspath $(NATIVE_BUILD))' -lm -o $@
# The bench executor doe_c_direct_vulkan runs bin/doe-c-runtime (gitignored);
# the copy keeps its rpath to $(NATIVE_BUILD), which must stay in place.
bench-bin: $(BUILD)/doe-c-runtime
	mkdir -p bin
	cp '$(BUILD)/doe-c-runtime' bin/doe-c-runtime
$(BUILD)/test_json: tests/test_json.c src/json.c src/json.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_json.c src/json.c -lm -o $@
$(BUILD)/test_stream: tests/test_stream.c $(CORE) $(CORE_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_stream.c $(CORE) -lm -o $@
$(BUILD)/test_sha256: tests/test_sha256.c src/sha256.c src/sha256.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_sha256.c src/sha256.c -o $@
$(BUILD)/test_trace: tests/test_trace.c $(CORE) $(CORE_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_trace.c $(CORE) -lm -o $@
# Host-only: parsing, rejection, the trace hash chain and SHA-256; no device.
check-host: $(BUILD)/test_json $(BUILD)/test_stream $(BUILD)/test_trace $(BUILD)/test_sha256
	$(BUILD)/test_json
	$(BUILD)/test_stream ../../../examples
	$(BUILD)/test_trace
	$(BUILD)/test_sha256
# Executes example streams on the selected Vulkan adapter (a CPU ICD such as
# lavapipe by default) and checks the artifacts with pipeline/trace/replay.py.
check: check-host $(BUILD)/doe-c-runtime
	$(PYTHON) tests/test_cli.py --binary '$(BUILD)/doe-c-runtime' --work '$(BUILD)/cli-work'
