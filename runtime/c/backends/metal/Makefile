CC ?= cc
AR ?= ar
BUILD ?= /tmp/doe-c-metal
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
STRICT = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
SOURCES = src/context.c src/resources.c src/commands.c src/objc_runtime.c \
          src/objc_pipeline.c src/objc_commands.c src/texture_formats.c src/textures.c \
          src/samplers.c src/texture_ownership.c src/texture_transfer.c \
          src/objc_images.c src/objc_image_transfer.c
OBJECTS = $(SOURCES:src/%.c=$(BUILD)/%.o)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LDLIBS =
else
LDLIBS = -ldl
endif
.PHONY: all check probe sanitize clean abi-compile
all: $(BUILD)/libdoe_metal.a
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c src/internal.h src/native.h src/objc_abi.h include/doe_metal.h include/doe_metal_texture.h src/texture_internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -c $< -o $@
$(BUILD)/libdoe_metal.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_lifetime: tests/test_lifetime.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_lifetime.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/probe: tests/probe.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_objc_calls: tests/test_objc_calls.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_images: tests/test_images.c tests/mock_native.c tests/mock_native.h $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_images.c tests/mock_native.c $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
$(BUILD)/test_objc_images: tests/test_objc_images.c $(BUILD)/libdoe_metal.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) $< $(BUILD)/libdoe_metal.a $(LDLIBS) -o $@
check: $(BUILD)/test_lifetime $(BUILD)/test_objc_calls $(BUILD)/test_images $(BUILD)/test_objc_images
	$(BUILD)/test_lifetime
	$(BUILD)/test_objc_calls
	$(BUILD)/test_images
	$(BUILD)/test_objc_images
probe: $(BUILD)/probe
	$(BUILD)/probe
sanitize:
	$(MAKE) BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check
# Syntax/typed-call compilation only, NOT Apple compiler/SDK ABI verification.
abi-compile: | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__APPLE__ -D__MACH__ -D__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=130000 -c src/objc_runtime.c -o $(BUILD)/objc_x86_64_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__APPLE__ -D__MACH__ -D__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__=130000 -D__arm64__ -c src/objc_runtime.c -o $(BUILD)/objc_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_images.c -o $(BUILD)/objc_images_arm64_branch_compile.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -D__arm64__ -c src/objc_image_transfer.c -o $(BUILD)/objc_image_transfer_arm64_branch_compile.o
clean:
	rm -rf $(BUILD)
