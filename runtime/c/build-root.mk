# SPDX-License-Identifier: MIT
# Per-checkout default scratch root for every owner build. Clones on one host
# must not share object trees: make's mtime rules and owner verifiers would mix
# objects compiled from another clone's sources. Explicit BUILD=/OUT= win.
DOE_C_SOURCE_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DOE_C_CHECKOUT_ID := $(shell printf '%s' '$(DOE_C_SOURCE_ROOT)' | cksum | cut -d' ' -f1)
DOE_C_TMP ?= /tmp/doe-c-$(DOE_C_CHECKOUT_ID)
