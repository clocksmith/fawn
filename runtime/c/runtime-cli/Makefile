# SPDX-License-Identifier: MIT
# doe-c-runtime: the C command-stream runtime CLI over libwebgpu_doe_c.
CC = gcc
.DEFAULT_GOAL := all
PYTHON ?= python3
include $(dir $(lastword $(MAKEFILE_LIST)))../build-root.mk
BUILD ?= $(DOE_C_TMP)/runtime-cli
NATIVE_BUILD ?= $(BUILD)/native
NATIVE_LIBRARY_NAME := $(shell $(MAKE) --no-print-directory -s -C ../native native-library-name)
NATIVE_LIBRARY = $(NATIVE_BUILD)/$(NATIVE_LIBRARY_NAME)
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc -I../../zig/vendor/webgpu-headers -I../native/include
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -ffp-contract=off
include sources.mk
CORE = $(CLI_CORE) $(BUILD)/quirk_toggles.c
CORE_HEADERS = src/host_io.h src/json.h src/stream.h src/stream_internal.h src/trace.h src/sha256.h src/quirks.h \
  src/operators.h src/numeric_policy.h src/numeric_stability.h src/numeric_eval.h src/numeric_annotation.h src/plan.h
TOGGLE_REGISTRY = ../../../config/quirk-toggle-registry.json
RUNTIME = $(CLI_RUNTIME) $(SURFACE_HOST)
ifeq ($(shell uname -s),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
SURFACE_HOST = $(BUILD)/surface_metal.o
DAWN_BACKEND_ID = dawn_c_metal
LDLIBS += -framework AppKit -framework QuartzCore
$(BUILD)/surface_metal.o: src/surface_metal.m src/surface_platform.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
else
SURFACE_HOST = src/surface_xcb.c
DAWN_BACKEND_ID = dawn_direct_vulkan
DAWN_SURFACE_CPPFLAGS = -DDOE_SURFACE_XLIB
DAWN_SURFACE_LIBS = -lX11-xcb -lX11
LDLIBS += -lxcb
endif
RUNTIME_HEADERS = src/cli_options.h src/cli_inputs.h src/cli_output.h src/render_public.h src/surface_platform.h src/execute.h src/runtime_internal.h ../../zig/vendor/webgpu-headers/webgpu.h \
  ../native/include/doe_native_adapter.h
.PHONY: all bench-bin bench-bin-dawn check check-dawn check-host dawn native
all: $(BUILD)/doe-c-runtime
$(BUILD):
	mkdir -p $@
# The toggle effect table, generated from the registry doe-zig-runtime embeds.
$(BUILD)/quirk_toggles.c: tools/gen_toggle_registry.py $(TOGGLE_REGISTRY) | $(BUILD)
	$(PYTHON) tools/gen_toggle_registry.py $(TOGGLE_REGISTRY) $@
# The C runtime library is built by its owner; this owner only links it.
native:
	$(MAKE) --no-print-directory -C ../native BUILD='$(abspath $(NATIVE_BUILD))' all
$(NATIVE_LIBRARY): native
$(BUILD)/doe-c-runtime: src/main.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c $(CORE) $(RUNTIME) -L'$(abspath $(NATIVE_BUILD))' \
	  '$(abspath $(NATIVE_LIBRARY))' '-Wl,-rpath,$(abspath $(NATIVE_BUILD))' $(LDLIBS) -lm -o $@
# The same sources over Dawn's webgpu.h library (built by the bench vendor
# tree, not here): dawn-c-runtime writes executionBackend dawn_direct_vulkan.
DAWN_LIBRARY ?= $(abspath ../../../bench/vendor/dawn/out/Release/libwebgpu_dawn.so)
$(BUILD)/dawn-c-runtime: src/main.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) | $(BUILD)
	@test -f '$(DAWN_LIBRARY)' || { echo "dawn-c-runtime: DAWN_LIBRARY $(DAWN_LIBRARY) does not exist" >&2; exit 2; }
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DAWN_SURFACE_CPPFLAGS) -DDOE_RUNTIME_DAWN -DDOE_RUNTIME_MODULE='"dawn-c-runtime"' -DDOE_RUNTIME_BACKEND_ID='"$(DAWN_BACKEND_ID)"' \
	  src/main.c $(CORE) $(RUNTIME) '$(DAWN_LIBRARY)' '-Wl,-rpath,$(dir $(DAWN_LIBRARY))' $(DAWN_SURFACE_LIBS) $(LDLIBS) -lm -o $@
