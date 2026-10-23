# Experimental C future/completion owner

The [contract](CONTRACT.md) and [public header](include/doe_futures.h) are frozen.
Implementation and independent acceptance remain pending. [Provenance](PROVENANCE.md)
binds the retained source; CONTRACT.sha256 seals this native boundary.

This lane replaces the behavior owned by the existing native CPU future queue,
not a toy counter, and does not switch production away from Zig. Request payload
allocation, instance/device leases and GPU completion remain with their owners.
