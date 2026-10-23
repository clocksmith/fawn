CC = gcc
CXX = g++
BUILD ?= /tmp/doe-c-native
COMPILER_LIBRARY ?= $(BUILD)/libdoe_compiler.so
SANITIZE ?=
# The compiler remains the sole source-list/flag owner; no copied build manifest.
COMPILER_SOURCES = $(addprefix ../compiler/,$(shell sed -n 's/^LIB_SOURCES = //p' ../compiler/Makefile))
COMPILER_FLAGS = $(shell sed -n 's/^CFLAGS = //p' ../compiler/Makefile)
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -DWGPU_SHARED_LIBRARY -DWGPU_IMPLEMENTATION -I../../zig/vendor/webgpu-headers \
 -I../instance/include -I../futures/include -I../callbacks/include -I../resources/include \
 -I../labels/include -I../error-scopes/include -I../shader/include -I../compiler/include \
 -I../backends/vulkan/include
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -fPIC -pthread $(SANITIZE)
SOURCES = $(wildcard src/*.c) $(wildcard ../instance/src/*.c) $(wildcard ../futures/src/*.c) \
 $(wildcard ../callbacks/src/*.c) $(wildcard ../resources/src/*.c) $(wildcard ../labels/src/*.c) \
 $(wildcard ../error-scopes/src/*.c) ../shader/src/shader.c $(wildcard ../backends/vulkan/src/*.c)
OBJECTS = $(patsubst src/%.c,$(BUILD)/native/%.o,$(filter src/%,$(SOURCES))) \
 $(patsubst ../%.c,$(BUILD)/owners/%.o,$(filter ../%,$(SOURCES)))
HEADERS = $(wildcard src/*.h ../*/include/*.h ../resources/src/*.h ../backends/vulkan/src/*.h ../backends/vulkan/include/*.h)
COMPILER_RUNPATH ?= $(abspath $(dir $(COMPILER_LIBRARY)))
COMPILER_LINK = -L$(abspath $(dir $(COMPILER_LIBRARY))) -l:$(notdir $(COMPILER_LIBRARY)) '-Wl,-rpath,$(COMPILER_RUNPATH)'
.PHONY: all check check-empty-module clean
all: $(BUILD)/libwebgpu_doe_c.so
$(BUILD):
	mkdir -p $@
$(BUILD)/libdoe_compiler.so: $(COMPILER_SOURCES) $(wildcard ../compiler/include/*.h ../compiler/src/*.h) ../compiler/src/exports.map ../compiler/Makefile | $(BUILD)
	test -n "$(COMPILER_SOURCES)"
	$(CC) $(COMPILER_FLAGS) $(SANITIZE) -fPIC -shared $(COMPILER_SOURCES) -Wl,--version-script=../compiler/src/exports.map -lm -o $@
$(BUILD)/native/%.o: src/%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden -c $< -o $@
$(BUILD)/owners/%.o: ../%.c $(HEADERS) Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fvisibility=hidden -c $< -o $@
$(BUILD)/libwebgpu_doe_c.so: $(OBJECTS) $(COMPILER_LIBRARY) | $(BUILD)
	$(CC) $(CFLAGS) -shared -Wl,-z,defs -Wl,-Bsymbolic-functions $(OBJECTS) $(COMPILER_LINK) -ldl -o $@
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
check: $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/empty-shader $(BUILD)/graphics $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery
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
check-empty-module: $(BUILD)/empty-shader
	$(BUILD)/empty-shader
clean:
	rm -f $(OBJECTS)
	rm -f $(BUILD)/atomics $(BUILD)/debug $(BUILD)/msaa $(BUILD)/depth $(BUILD)/demote $(BUILD)/graphics-admission $(BUILD)/graphics $(BUILD)/empty-shader $(BUILD)/libwebgpu_doe_c.so $(BUILD)/libdoe_compiler.so $(BUILD)/compute $(BUILD)/procedures-cxx $(BUILD)/lifecycle $(BUILD)/textures $(BUILD)/admission $(BUILD)/failures $(BUILD)/timestamps $(BUILD)/recovery
