# SPDX-License-Identifier: MIT
.DEFAULT_GOAL := all
include ../build-root.mk
BUILD ?= $(DOE_C_TMP)/platform
CC = cc
CXX = c++
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -pthread
CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -pthread
HEADERS = $(wildcard include/*.h)
.PHONY: all check check-faults clean
all: $(BUILD)/test_primitives $(BUILD)/test_cpp
$(BUILD):
	mkdir -p $@
$(BUILD)/test_primitives: tests/test_primitives.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iinclude $< $(LDFLAGS) -o $@
$(BUILD)/test_cpp: tests/test_cpp.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -Iinclude $< $(LDFLAGS) -o $@
check: all
	$(BUILD)/test_primitives
	$(BUILD)/test_cpp
# Genuine GNU linker wrappers, not a synthetic Windows build on POSIX.
FAULT_WRAPS = pthread_mutexattr_settype pthread_mutex_init pthread_mutexattr_destroy pthread_join pthread_detach
comma := ,
$(BUILD)/test_posix_faults: tests/test_posix_faults.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Iinclude $< $(LDFLAGS) $(addprefix -Wl$(comma)--wrap=,$(FAULT_WRAPS)) -o $@
check-faults: $(BUILD)/test_posix_faults
	$(BUILD)/test_posix_faults
clean:
	rm -rf $(BUILD)