dawn: $(BUILD)/dawn-c-runtime
# The bench executor dawn_direct_vulkan runs bin/dawn-c-runtime (gitignored).
bench-bin-dawn: $(BUILD)/dawn-c-runtime
	mkdir -p bin
	cp '$(BUILD)/dawn-c-runtime' bin/dawn-c-runtime
# The whole CLI suite over Dawn: the same host references and Zig hash chains.
check-dawn: check-host $(BUILD)/dawn-c-runtime
	$(PYTHON) tests/test_cli.py --binary '$(BUILD)/dawn-c-runtime' --work '$(BUILD)/dawn-cli-work' \
	  --module dawn-c-runtime --backend-id dawn_direct_vulkan
	$(PYTHON) tests/test_cli_diagnostics.py --binary '$(BUILD)/dawn-c-runtime' --work '$(BUILD)/dawn-diagnostics-work'
	$(PYTHON) tests/test_cli_queue.py --binary '$(BUILD)/dawn-c-runtime' --work '$(BUILD)/dawn-queue-work'
# The bench executor doe_c_direct_vulkan runs bin/doe-c-runtime (gitignored);
# the copy keeps its rpath to $(NATIVE_BUILD), which must stay in place.
bench-bin: $(BUILD)/doe-c-runtime
	mkdir -p bin
	cp '$(BUILD)/doe-c-runtime' bin/doe-c-runtime
