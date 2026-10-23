# Shared arithmetic leaves

Keep each helper a single typed operation with no device/resource ownership or
format policy. The copy footprint helper preserves normalized zero arithmetic;
callers own zero admission, pitch/defaults, alignment and capacity. Output remains
untouched on overflow. Do not grow this leaf into a general arithmetic framework.
