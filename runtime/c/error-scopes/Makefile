CC = gcc
CXX = g++
AR = ar
BUILD ?= build
CPPFLAGS += -Iinclude -Iintegration -I../../zig/vendor/webgpu-headers
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -fPIC -fno-short-enums -fstack-usage -Wall -Wextra -Wpedantic -Werror -pthread
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror -pthread
LDLIBS += -pthread
OBJECTS = $(BUILD)/error_scopes.o $(BUILD)/doe_error_scopes_zig_bridge.o
HEADERS = include/doe_error_scopes.h src/internal.h integration/doe_error_scopes_zig_bridge.h \
          ../../zig/vendor/webgpu-headers/webgpu.h

.PHONY: all check check-api check-faults check-concurrency check-layout native-archive
all: $(BUILD)/libdoe_error_scopes.a
native-archive:
	python3 tools/archive.py --build-directory $(abspath $(BUILD))
$(BUILD):
	mkdir -p $@
$(BUILD)/error_scopes.o: src/error_scopes.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/doe_error_scopes_zig_bridge.o: integration/doe_error_scopes_zig_bridge.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libdoe_error_scopes.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_api: tests/test_api.c $(BUILD)/libdoe_error_scopes.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/test_cpp: tests/test_cpp.cpp $(BUILD)/libdoe_error_scopes.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/test_faults: tests/test_faults.c $(BUILD)/libdoe_error_scopes.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) \
	  -Wl,--wrap=pthread_mutex_init -Wl,--wrap=pthread_mutex_lock \
	  -Wl,--wrap=pthread_mutex_unlock -Wl,--wrap=pthread_mutex_destroy -o $@
check-api: $(BUILD)/test_api $(BUILD)/test_cpp
	$(BUILD)/test_api
	$(BUILD)/test_cpp
check-faults: $(BUILD)/test_faults
	$(BUILD)/test_faults
$(BUILD)/test_concurrency: tests/test_concurrency.c $(BUILD)/libdoe_error_scopes.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/test_layout: tests/test_layout.c $(BUILD)/libdoe_error_scopes.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS) -o $@
check-concurrency: $(BUILD)/test_concurrency
	$(BUILD)/test_concurrency
check-layout: $(BUILD)/test_layout
	$(BUILD)/test_layout
check: check-api check-faults check-concurrency check-layout
