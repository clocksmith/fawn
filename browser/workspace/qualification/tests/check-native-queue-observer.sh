#!/usr/bin/env bash
# Build a tiny software-only client, then observe its native runtime from GDB.
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "usage: $0 /absolute/path/libwebgpu_doe_c.so EXPECTED_SHA256 FRESH_OUTPUT_DIR" >&2
  exit 2
fi
library=$(realpath "$1")
expected=$2
out=$(realpath -m "$3")
here=$(cd "$(dirname "$0")" && pwd)
runtime_root=${FAWN_RUNTIME_ROOT:-$(realpath "$here/../../../../runtime")}
icd=${SOFTWARE_VULKAN_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
[[ $expected =~ ^[0-9a-f]{64}$ && $(sha256sum "$library" | cut -d' ' -f1) == "$expected" ]]
[[ $icd == /usr/share/vulkan/icd.d/lvp* && -f $icd && ! -e $out ]]
mkdir -p "$out"
cp "$here/../trace-doe-vulkan-queue.py" "$out/observer.py"
cp "$here/native-queue-cancel.gdb" "$out/cancel.gdb"
libdir=$(dirname "$library")
cc -g -Wall -Werror -I"$runtime_root/c/native/tests" -I"$runtime_root/vendor/webgpu-headers" \
  -o "$out/fixture" "$here/native-queue-observer.c" "$library" -Wl,-rpath,"$libdir"
for mode in success cancel; do
  mkdir "$out/$mode"
  timeout 60 bwrap --unshare-all --die-with-parent --new-session --clearenv \
    --ro-bind /usr /usr --symlink usr/bin /bin --symlink usr/lib /lib --symlink usr/lib64 /lib64 \
    --ro-bind /etc/ld.so.cache /etc/ld.so.cache --ro-bind /etc/passwd /etc/passwd --ro-bind /etc/group /etc/group \
    --ro-bind "$out" "$out" --bind "$out/$mode" /receipt --ro-bind "$libdir" "$libdir" \
    --proc /proc --dev /dev --tmpfs /tmp --dir /var --dir /run --chdir /tmp \
    --setenv HOME /tmp --setenv PATH /usr/bin:/bin --setenv LANG C.UTF-8 --setenv LP_NUM_THREADS 1 \
    --setenv VK_DRIVER_FILES "$icd" --setenv VK_ICD_FILENAMES "$icd" --setenv DOE_C_TEST_ADAPTER software \
    --setenv BLITZEN_NATIVE_TRACE_LIBRARY "$library" --setenv BLITZEN_NATIVE_TRACE_SHA256 "$expected" \
    --setenv BLITZEN_NATIVE_TRACE_OUT /receipt/native.jsonl --setenv CASE "$mode" --setenv ROOT "$out" \
    -- /bin/sh -ec '
      test ! -e /dev/dri && test ! -e /dev/nvidia0 && test ! -e /run/user
      if [ "$CASE" = success ]; then advance=continue; else advance="source $ROOT/cancel.gdb"; fi
      exec gdb -nx -nh -q -batch -iex "set auto-load off" -iex "set debuginfod enabled off" \
        -ex "set disable-randomization off" -ex start -ex "source $ROOT/observer.py" -ex "$advance" --args "$ROOT/fixture"
    ' > "$out/$mode/gdb.log" 2>&1
done
python3 - "$out" "$expected" <<'PY'
import json, sys
from pathlib import Path
path=Path(sys.argv[1])
for mode in ('success','cancel'):
    rows=[json.loads(line) for line in (path/mode/'native.jsonl').read_text().splitlines()]
    armed,end=rows[0],rows[-1]
    assert armed['event']=='collector-armed' and armed['nativeLibrarySha256']==sys.argv[2]
    assert end['event']=='collector-ended' and end['errorCount']==0 and end['pageOwnership'] is None and not end['latencyQualified']
    encoded=[(r['submit'],r['queue'],r['workgroups']) for r in rows if r['event']=='compute-encoding-entered']
    submitted=[r['submit'] for r in rows if r['event']=='native-submission-entered']
    fences=[(r['submit'],r['queue']) for r in rows if r['event']=='native-fence-observed']
    if mode=='success':
        assert encoded==[(1,1,[4,1,1]),(2,2,[4,1,1])] and submitted==[1,2] and fences==[(1,1),(2,2)]
        assert end['exitCode']==0 and not end['unresolved']
        assert 'all eight expected words' in (path/mode/'gdb.log').read_text()
    else:
        assert encoded==[(1,1,[4,1,1])] and submitted==[1] and fences==[] and end['unresolved']==[1]
        assert end['exitCode']!=0
print('Two pinned native Doe queue completions observed; pre-fence interruption remained unresolved. No page ownership assigned.')
PY
