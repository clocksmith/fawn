# Attributing an ordinary compute submission to Doe

A WebGPU queue can report success after the device rejected a submitted command.
In the native fixture, an already-consumed command raises a validation error and the
work-done callback still succeeds. Even when a queue submits a valid batch, a
second command in the same batch can perform only a copy.

The diagnostic build exposes `doeNativeVulkanCommandWitnessV1` to the native
consumer. Before the ordinary submit, arm the exact inner command on its own
queue with `arm=1`. Require state 1 and serial zero. Keep the command alive.
Record the ordinary work-done callback status and, after it returns, read with
`arm=0`. Require callback success, state 3 and a nonzero serial. State 2
establishes acceptance only. States 0 and 4 cannot establish completion. The
read adds no fence, wait or queue submission. A reused command must pass a
fresh arm for the current grant. A prior receipt cannot authorize another
submission.

The witness follows a successfully encoded compute dispatch into an accepted
native Vulkan submission and its own observed fence. Page identity, capture
permission, the pipeline and bindings actually encoded, the loaded DSO and
the verified application outcome each require separate evidence. Keep runtime
pointers within the GPU process.

## Reproduce the native controls

Use an isolated Linux Fawn checkout at `aad2f10063e2b95a406fef6d0c79199bd2988cc4`
and the qualification files next to this document. The packager reads a Git
revision. Commit the overlay in the isolated checkout before packaging it.
Set `CASE` to the directory containing these qualification tests and `OUT` to
an empty absolute directory. Run the programs with the software Vulkan ICD
and no hardware device nodes.

```sh
git apply "$CASE/resident-doe-command-witness.patch"
git add runtime/c
git -c user.name="Local qualification" -c user.email="local@invalid" commit -m "Instrument native Vulkan command completion"
make -C runtime/c package REVISION=HEAD BUILD="$OUT/build" STAGE="$OUT/package"
for probe in native-queue-future native-command-witness; do
  cc -std=c11 -O2 -Wall -Wextra -Werror \
    -Iruntime/c/native/tests -Iruntime/c/native/include -Iruntime/vendor/webgpu-headers \
    "$CASE/$probe.c" -L"$OUT/package/lib" -lwebgpu_doe_c -ldl \
    -Wl,-rpath-link,"$OUT/package/lib" -o "$OUT/$probe"
done
export DOE_C_TEST_ADAPTER=software LP_NUM_THREADS=1
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
export LD_LIBRARY_PATH="$OUT/package/lib"
export RESIDENT_EXPECTED_DOE_LIBRARY="$LD_LIBRARY_PATH/libwebgpu_doe_c.so"
"$OUT/native-queue-future"
"$OUT/native-command-witness"
```

The queue probe can also use an unmodified staged Doe library. Set both library
variables to that build and verify its digest separately. Pointing the expected
path at a different inode must exit 2. Compare both library orders and record
whether native submissions were pending *before* registering the callback.
The command probe checks correct readback, a same-batch copy, a wrong queue,
fresh and repeated arms, and a rejected command. Browser timing is measured by
the separate Resident Process comparison.
