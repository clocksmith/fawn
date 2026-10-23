# SPDX-License-Identifier: MIT
# Explicit developer staging only. Does not install or switch a provider.
PYTHON ?= python3
CC = gcc
CXX = g++
REVISION ?= HEAD
BUILD ?= /tmp/doe-c-package-build
STAGE ?= /tmp/doe-c-package
.PHONY: package check-tools
package:
	$(PYTHON) tools/build-native-package.py --ref '$(REVISION)' --build '$(BUILD)' --destination '$(STAGE)' --cc '$(CC)' --cxx '$(CXX)'
check-tools:
	$(PYTHON) -m unittest discover -s tools -p 'test_*.py'
