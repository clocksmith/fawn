CC = gcc
CXX = g++
AR = ar
BUILD ?= build
FUTURES_ARCHIVE ?= ../futures/build/libdoe_futures.a
CPPFLAGS += -Iinclude -I../futures/include -I../../zig/vendor/webgpu-headers
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -fPIC -pthread
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -pthread
LDFLAGS += -pthread
OBJECTS = $(BUILD)/instance.o $(BUILD)/wait.o $(BUILD)/capabilities.o
SOURCES = src/instance.c src/wait.c src/capabilities.c
comma := ,
FAULT_WRAPS = pthread_mutex_init pthread_condattr_init pthread_condattr_setclock \
              pthread_cond_init pthread_condattr_destroy pthread_cond_destroy \
              pthread_mutex_destroy pthread_mutex_unlock doe_future_queue_requirements

.PHONY: all check
all: $(BUILD)/libdoe_instance.a
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c src/internal.h include/doe_instance.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libdoe_instance.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_api: tests/test_api.c $(BUILD)/libdoe_instance.a $(FUTURES_ARCHIVE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@
$(BUILD)/test_cpp: tests/test_cpp.cpp $(BUILD)/libdoe_instance.a $(FUTURES_ARCHIVE)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDFLAGS) -o $@
$(BUILD)/test_concurrency: tests/test_concurrency.c $(BUILD)/libdoe_instance.a $(FUTURES_ARCHIVE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@
$(BUILD)/test_faults: tests/test_faults.c $(SOURCES) src/internal.h $(FUTURES_ARCHIVE) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDOE_INSTANCE_TESTING -DDOE_INSTANCE_TEST_REFERENCE_LIMIT=4 \
	  tests/test_faults.c $(SOURCES) $(FUTURES_ARCHIVE) $(LDFLAGS) \
	  $(addprefix -Wl$(comma)--wrap=,$(FAULT_WRAPS)) -o $@
$(BUILD)/abi_probe: integration/abi_probe.c $(BUILD)/libdoe_instance.a $(FUTURES_ARCHIVE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@
$(BUILD)/native_consumer: integration/native_consumer.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -ldl $(LDFLAGS) -o $@
check: $(BUILD)/test_api $(BUILD)/test_cpp $(BUILD)/test_concurrency $(BUILD)/test_faults $(BUILD)/abi_probe
	$(BUILD)/test_api
	$(BUILD)/test_cpp
	$(BUILD)/test_concurrency
	$(BUILD)/test_faults
	$(BUILD)/abi_probe
