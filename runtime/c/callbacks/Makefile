CC = gcc
CXX = g++
AR = ar
BUILD ?= build
FUTURES_ARCHIVE ?= ../futures/build/libdoe_futures.a
CPPFLAGS += -Iinclude -I../futures/include
CFLAGS += -std=c11 -O2 -g -fPIC -Wall -Wextra -Wpedantic -Werror -pthread
CXXFLAGS += -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -pthread
LDLIBS += -pthread
.PHONY: all check
all: $(BUILD)/libdoe_callbacks.a
$(BUILD):
	mkdir -p $@
$(BUILD)/callbacks.o: src/callbacks.c include/doe_callbacks.h ../futures/include/doe_futures.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/libdoe_callbacks.a: $(BUILD)/callbacks.o
	$(AR) rcs $@ $^
$(BUILD)/test_api: tests/test_api.c $(BUILD)/libdoe_callbacks.a $(FUTURES_ARCHIVE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/libdoe_callbacks.a $(FUTURES_ARCHIVE) $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD)/test_cpp: tests/test_cpp.cpp $(BUILD)/libdoe_callbacks.a $(FUTURES_ARCHIVE)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD)/libdoe_callbacks.a $(FUTURES_ARCHIVE) $(LDFLAGS) $(LDLIBS) -o $@
check: $(BUILD)/test_api $(BUILD)/test_cpp
	$(BUILD)/test_api
	$(BUILD)/test_cpp
