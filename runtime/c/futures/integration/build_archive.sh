#!/bin/sh
# Source-only reproduction: accepted C and private bridge compiled ONLY by GCC.
set -eu
out=${1:?usage: build_archive.sh ABSOLUTE_OUTPUT_DIRECTORY}
case "$out" in /*) ;; *) echo "output must be absolute" >&2; exit 2;; esac
sanitizer=${2:-none}
case "$sanitizer" in
    none) instrumentation= ;;
    address,undefined|thread) instrumentation="-fsanitize=$sanitizer -fno-omit-frame-pointer" ;;
    *) echo "unsupported sanitizer: $sanitizer" >&2; exit 2 ;;
esac
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$out"
for source in storage futures; do
    gcc $instrumentation -std=c11 -O2 -g -fPIC -Wall -Wextra -Wpedantic -Werror -pthread \
        -I"$here/../include" -c "$here/../src/$source.c" -o "$out/$source.o"
done
gcc $instrumentation -std=c11 -O2 -g -fPIC -Wall -Wextra -Wpedantic -Werror -pthread \
    -I"$here/../include" -c "$here/doe_futures_zig_bridge.c" -o "$out/bridge.o"
ar rcs "$out/libdoe_futures_zig.a" "$out/storage.o" "$out/futures.o" "$out/bridge.o"
