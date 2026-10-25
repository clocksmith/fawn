/* SPDX-License-Identifier: MIT
 * Process entry for the smoke runners in the C/Make build, the role the Zig
 * anchors (the ort_*_anchor.zig files in runtime/zig/src/integrations/onnxruntime) play in zig
 * build: pass argv to the runner's extern "C" entry (DOE_SMOKE_ENTRY) and exit
 * 1 on any nonzero result. */
int DOE_SMOKE_ENTRY(int argc, char **argv);

int main(int argc, char **argv)
{
    return DOE_SMOKE_ENTRY(argc, argv) != 0 ? 1 : 0;
}
