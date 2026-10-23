#!/usr/bin/env python3
"""Boundary tests for the mechanical C style rules, including frozen exemptions."""
import hashlib
from pathlib import Path
import tempfile
import unittest

from check_style import run

SPDX = b"/* SPDX-License-Identifier: MIT */\n"
GUARDED = SPDX + b"#ifndef UNIT_H\n#define UNIT_H\nint unit;\n#endif\n"


class StyleTests(unittest.TestCase):
    def check(self, files: dict[str, bytes]) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, data in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            return run(root, sorted(files))[1]

    def rules(self, files: dict[str, bytes]) -> list[str]:
        return [failure.split(":", 1)[0] for failure in self.check(files)]

    def test_compliant_sources_pass(self):
        self.assertEqual(self.rules({"o/a.c": SPDX + b"int a;\n", "o/a.h": GUARDED,
                                     "o/b.hpp": SPDX + b"#pragma once\n"}), [])

    def test_license_must_be_in_first_three_lines(self):
        self.assertEqual(self.rules({"o/a.c": b"\n\n\n" + SPDX}), ["license"])
        self.assertEqual(self.rules({"o/a.m": b"\n\n" + SPDX}), [])

    def test_header_guard_must_define_the_tested_name(self):
        wrong = SPDX + b"#ifndef UNIT_H\n#define OTHER_H\n#endif\n"
        self.assertEqual(self.rules({"o/a.h": wrong}), ["guard"])
        self.assertEqual(self.rules({"o/a.c": SPDX}), [])

    def test_suppression_pragma_fails_even_when_frozen(self):
        data = GUARDED + b"#pragma GCC diagnostic ignored \"-Wswitch\"\n"
        self.assertEqual(self.rules({"o/a.h": data}), ["pragma"])
        self.assertEqual(self.rules({"o/independent_tests/a.h": data}), ["pragma"])

    def test_size_applies_to_all_c_family_sources_even_when_frozen(self):
        self.assertEqual(self.rules({"o/a.cpp": SPDX + b"\n" * 998}), [])
        self.assertEqual(self.rules({"o/a.cpp": SPDX + b"\n" * 999}), ["size"])
        self.assertEqual(self.rules({"o/independent_tests_m2/a.c": b"\n" * 1000}), ["size"])

    def test_independent_tests_are_frozen(self):
        self.assertEqual(self.rules({"o/independent_tests_m3/a.h": b"int a;\n"}), [])

    def test_seal_exempts_only_matching_bytes(self):
        frozen = b"int sealed;\n"
        digest = hashlib.sha256(frozen).hexdigest().encode()
        seal = digest + b"  o/a.c\n"
        self.assertEqual(self.rules({"o/a.c": frozen, "o/SEAL.sha256": seal}), [])
        self.assertEqual(self.rules({"o/a.c": frozen + b"int changed;\n",
                                     "o/SEAL.sha256": seal}), ["license"])
        relative = digest + b"  a.c\n"
        self.assertEqual(self.rules({"o/a.c": frozen, "o/SEAL.sha256": relative}), [])

    def test_native_clients_select_adapters_through_the_helper(self):
        direct = SPDX + b"void f(void) { options.forceFallbackAdapter = WGPU_TRUE; }\n"
        built = {"runtime/c/Makefile": b"T = client.c backend_selection.c\n"}
        self.assertEqual(self.rules({**built, "runtime/c/native/tests/client.c": direct}), ["adapter"])
        cpu = SPDX + b"int f(int t) { return t == WGPUAdapterType_CPU; }\n"
        self.assertEqual(self.rules({**built, "runtime/c/native/tests/client.c": cpu}), ["adapter"])
        self.assertEqual(self.rules({**built, "runtime/c/native/tests/backend_selection.c": direct}), [])
        self.assertEqual(self.rules({**built, "runtime/c/backends/vulkan/tests/client.c": direct}), [])

    def test_every_test_source_is_built_by_something(self):
        client = SPDX + b"int main(void) { return 0; }\n"
        self.assertEqual(self.rules({"o/tests/client.c": client}), ["test"])
        self.assertEqual(self.rules({"o/tests/client.c": client,
                                     "o/Makefile": b"check:\n\tcc tests/client.c\n"}), [])
        self.assertEqual(self.rules({"o/tests/client.c": client,
                                     "o/tools/run.py": b"SOURCES = ['client.c']\n"}), [])
        self.assertEqual(self.rules({"o/independent_tests/tests/client.c": client}), [])

    def test_owner_flags(self):
        strict = b"CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror\n"
        self.assertEqual(self.rules({"o/Makefile": strict}), [])
        self.assertEqual(self.rules({"o/Makefile": b"CFLAGS += -std=c11 -Wall\n"}), ["flags"])
        self.assertEqual(self.rules({"o/Makefile": b"package:\n\ttrue\n"}), [])

    def test_tmp_defaults(self):
        self.assertEqual(self.rules({"o/Makefile": b"BUILD ?= /tmp/doe-c-o\n"}), ["tmp"])
        self.assertEqual(self.rules({"o/sources.mk": b"OUT ?= /tmp/x\n"}), ["tmp"])
        self.assertEqual(self.rules({"o/Makefile": b"BUILD ?= $(DOE_C_TMP)/o\n"}), [])
        self.assertEqual(self.rules({"o/Makefile": b"BUILD := $(abspath $(BUILD))\n"}), [])


if __name__ == "__main__":
    unittest.main()
