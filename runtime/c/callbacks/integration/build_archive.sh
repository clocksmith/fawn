#!/bin/sh
# The archive contains only this C payload owner. Link one accepted futures archive.
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
gcc $instrumentation -std=c11 -O2 -g -fPIC -Wall -Wextra -Wpedantic -Werror -pthread \
    -I"$here/../include" -I"$here/../../futures/include" \
    -c "$here/../src/callbacks.c" -o "$out/callbacks.o"
ar rcs "$out/libdoe_callbacks.a" "$out/callbacks.o"
