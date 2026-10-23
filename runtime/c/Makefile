# SPDX-License-Identifier: MIT
# Explicit developer staging and cross-owner checks. Does not install or switch a provider.
PYTHON ?= python3
CC = gcc
CXX = g++
REVISION ?= HEAD
BUILD ?= /tmp/doe-c-package-build
STAGE ?= /tmp/doe-c-package
# Every owner with a library build, in dependency order. A changed public header
# or enum must still build every consumer; owner checks alone cannot see that.
OWNERS = futures callbacks instance labels error-scopes dropin-policy shader \
	resources compiler backends/vulkan backends/metal backends/d3d12 native
.PHONY: package check check-tools check-style check-build check-globals check-js check-vulkan
package:
	$(PYTHON) tools/build-native-package.py --ref '$(REVISION)' --build '$(BUILD)' --destination '$(STAGE)' --cc '$(CC)' --cxx '$(CXX)'
check: check-style check-tools check-globals
check-tools:
	$(PYTHON) -m unittest discover -s tools -p 'test_*.py'
check-style:
	$(PYTHON) tools/check_style.py
check-build:
	@set -e; for owner in $(OWNERS); do \
		$(MAKE) --no-print-directory -C $$owner BUILD='$(BUILD)'/$$owner OUT='$(BUILD)'/$$owner all; \
	done
	@echo "C owner build: $(words $(OWNERS)) owners built into $(BUILD)"
# The compiler owner builds into its gitignored compiler/build regardless of BUILD.
check-globals: check-build
	$(PYTHON) tools/check_globals.py '$(BUILD)' compiler/build
# Unchanged doe-gpu JavaScript consumer on a staged package (make package first).
# Rebuilds the gitignored N-API addon and runs the first-kernel integration test
# with DOE_WEBGPU_LIB on the staged C library. Needs Node headers and a physical
# GPU (the package requests a nonfallback adapter); developer evidence only.
check-js:
	@test -f '$(STAGE)/MANIFEST.json' || { echo 'check-js needs a finished STAGE from make package' >&2; exit 2; }
	cd ../../packages/doe-gpu && node scripts/build-addon.js
	cd ../../packages/doe-gpu && env -u VK_ICD_FILENAMES DOE_WEBGPU_LIB='$(abspath $(STAGE))/lib/libwebgpu_doe_c.so' \
		node test/integration/test-integration-first-kernel.js
# Every software-Vulkan resource lifetime client (needs a CPU ICD, e.g.
# VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json). Shared Vulkan header
# changes must keep all of them building and passing; owner checks skip them.
VULKAN_LIFETIMES = vulkan-lifetime vulkan-render-lifetime vulkan-query-lifetime \
	vulkan-indirect-lifetime vulkan-indirect-render-lifetime
check-vulkan:
	$(MAKE) --no-print-directory -C compiler shared SHARED_LIBRARY='$(abspath $(BUILD))/vulkan-compiler/libdoe_compiler.so'
	@set -e; for target in $(VULKAN_LIFETIMES); do \
		$(MAKE) --no-print-directory -C resources BUILD='$(abspath $(BUILD))/resources-vulkan' \
			COMPILER_LIBRARY='$(abspath $(BUILD))/vulkan-compiler/libdoe_compiler.so' $$target; \
	done
