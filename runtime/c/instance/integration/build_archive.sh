#!/bin/sh
# This owner archive contains no future/callback objects and invokes only GCC.
set -eu
out=${1:?usage: build_archive.sh ABSOLUTE_OUTPUT_DIRECTORY [address,undefined|thread]}
case "$out" in /*) ;; *) echo 'output must be absolute' >&2; exit 2;; esac
case "${2:-none}" in
    none) instrumentation= ;;
    address,undefined|thread) instrumentation="-fsanitize=$2 -fno-omit-frame-pointer" ;;
    *) echo 'unsupported sanitizer' >&2; exit 2;;
esac
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$out"
for source in instance wait capabilities; do
    gcc $instrumentation -std=c11 -O2 -g -fPIC -Wall -Wextra -Wpedantic -Werror -pthread \
        -I"$here/../include" -I"$here/../../futures/include" \
        -I"$here/../../../zig/vendor/webgpu-headers" \
        -c "$here/../src/$source.c" -o "$out/$source.o"
done
ar rcs "$out/libdoe_instance.a" "$out/instance.o" "$out/wait.o" "$out/capabilities.o"
