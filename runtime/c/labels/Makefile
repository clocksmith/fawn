# GCC-only label owner. No Zig compilation, dependency or fallback.
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
ROOT := $(abspath ../../..)
OWNER := $(ROOT)/runtime/c/labels
BUILD ?= /tmp/doe-labels-author-20261023/build
BUILD := $(abspath $(BUILD))
ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif
PYTHON ?= python3
TARGET ?= x86_64-linux-gnu
PROFILE ?= normal
override CFLAGS := -std=c11 -O2 -g -pthread -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes
override CXXFLAGS := -std=c++17 -O2 -g -pthread -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
PRODUCER := $(OWNER)/tools/archive.py
ARGS = --repo-root "$(ROOT)" --compiler "$(CC)" --archiver "$(AR)" --target "$(TARGET)" --profile "$(PROFILE)"
.PHONY: all verify check
all:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) "$(PRODUCER)" $(ARGS) --build-dir "$(BUILD)"
verify:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) "$(PRODUCER)" $(ARGS) --archive "$(BUILD)/libdoe_labels.a"
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) "$(PRODUCER)" $(ARGS) --archive "$(BUILD)/libdoe_labels_native.a"
check: all
	"$(CC)" $(CFLAGS) -I "$(OWNER)/include" "$(OWNER)/tests/test_store.c" "$(BUILD)/libdoe_labels.a" -o "$(BUILD)/test_store"
	"$(CXX)" $(CXXFLAGS) -I "$(OWNER)/include" "$(OWNER)/tests/test_cpp.cpp" "$(BUILD)/libdoe_labels.a" -o "$(BUILD)/test_cpp"
	"$(CC)" $(CFLAGS) -I "$(OWNER)/include" "$(OWNER)/tests/test_faults.c" "$(BUILD)/libdoe_labels.a" -Wl,--wrap=pthread_mutex_init -Wl,--wrap=pthread_mutex_destroy -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_unlock -o "$(BUILD)/test_faults"
	ulimit -c 0; "$(BUILD)/test_faults"
	ulimit -c 0; "$(BUILD)/test_store"
	"$(BUILD)/test_cpp"
	DOE_LABELS_TEST_OUTPUT="$(BUILD)/verifier-tests" PYTHONDONTWRITEBYTECODE=1 $(PYTHON) "$(OWNER)/tests/test_archive.py"
