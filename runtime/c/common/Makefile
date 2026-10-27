CC = gcc
include $(dir $(lastword $(MAKEFILE_LIST)))../build-root.mk
BUILD ?= $(DOE_C_TMP)/common
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror
.PHONY: check
check: $(BUILD)/test_copy_footprint $(BUILD)/test_dispatch_guard
	$(BUILD)/test_copy_footprint
	$(BUILD)/test_dispatch_guard
$(BUILD)/test_copy_footprint: tests/test_copy_footprint.c include/doe_copy_footprint.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
$(BUILD)/test_dispatch_guard: tests/test_dispatch_guard.c include/doe_dispatch_guard.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD)/test_sha256: tests/test_sha256.c include/doe_sha256.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
.PHONY: check-sha256
check-sha256: $(BUILD)/test_sha256
	$(BUILD)/test_sha256
check: check-sha256
ifeq ($(shell uname -s),Darwin)
$(BUILD)/test_sha256_failure: tests/test_sha256.c include/doe_sha256.h
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DDOE_SHA_TEST_PLATFORM_FAILURE $< -o $@
.PHONY: check-sha256-failure
check-sha256-failure: $(BUILD)/test_sha256_failure
	$(BUILD)/test_sha256_failure
check-sha256: check-sha256-failure
endif
