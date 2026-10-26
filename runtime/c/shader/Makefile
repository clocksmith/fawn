CC = gcc
CXX = g++
AR = ar
BUILD ?= build
COMPILER_LIBRARY ?= ../compiler/build/libdoe_compiler.so
CPPFLAGS += -Iinclude -I../compiler/include
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
COMPILER_LINK = $(abspath $(COMPILER_LIBRARY)) -Wl,-rpath,$(abspath $(dir $(COMPILER_LIBRARY)))
SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst src/%.c,$(BUILD)/%.o,$(SOURCES))
HEADERS = $(wildcard src/*.h) include/doe_shader.h $(wildcard ../compiler/include/*.h)
.PHONY: all test sanitize
all: $(BUILD)/libdoe_shader.a
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@
$(BUILD)/libdoe_shader.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_diagnostics: tests/test_diagnostics.c $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -o $@
$(BUILD)/test_long_names: tests/test_long_names.c $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_long_names.c $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -o $@
$(BUILD)/test_growth: tests/test_growth.c $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_growth.c $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -o $@
$(BUILD)/test_shader: tests/test_shader.c $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_shader.c $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -o $@
$(BUILD)/cpp: tests/cpp.cpp $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS)
	$(CXX) $(CPPFLAGS) -std=c++17 -Wall -Wextra -Wpedantic -Werror tests/cpp.cpp $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -o $@
test: $(BUILD)/test_diagnostics $(BUILD)/test_long_names $(BUILD)/test_growth $(BUILD)/test_shader $(BUILD)/test_specialize $(BUILD)/cpp
	$(BUILD)/test_diagnostics
	$(BUILD)/test_long_names
	$(BUILD)/test_growth
	$(BUILD)/test_shader
	$(BUILD)/test_specialize
	$(BUILD)/cpp
sanitize: | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) tests/test_shader.c $(COMPILER_LINK) -o $(BUILD)/sanitize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/sanitize
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) tests/test_specialize.c $(COMPILER_LINK) -lm -o $(BUILD)/sanitize_specialize
	ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/sanitize_specialize

# Optional real Vulkan consumer; no software adapter is enabled implicitly.
VULKAN_SOURCES = $(addprefix ../backends/vulkan/,$(shell sed -n 's/^SOURCES = //p' ../backends/vulkan/Makefile))
$(BUILD)/vulkan_images: tests/vulkan_images.c $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS) $(VULKAN_SOURCES) $(wildcard ../backends/vulkan/src/*.h ../backends/vulkan/include/*.h)
	$(CC) $(CPPFLAGS) -I../backends/vulkan/include $(CFLAGS) tests/vulkan_images.c $(VULKAN_SOURCES) $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -ldl -o $@
.PHONY: test-vulkan-images
test-vulkan-images: $(BUILD)/vulkan_images
	$(BUILD)/vulkan_images $(NATIVE_ARGS)

$(BUILD)/vulkan_graphics: tests/vulkan_graphics.c $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS) $(VULKAN_SOURCES) $(wildcard ../backends/vulkan/src/*.h ../backends/vulkan/include/*.h)
	$(CC) $(CPPFLAGS) -I../backends/vulkan/include $(CFLAGS) tests/vulkan_graphics.c $(VULKAN_SOURCES) $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -ldl -o $@
.PHONY: test-vulkan-graphics
test-vulkan-graphics: $(BUILD)/vulkan_graphics
	$(BUILD)/vulkan_graphics $(NATIVE_ARGS)

$(BUILD)/test_specialize: tests/test_specialize.c $(BUILD)/libdoe_shader.a $(COMPILER_LIBRARY) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_specialize.c $(BUILD)/libdoe_shader.a $(COMPILER_LINK) -lm -o $@
