# libvmx

VMX, the intra-frame video codec of [Open Media Transport](https://www.openmediatransport.org/), from
https://github.com/openmediatransport/libvmx at `31724bb96530325a989bc157c8e07538d6f1c4db` (MIT, see `LICENSE.txt`).

Only the x86-64 sources are copied. Two changes are marked `mxl-replay`:

- `src/thread_tasks.h`. The worker and `Join` wait with a predicate. A pool destroyed right after it
  was created (`VMX_SetThreads` after `VMX_Create`) could lose the stop signal, and its `join()`
  hung. The worker had passed its `while (running)` check before `Destroy` cleared the flag, then
  waited for a notification that had already been sent. The unit test hung once in the lab's
  repeated runs.
- `src/vmxcodec.cpp` (`VMX_DecodePlanes`, `VMX_EncodePlanes`). With one thread, the calling thread
  codes the slices. Upstream hands them to its worker thread even then. On a VM whose cores don't
  share a cache, that made a 1080p picture take 4–10 ms instead of 3–4, because the worker read the
  8 MB the caller had just written on another core.

`vmx_gcc.h` is ours. The build force-includes it so GCC accepts the MSVC `__declspec(align(n))` in
the sources (upstream builds with `clang -fdeclspec`). Like upstream's Linux build, everything
compiles with `-mavx2 -mbmi -mlzcnt`, so the codec needs a CPU with AVX2 and BMI2 (Haswell and
later, every Zen). `vmxAvailable()` checks for them at run time.
