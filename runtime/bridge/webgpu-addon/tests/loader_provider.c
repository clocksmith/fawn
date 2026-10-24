/* SPDX-License-Identifier: MIT */
__declspec(dllimport) int doe_addon_loader_dependency(void);
__declspec(dllexport) int doe_addon_loader_probe(void) { return doe_addon_loader_dependency() + 7; }