$(BUILD)/test_json: tests/test_json.c src/json.c src/json.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_json.c src/json.c $(LDLIBS) -lm -o $@
$(BUILD)/test_stream: tests/test_stream.c $(CORE) $(CORE_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_stream.c $(CORE) $(LDLIBS) -lm -o $@
$(BUILD)/test_sha256: tests/test_sha256.c src/sha256.c src/sha256.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_sha256.c src/sha256.c -o $@
$(BUILD)/test_quirks: tests/test_quirks.c $(CORE) $(CORE_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_quirks.c $(CORE) $(LDLIBS) -lm -o $@
$(BUILD)/test_numeric: tests/test_numeric.c $(CORE) $(CORE_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_numeric.c $(CORE) $(LDLIBS) -lm -o $@
$(BUILD)/test_trace: tests/test_trace.c $(CORE) $(CORE_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_trace.c $(CORE) $(LDLIBS) -lm -o $@
$(BUILD)/test_map: tests/test_map.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_map.c $(CORE) $(RUNTIME) -L'$(abspath $(NATIVE_BUILD))' \
	  '$(abspath $(NATIVE_LIBRARY))' '-Wl,-rpath,$(abspath $(NATIVE_BUILD))' $(LDLIBS) -lm -o $@
$(BUILD)/test_texture_cleanup: tests/test_texture_cleanup.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_texture_cleanup.c $(CORE) $(RUNTIME) -L'$(abspath $(NATIVE_BUILD))' \
	  '$(abspath $(NATIVE_LIBRARY))' '-Wl,-rpath,$(abspath $(NATIVE_BUILD))' $(LDLIBS) -lm \
	  -Wl,--wrap=wgpuTextureCreateView,--wrap=wgpuTextureViewRelease,--wrap=wgpuDeviceCreateBuffer -o $@
# Host-only: parsing, rejection, the trace hash chain and SHA-256; no device.
check-host: $(BUILD)/test_json $(BUILD)/test_stream $(BUILD)/test_trace $(BUILD)/test_sha256 $(BUILD)/test_quirks \
  $(BUILD)/test_numeric
	$(BUILD)/test_json
	$(BUILD)/test_stream ../../../examples
	$(BUILD)/test_trace
	$(BUILD)/test_sha256
	$(BUILD)/test_quirks ../../../examples
	$(BUILD)/test_numeric ../../.. '$(BUILD)'
# Executes example streams on the selected Vulkan adapter (a CPU ICD such as
# lavapipe by default) and checks the artifacts with pipeline/trace/replay.py.
check: check-queue check-diagnostics check-host $(BUILD)/doe-c-runtime $(BUILD)/test_map $(BUILD)/test_texture_cleanup
	$(BUILD)/test_map
	$(BUILD)/test_texture_cleanup ../../../bench/kernels
	$(PYTHON) tests/test_cli.py --binary '$(BUILD)/doe-c-runtime' --work '$(BUILD)/cli-work'

check-diagnostics: $(BUILD)/doe-c-runtime $(BUILD)/test_diagnostics
	$(BUILD)/test_diagnostics
	$(PYTHON) tests/test_cli_diagnostics.py --binary '$(BUILD)/doe-c-runtime' --work '$(BUILD)/diagnostics-work'

$(BUILD)/test_diagnostics: tests/test_diagnostics.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_diagnostics.c $(CORE) $(RUNTIME) -L'$(abspath $(NATIVE_BUILD))' \
	  '$(abspath $(NATIVE_LIBRARY))' '-Wl,-rpath,$(abspath $(NATIVE_BUILD))' $(LDLIBS) -lm \
	  -Wl,--wrap=wgpuShaderModuleGetCompilationInfo,--wrap=wgpuDeviceCreateRenderPipelineAsync,--wrap=wgpuCommandEncoderFinish \
	  -Wl,--wrap=wgpuRenderPassEncoderDraw,--wrap=wgpuQueueSubmit -o $@

# Requires a reachable real display; no skip-as-pass fallback.
check-surface: $(BUILD)/doe-c-runtime
	$(PYTHON) tests/test_cli_surface.py --binary '$(BUILD)/doe-c-runtime' --work '$(BUILD)/surface-work'
check-surface-dawn: $(BUILD)/dawn-c-runtime
	$(PYTHON) tests/test_cli_surface.py --binary '$(BUILD)/dawn-c-runtime' --work '$(BUILD)/dawn-surface-work'

$(BUILD)/test_queue: tests/test_queue.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) $(NATIVE_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_queue.c $(CORE) $(RUNTIME) -L'$(abspath $(NATIVE_BUILD))' \
	  '$(abspath $(NATIVE_LIBRARY))' '-Wl,-rpath,$(abspath $(NATIVE_BUILD))' $(LDLIBS) -lm \
	  -Wl,--wrap=wgpuQueueOnSubmittedWorkDone,--wrap=wgpuInstanceProcessEvents,--wrap=wgpuInstanceWaitAny -o $@
check-queue: $(BUILD)/test_queue $(BUILD)/doe-c-runtime
	$(BUILD)/test_queue
	$(PYTHON) tests/test_cli_queue.py --binary '$(BUILD)/doe-c-runtime' --work '$(BUILD)/queue-work'

# Linux loader interposition proves the host path never creates a native instance.
$(BUILD)/no_device.so: tests/no_device.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC $< -o $@
check-host-modes: $(BUILD)/doe-c-runtime $(BUILD)/no_device.so
	$(PYTHON) tests/test_cli_host_modes.py --binary '$(BUILD)/doe-c-runtime' --no-device '$(BUILD)/no_device.so' \
	  --work '$(BUILD)/host-modes-work'
ifeq ($(shell uname -s),Linux)
check: check-host-modes
endif

$(BUILD)/test_public_render_dawn: tests/test_public_render.c $(CORE) $(RUNTIME) $(CORE_HEADERS) $(RUNTIME_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_public_render.c $(CORE) $(RUNTIME) '$(DAWN_LIBRARY)' \
	  '-Wl,-rpath,$(dir $(DAWN_LIBRARY))' $(LDLIBS) -lm \
	  -Wl,--wrap=wgpuRenderPassEncoderSetPipeline,--wrap=wgpuRenderPassEncoderSetBindGroup \
	  -Wl,--wrap=wgpuRenderBundleEncoderSetPipeline,--wrap=wgpuRenderBundleEncoderSetBindGroup \
	  -Wl,--wrap=wgpuRenderPassEncoderExecuteBundles,--wrap=wgpuRenderBundleEncoderFinish \
	  -Wl,--wrap=wgpuCommandEncoderCopyTextureToTexture -o $@
check-public-render: $(BUILD)/test_public_render_dawn $(BUILD)/dawn-c-runtime $(BUILD)/doe-c-runtime
	$(BUILD)/test_public_render_dawn
	$(PYTHON) tests/test_cli_public_render.py --binary '$(BUILD)/dawn-c-runtime' --native '$(BUILD)/doe-c-runtime' \
	  --work '$(BUILD)/public-render-work'

$(BUILD)/test_host_io: tests/test_host_io.c src/host_io.c src/host_io.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_host_io.c src/host_io.c -o $@
$(BUILD)/test_host_io_file: tests/test_host_io.c src/host_io.c src/host_io.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDOE_HOST_FILE_OUTPUT tests/test_host_io.c src/host_io.c -o $@
check-host-io: $(BUILD)/test_host_io $(BUILD)/test_host_io_file
	$(BUILD)/test_host_io
	$(BUILD)/test_host_io_file
check-host: check-host-io
