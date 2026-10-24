CC = gcc
CXX = g++
AR = ar
BUILD ?= build
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -pthread
LDFLAGS += -pthread
PLATFORM_HEADERS = $(wildcard ../platform/include/*.h)
OBJECTS = $(BUILD)/storage.o $(BUILD)/futures.o

.PHONY: all check clean
all: $(BUILD)/libdoe_futures.a

$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c src/internal.h include/doe_futures.h $(PLATFORM_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libdoe_futures.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(BUILD)/test_api: tests/test_api.c $(BUILD)/libdoe_futures.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -o $@
$(BUILD)/test_cpp: tests/test_cpp.cpp $(BUILD)/libdoe_futures.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDFLAGS) -o $@
check: $(BUILD)/test_api $(BUILD)/test_cpp
	$(BUILD)/test_api
	$(BUILD)/test_cpp
clean:
	rm -rf $(BUILD)

FAULT_WRAPS = pthread_mutex_init pthread_condattr_init pthread_condattr_setclock \
              pthread_cond_init pthread_condattr_destroy pthread_cond_destroy \
              pthread_mutex_destroy
$(BUILD)/test_faults: tests/test_faults.c $(BUILD)/libdoe_futures.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) $(addprefix -Wl$(comma)--wrap=,$(FAULT_WRAPS)) -o $@
comma := ,
$(BUILD)/test_exhaustion: tests/test_exhaustion.c src/storage.c src/futures.c src/internal.h include/doe_futures.h $(PLATFORM_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDOE_FUTURES_TESTING \
	  -DDOE_FUTURES_TEST_INSTANCE_LIMIT=2 -DDOE_FUTURES_TEST_SEQUENCE_LIMIT=2 \
	  -DDOE_FUTURES_TEST_LEGACY_LIMIT=0x8000000000000001ULL \
	  tests/test_exhaustion.c src/storage.c src/futures.c $(LDFLAGS) -o $@
check-faults: $(BUILD)/test_faults $(BUILD)/test_exhaustion
	$(BUILD)/test_faults
	$(BUILD)/test_exhaustion
.PHONY: check-faults
check: check-faults
$(BUILD)/test_time: tests/test_time.c $(BUILD)/libdoe_futures.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -Wl,--wrap=clock_gettime -Wl,--wrap=pthread_cond_timedwait -o $@
check-time: $(BUILD)/test_time
	$(BUILD)/test_time
.PHONY: check-time
check: check-time
$(BUILD)/test_concurrency: tests/test_concurrency.c $(BUILD)/libdoe_futures.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDFLAGS) -Wl,--wrap=pthread_cond_wait -o $@
check-concurrency: $(BUILD)/test_concurrency
	$(BUILD)/test_concurrency
.PHONY: check-concurrency
check: check-concurrency

# Linux-host control of Darwin's typed relative-wait boundary, not an SDK build.
$(BUILD)/test_darwin_time: tests/test_darwin_time.c src/storage.c src/futures.c src/internal.h include/doe_futures.h $(PLATFORM_HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDOE_FUTURES_TESTING -DDOE_FUTURES_TEST_DARWIN \
	  tests/test_darwin_time.c src/storage.c src/futures.c $(LDFLAGS) \
	  -Wl,--wrap=clock_gettime -Wl,--wrap=pthread_condattr_setclock -o $@
.PHONY: check-darwin-boundary
check-darwin-boundary: $(BUILD)/test_darwin_time
	$(BUILD)/test_darwin_time

.PHONY: check-portable
check-portable: $(BUILD)/test_api $(BUILD)/test_cpp
	$(BUILD)/test_api
	$(BUILD)/test_cpp
