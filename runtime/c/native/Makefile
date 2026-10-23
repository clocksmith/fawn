CC = gcc
CXX = g++
BUILD ?= /tmp/doe-c-native
COMPILER_LIBRARY ?= $(BUILD)/libdoe_compiler.so
SANITIZE ?=
# One owner list drives source, public include and private header dependencies.
OWNER_DIRS = ../instance ../futures ../callbacks ../resources ../labels \
 ../error-scopes ../shader ../backends/vulkan ../backends/metal
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -DWGPU_SHARED_LIBRARY -DWGPU_IMPLEMENTATION \
 -I../../zig/vendor/webgpu-headers -I../compiler/include $(addprefix -I,$(addsuffix /include,$(OWNER_DIRS)))
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -fPIC -pthread $(SANITIZE)
SOURCES = $(wildcard src/*.c) $(foreach owner,$(OWNER_DIRS),$(wildcard $(owner)/src/*.c))
OBJECTS = $(patsubst src/%.c,$(BUILD)/native/%.o,$(filter src/%,$(SOURCES))) \
 $(patsubst ../%.c,$(BUILD)/owners/%.o,$(filter ../%,$(SOURCES)))
HEADERS = $(wildcard src/*.h ../compiler/include/*.h ../common/include/*.h) \
 $(foreach owner,$(OWNER_DIRS),$(wildcard $(owner)/include/*.h $(owner)/src/*.h)) \
 ../../zig/vendor/webgpu-headers/webgpu.h
COMPILER_RUNPATH ?= $(abspath $(dir $(COMPILER_LIBRARY)))
COMPILER_LINK = -L$(abspath $(dir $(COMPILER_LIBRARY))) -l:$(notdir $(COMPILER_LIBRARY)) '-Wl,-rpath,$(COMPILER_RUNPATH)'
.PHONY: all check check-build-dependencies check-empty-module clean
all: $(BUILD)/libwebgpu_doe_c.so
$(BUILD):
	mkdir -p $@
.PHONY: compiler-shared
compiler-shared:
$(BUILD)/libdoe_compiler.so: compiler-shared | $(BUILD)
	$(MAKE) -C ../compiler shared SHARED_LIBRARY="$(abspath $@)" \
	  TMPDIR="$(abspath $(BUILD))/tmp" EXTRA_CFLAGS="$(SANITIZE)"
$(BUILD)/native/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden -c $< -o $@
$(BUILD)/owners/%.o: ../%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden -c $< -o $@
$(BUILD)/libwebgpu_doe_c.so: $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CFLAGS) -shared -Wl,-z,defs -Wl,-Bsymbolic-functions $(OBJECTS) $(COMPILER_LINK) -ldl -o $@
$(BUILD)/backend-selection: tests/backend_selection.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/metal-bridge: tests/metal_bridge.c tests/consumer.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/metal_bridge.c ../backends/metal/tests/mock_native.c $(OBJECTS) $(COMPILER_LINK) -ldl \
	  -Wl,--wrap=doe_mtl_context_create -Wl,--wrap=doe_mtl_context_device -Wl,--wrap=doe_mtl_commands_create \
	  -Wl,--wrap=doe_mtl_commands_wait -Wl,--wrap=doe_mtl_buffer_create -o $@
$(BUILD)/indirect-render: tests/indirect_render.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/indirect-compute: tests/indirect_compute.c tests/consumer.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/indirect_compute.c $(OBJECTS) $(COMPILER_LINK) -ldl -Wl,--wrap=doe_vk_dispatch_validator_create -o $@
$(BUILD)/compute: tests/compute.c $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/atomics: tests/atomics.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/debug: tests/debug.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/msaa: tests/msaa.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/depth: tests/depth.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/demote: tests/demote.c tests/consumer.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/demote.c $(OBJECTS) $(COMPILER_LINK) -ldl -Wl,--wrap=doe_vk_context_create -o $@
$(BUILD)/graphics-admission: tests/graphics_admission.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/graphics: tests/graphics.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/textures: tests/textures.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/admission: tests/admission.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/failures: tests/failures.c tests/consumer.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/failures.c $(OBJECTS) $(COMPILER_LINK) -ldl -Wl,--wrap=doe_vk_buffer_create -Wl,--wrap=doe_vk_pipeline_create -Wl,--wrap=doe_vk_render_pipeline_create -Wl,--wrap=doe_vk_timestamp_normalizer_create -o $@
$(BUILD)/timestamps: tests/timestamps.c tests/consumer.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/timestamps.c $(OBJECTS) $(COMPILER_LINK) -ldl -o $@
$(BUILD)/recovery: tests/recovery.c tests/consumer.h $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/recovery.c $(OBJECTS) $(COMPILER_LINK) -ldl -Wl,--wrap=doe_vk_commands_submit -Wl,--wrap=doe_vk_commands_recover -Wl,--wrap=doe_vk_commands_release -o $@
$(BUILD)/empty-shader: tests/empty_shader.c tests/consumer.h $(BUILD)/libwebgpu_doe_c.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
$(BUILD)/lifecycle: tests/lifecycle.c $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/lifecycle.c $(OBJECTS) $(COMPILER_LINK) -ldl -o $@
$(BUILD)/procedures-cxx: tests/procedures.cpp $(BUILD)/libwebgpu_doe_c.so
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Wpedantic -Werror $(SANITIZE) $< -L$(BUILD) -lwebgpu_doe_c -Wl,-rpath,$(abspath $(BUILD)) -o $@
check: $(BUILD)/indirect-render $(BUILD)/indirect-compute check-build-dependencies check-host-bridge $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/empty-shader $(BUILD)/graphics $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery
	$(BUILD)/indirect-render
	$(BUILD)/indirect-compute
	$(BUILD)/atomics
	$(BUILD)/debug
	$(BUILD)/msaa
	$(BUILD)/depth
	$(BUILD)/demote
	$(BUILD)/graphics-admission
	$(BUILD)/empty-shader
	$(BUILD)/graphics
	$(BUILD)/compute
	$(BUILD)/procedures-cxx
	$(BUILD)/lifecycle
	$(BUILD)/textures
	$(BUILD)/admission
	$(BUILD)/failures
	$(BUILD)/timestamps
	$(BUILD)/recovery
	python3 tests/audit-exports.py $(BUILD)/libwebgpu_doe_c.so $(if $(SANITIZE),,--runtime)
check-build-dependencies:
	python3 tests/build_dependencies.py
.PHONY: check-host-bridge
check-host-bridge: $(BUILD)/backend-selection $(BUILD)/metal-bridge
	$(BUILD)/backend-selection
	$(BUILD)/metal-bridge
check-empty-module: $(BUILD)/empty-shader
	$(BUILD)/empty-shader
clean:
	rm -f $(OBJECTS)
	rm -f $(BUILD)/indirect-render $(BUILD)/indirect-compute $(BUILD)/backend-selection $(BUILD)/metal-bridge $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/graphics $(BUILD)/empty-shader $(BUILD)/libwebgpu_doe_c.so $(BUILD)/libdoe_compiler.so $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery
