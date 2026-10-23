CC ?= cc
AR ?= ar
BUILD ?= /tmp/doe-c-d3d12
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
STRICT = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
OWNER = src/context.c src/resources.c src/commands.c src/compute.c
WINDOWS = src/windows_context.c src/windows_resources.c src/windows_commands.c
HOST_OBJECTS = $(OWNER:src/%.c=$(BUILD)/host/%.o)
WINDOWS_OBJECTS = $(OWNER:src/%.c=$(BUILD)/windows/%.o) $(WINDOWS:src/%.c=$(BUILD)/windows/%.o)
HEADERS = include/doe_d3d12.h src/internal.h src/native.h src/windows_internal.h
.PHONY: all check sanitize windows native-fixture
# On other platforms only the explicitly host-scoped owner tests are available.
ifeq ($(OS),Windows_NT)
all: windows
else
all: check
endif
$(BUILD)/host $(BUILD)/windows:
	mkdir -p $@
$(BUILD)/host/%.o: src/%.c $(HEADERS) | $(BUILD)/host
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -Dcalloc=doe_dx_test_calloc -Dfree=doe_dx_test_free -c $< -o $@
$(BUILD)/test_owner: tests/test_owner.c tests/mock_native.c tests/mock_native.h $(HOST_OBJECTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/test_owner.c tests/mock_native.c $(HOST_OBJECTS) -o $@
check: $(BUILD)/test_owner
	$(BUILD)/test_owner
sanitize:
	$(MAKE) BUILD=$(BUILD)/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' check
$(BUILD)/windows/%.o: src/%.c $(HEADERS) | $(BUILD)/windows
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) -DDOE_DX_REQUIRE_WINDOWS -c $< -o $@
$(BUILD)/libdoe_d3d12.a: $(WINDOWS_OBJECTS)
	$(AR) rcs $@ $^
windows: $(BUILD)/libdoe_d3d12.a
# Build only. Executing this requires separately authorized Windows/GPU work.
native-fixture: $(BUILD)/libdoe_d3d12.a tests/native_compute.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STRICT) tests/native_compute.c $(BUILD)/libdoe_d3d12.a -ld3d12 -ldxgi -ldxguid -o $(BUILD)/native_compute.exe
