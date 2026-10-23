#!/bin/sh
# CPU-only source reproduction with existing GCC, G++, Zig and Vulkan loader.
# Usage: run_checks.sh NEW_ABSOLUTE_OUTPUT_DIR EXISTING_ZIG EXISTING_LINK_PREFIX
set -eu
ulimit -c 0
out=${1:?new absolute output directory required}
zig=${2:?existing Zig 0.15.2 executable required}
link_prefix=${3:?existing native Vulkan loader search prefix required}
case "$out" in /*) ;; *) echo "output must be absolute" >&2; exit 2;; esac
if test -e "$out"; then echo "output must be new; preserve prior evidence" >&2; exit 2; fi
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../../.." && pwd)
mkdir -p "$out"
run() {
    label=$1
    shift
    python3 "$here/capture.py" "$out/$label.json" "$@"
}
cd "$repo"
run gcc-version gcc --version
run zig-version "$zig" version
run archive sh "$here/build_archive.sh" "$out/archive"
run native-compile gcc -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror \
    -Iruntime/zig/vendor/webgpu-headers "$here/native_consumer.c" -ldl -o "$out/native_consumer"
run cpp-compile g++ -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Werror \
    -fno-exceptions -fno-rtti -pthread -Iruntime/c/futures/include \
    -Iruntime/c/futures/integration "$here/test_bridge.cpp" \
    "$out/archive/libdoe_futures_zig.a" -o "$out/test_bridge"
run cpp-run "$out/test_bridge"
run fault-compile gcc -std=c11 -O2 -g -fPIC -shared -Wall -Wextra -Wpedantic \
    -Werror -pthread "$here/pthread_faults.c" -ldl -o "$out/pthread_faults.so"
cd "$repo/runtime/zig"
run selected-build "$zig" build dropin test-c-futures-integration -j1 \
    -Doptimize=ReleaseFast "-Dexperimental-c-futures-archive=$out/archive/libdoe_futures_zig.a" \
    --search-prefix "$link_prefix" --cache-dir "$out/cache" \
    --global-cache-dir "$out/global-cache" --prefix "$out/selected" --verbose
run native-selected "$out/native_consumer" "$out/selected/lib/libwebgpu_doe.so" selected
run setup-failure env "LD_PRELOAD=$out/pthread_faults.so" DOE_FUTURES_FAULT=setup \
    "$out/native_consumer" "$out/selected/lib/libwebgpu_doe.so" setup-failure
run fatal-init env "LD_PRELOAD=$out/pthread_faults.so" DOE_FUTURES_FAULT=init-cleanup \
    "$out/native_consumer" "$out/selected/lib/libwebgpu_doe.so" fatal-init || true
run fatal-deinit env "LD_PRELOAD=$out/pthread_faults.so" DOE_FUTURES_FAULT=deinit-cleanup \
    "$out/native_consumer" "$out/selected/lib/libwebgpu_doe.so" fatal-deinit || true
python3 - "$out" <<'PY'
import json
import pathlib
import sys
for name in ["fatal-init", "fatal-deinit"]:
    receipt = json.loads((pathlib.Path(sys.argv[1]) / (name + ".json")).read_text())
    if receipt["returncode"] != -6 or "failed closed: status 13" not in receipt["stderr"]:
        raise SystemExit(name + " did not produce required fail-closed abort")
PY
run default-build "$zig" build dropin test-full -j1 -Doptimize=ReleaseFast \
    -Dtest-filter=doe_future_ids --search-prefix "$link_prefix" \
    --cache-dir "$out/cache" --global-cache-dir "$out/global-cache" \
    --prefix "$out/default" --verbose
run native-default "$out/native_consumer" "$out/default/lib/libwebgpu_doe.so" default
cd "$repo"
run frozen-seals sha256sum -c runtime/c/futures/integration/CONTRACT.sha256
sha256sum "$out/selected/lib/libwebgpu_doe.so" "$out/default/lib/libwebgpu_doe.so" \
    "$out/archive/libdoe_futures_zig.a" > "$out/ARTIFACTS.sha256"
