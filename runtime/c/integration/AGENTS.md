# Native C selection integration

Follow the parent C instructions and common style. This directory composes
existing owners; it owns no object reference counts, callbacks, error policy,
shader meaning or backend implementation.

- Keep each selection explicit and production defaults unchanged. Build the C
  owners with their GCC producers, never compile them implicitly through Zig.
- Loader smoke and physical execution are separate modes. A controlled missing
  ICD proves failure delivery, not adapter/device/GPU support. Never run the
  hardware mode without separate authorization for that physical host.
- Localize only the individually named build-bound bridge symbols. Preserve
  independent C APIs and public WebGPU symbols; test actual ELF preemption.
- The portable consumer has one implementation compiled as C11 and C++17.
  Non-Linux branches are authored only until built and exercised on that target.
