# SPDX-License-Identifier: MIT
# C/Make build of the ONNX Runtime plugin EP and its smoke runners: the same
# C++20 sources runtime/zig/build.zig compiles (ort-plugin-ep, ort-plugin-ep-smoke,
# ort-plugin-ep-session-smoke, ort-incumbent-session-smoke) with the system C++
# compiler, without zig build or the Zig anchor modules. The EP computes its
# narrow slice with CPU kernels and links no WebGPU library (neither
# libwebgpu_doe nor libwebgpu_doe_c). The Zig build path is unchanged.
CXX ?= g++
CC = gcc
PYTHON ?= python3
BUILD ?= $(CURDIR)/build
ORT_LIB ?= /usr/lib/x86_64-linux-gnu/libonnxruntime.so.1.23.2
CPPFLAGS += -Ivendor/onnxruntime/include -Isrc
CXXFLAGS ?= -O2 -g
# The flags runtime/zig/build.zig passes (-std=c++20); the sources are not warning-clean under -Wall.
CXXFLAGS += -std=c++20 -fPIC
EP_SOURCES = src/doe_ort_ep_factory.cc src/doe_ort_ep.cc src/doe_ort_ep_exports.cc
HEADERS = src/doe_ort_ep.h src/doe_ort_ep_factory.h src/doe_ort_ep_api_version.h \
  vendor/onnxruntime/include/onnxruntime_c_api.h vendor/onnxruntime/include/onnxruntime_ep_c_api.h
.PHONY: all check smoke
all: $(BUILD)/libonnxruntime_doe_ep.so $(BUILD)/doe-ort-ep-smoke $(BUILD)/doe-ort-ep-session-smoke \
  $(BUILD)/ort-incumbent-session-smoke
$(BUILD):
	mkdir -p $@
$(BUILD)/libonnxruntime_doe_ep.so: $(EP_SOURCES) $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -shared $(EP_SOURCES) -o $@
# Each runner links src/smoke_main.c, the entry the Zig anchors provide in zig build.
$(BUILD)/%-main.o: src/smoke_main.c | $(BUILD)
	$(CC) -O2 -g -std=c11 -Wall -Wextra -Werror -DDOE_SMOKE_ENTRY=$(ENTRY_$*) -c $< -o $@
ENTRY_doe-ort-ep-smoke = doeOrtEpSmokeMain
ENTRY_doe-ort-ep-session-smoke = doeOrtEpSessionSmokeMain
ENTRY_ort-incumbent-session-smoke = doeOrtIncumbentSessionSmokeMain
$(BUILD)/doe-ort-ep-smoke: src/doe_ort_ep_smoke.cc $(BUILD)/doe-ort-ep-smoke-main.o $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD)/doe-ort-ep-smoke-main.o -ldl -o $@
$(BUILD)/doe-ort-ep-session-smoke: src/doe_ort_ep_session_smoke.cc $(BUILD)/doe-ort-ep-session-smoke-main.o $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD)/doe-ort-ep-session-smoke-main.o -ldl -o $@
$(BUILD)/ort-incumbent-session-smoke: src/ort_incumbent_session_smoke.cc $(BUILD)/ort-incumbent-session-smoke-main.o $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(BUILD)/ort-incumbent-session-smoke-main.o -ldl -o $@
# The factory and session smokes against a host ONNX Runtime (ORT_LIB); the
# incumbent smoke needs an ORT build with the WebGPU EP and is not run here.
check: all
	$(BUILD)/doe-ort-ep-smoke --plugin-path $(BUILD)/libonnxruntime_doe_ep.so --ort-lib-path $(ORT_LIB) \
	  --output $(BUILD)/doe-ort-ep-smoke.json
	$(BUILD)/doe-ort-ep-session-smoke --plugin-path $(BUILD)/libonnxruntime_doe_ep.so --ort-lib-path $(ORT_LIB) \
	  --case all --output $(BUILD)/doe-ort-ep-session-smoke.json
