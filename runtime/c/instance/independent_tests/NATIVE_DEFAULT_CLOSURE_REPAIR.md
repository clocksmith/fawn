# Default root real-export closure repair

The first 24-filter default supplemental root reached linker exit 1 after
270.243 seconds, not a timeout. The supervisor observed no live group afterward.
It lacked real buffer and encoder owners needed by the retained cleanup/command
paths, producing undefined doeNativeBufferRelease and four encoder symbols.
The failed root, exact command and diagnostics remain intact. Root v3 adds only
imports of doe_buffer_ops_native.zig and doe_encoder_native.zig. It supplies real
retained exports, not substitutes, and keeps the same 24 test filters/assertions.
No candidate runtime implementation changes. This is a post-handoff evaluator
fixture-closure repair and not a newly discovered candidate ownership defect.
