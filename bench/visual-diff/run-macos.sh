#!/bin/bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 DAWN_GENERATED_INCLUDE_DIR WEBGPU_LIBRARY_DIR NEW_OUTPUT_DIR" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
inc=$(cd "$1" && pwd)
lib=$(cd "$2" && pwd)
out=$3
mkdir "$out" # Preserve previous samples and their binary identities.
out=$(cd "$out" && pwd)

for name in dawn doe_c; do
    xcrun clang++ -dynamiclib -std=c++17 -O2 -fobjc-arc -I"$inc" -L"$lib" \
        -lwebgpu_"$name" -Wl,-rpath,"$lib" "$here/native_shared_diff.mm" \
        -framework Foundation -framework Metal -framework IOSurface -o "$out/$name.dylib"
done
xcrun clang++ -dynamiclib -std=c++17 -O2 "$here/cpu_readback.cpp" \
    -framework IOSurface -o "$out/cpu.dylib"
xcrun swiftc -O "$here/owned_window.swift" -o "$out/owned-window"
shasum -a 256 "$lib"/{libwebgpu_dawn,libwebgpu_doe_c,libdoe_compiler}.dylib \
    "$inc/dawn/webgpu.h" "$out"/*.dylib "$out/owned-window" >"$out/SHA256SUMS"
sw_vers >"$out/system.txt"
xcrun clang++ --version >>"$out/system.txt"
set +e
"$out/owned-window" "$out/matched-native.json" "$out/cpu.dylib" \
    "$out/dawn.dylib" "$out/doe_c.dylib" >"$out/adapters.log" 2>&1
status=$?
set -e
cat "$out/adapters.log"
if [[ -f "$out/matched-native.json" ]]; then
    python3 "$here/score-native.py" "$out/matched-native.json" | tee "$out/score.json"
fi
exit "$status"
