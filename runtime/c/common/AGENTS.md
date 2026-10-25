# Shared backend leaves

Keep each helper a single typed operation with no device/resource ownership or
format policy. The copy footprint helper preserves normalized zero arithmetic;
callers own zero admission, pitch/defaults, alignment and capacity. Output remains
untouched on overflow. Do not grow this leaf into a general arithmetic framework.

The dispatch guard source leaf shares exact generated WGSL between backends.
It owns source construction only: compiler owners retain language semantics and
reflection; each backend retains limit admission, GPU scratch, barriers and leases.
Never interpret caller argument bytes on the CPU or introduce shared execution,
device state, shader parsing or native binding policy here. Failed construction
leaves both outputs untouched. Keep the historical Vulkan source bytes unchanged.

The timestamp source leaf similarly owns exact binary32-period WGSL construction
only. Preserve its retained kernel bytes and floor/modulo arithmetic. Backend
owners supply real clock calibration, availability, scratch, barriers and leases.
