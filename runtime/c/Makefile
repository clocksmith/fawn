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
.PHONY: package check check-tools check-style check-build
package:
	$(PYTHON) tools/build-native-package.py --ref '$(REVISION)' --build '$(BUILD)' --destination '$(STAGE)' --cc '$(CC)' --cxx '$(CXX)'
check: check-style check-tools check-build
check-tools:
	$(PYTHON) -m unittest discover -s tools -p 'test_*.py'
check-style:
	$(PYTHON) tools/check_style.py
check-build:
	@set -e; for owner in $(OWNERS); do \
		$(MAKE) --no-print-directory -C $$owner BUILD='$(BUILD)'/$$owner all; \
	done
	@echo "C owner build: $(words $(OWNERS)) owners built into $(BUILD)"
