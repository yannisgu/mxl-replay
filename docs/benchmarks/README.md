# Benchmarks

Hardware numbers for the A4000 and L4 are filled in by a run on that GPU. This tree does not invent them.

## What to measure

For `dis-cuda` `balanced` and for `ofa` (when `VK_NV_optical_flow` is present):

- Concurrent 1080p50 `interpolate` channels before an output grain is late.
- Whether 2160p50 stays inside the same budget.
- Per output frame, GPU time of decode + flow + interpolate + pack. The target is under half of the frame period at the channel count above, and zero late grains over one hour.
- SM, memory, and OFA utilisation per channel.

Publish the table in this directory next to the harness report.

## Harness

`mxl-replay-harness a.ppm b.ppm 0.5 out.ppm` runs the port.

`mxl-replay-harness --compare reference.ppm port.ppm` prints PSNR and SSIM and exits 0 when PSNR is at least 40 dB.

The original Futatabi OpenGL build is not part of the default image. Build it from the Nageru 2.3.4 tarball in a separate job with EGL, run both on the same pairs, and store the report here. The synthetic translation test in the unit suite is the gate that runs without a GPU; it is not a substitute for the Futatabi comparison.

## Storage

At start the process writes 1 MiB sequentially and exposes `mxl_replay_write_bytes_per_second`. The full-load check is all configured cameras recording while two channels play. Warn when the result is below `REPLAY_STORAGE_MIN_MBPS` (default 100).

Sizing used by the free-space check: `width * height * (quality / 92) * 0.1875` bytes per frame. At 1920×1080, quality 92, 50 fps that is about 70 GB/h per camera. HFR multiplies by the frame-rate factor. Audio adds 48 kHz stereo float32.

## Lab run 2026-10-03: NVIDIA A16

Not a target GPU: one GA107 of an A16 (PCIe Gen4 x4), driver 595.84, 2× Xeon Gold 6136, image built from this repository (1.0.0 plus the recorder fix in the CHANGELOG). Cameras were mxl-test-player 1080p50 outputs (bars with burn-in), routed by IS-05; storage was the operating-system disk (`mxl_replay_write_bytes_per_second` 27.9 MB/s, below `REPLAY_STORAGE_MIN_MBPS`), buffer 0.05 h.

| Case | Result |
| --- | --- |
| 1.0.0, 4 cameras recording | about 9.3 recorded grains/s per camera, `dropped` 0 (grains skipped without being counted) |
| Fixed, 1 camera recording | 49.9 grains/s, no drops |
| Fixed, 4 cameras recording | about 20 grains/s per camera, about 30 drops/s per camera. The one recorder thread encodes about 80 frames/s in total; `gpuEncodeV210` holds one global nvJPEG state behind a mutex |
| 2 channels, `dis-cuda` `balanced`, `interpolate` at 0.5× (1 camera recording) | output head 3 grains behind the current index (`mxl-info` latency 35–55 ms), SM 69 % |
| 4 channels, same | output heads 3–8 grains behind (up to 160 ms), SM 69–76 %, process 1.1 cores |

Two 1080p50 interpolate channels keep up on this GPU; four do not. The GPU is not saturated in either case. Late output grains are not counted anywhere (no metric), so the lag was read from the output flows with `mxl-info`. GPU time per stage (`frame_gpu_seconds`) is described in the README but not exported. 2160p50, OFA and the Futatabi comparison were not run.

## Lab run 2026-10-06: blocking CUDA sync (1.2.3)

Same host and procedure as below (four test-player outputs recorded as cameras 1–4, every channel `interpolate` at 0.5× or played at 1× from 10 s back, 30 s measured). `perf` on 1.2.2 with 4 channels: 84 % of the process in libcuda, the CUDA runtime spinning in `cudaStreamSynchronize` (CUDA's default scheduling spins while the machine has spare cores). 1.2.3 sets `cudaDeviceScheduleBlockingSync`.

| Image | Channels | Speed | Recorded / dropped per camera per s | Output grains/s per channel | Output latency (grains) | CPU | GPU SM |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1.2.2 | 2 | 0.5× | 56.7–56.8 / 0 | 50.0, 50.0 | 4, 4 | 3.79 cores | 92 % |
| 1.2.3 | 2 | 0.5× | 56.8 / 0 | 50.0, 49.9 | 3, 2 | 0.82 cores | 91 % |
| 1.2.2 | 4 | 0.5× | 56.2–56.5 / 0 | 50.0–50.1 | 1–7 | 7.37 cores | 99 % |
| 1.2.3 | 4 | 0.5× | 56.7 / 0 | 49.9–50.2 | 2–6 | 0.89 cores | 100 % |
| 1.2.2 | 4 | 1× | 56.7–56.8 / 0 | 50.0 | 1 | 3.29 cores | 57 % |
| 1.2.3 | 4 | 1× | 56.4 / 0 | 50.0 | 1–2 | 1.75 cores | 58 % |
| 1.2.2 | 8 | 0.5× | 28–30 / 27–29 | 49.9–50.2 | 3–13 | 12.11 cores | 97 % |
| 1.2.3 | 8 | 0.5× | 30–31 / 26–27 | 49.9–50.3 | 5–10 | 0.93 cores | 98 % |

Recorded counts above 50 per second are cameras catching up on grains still in the input ring. The CPU growth of the 2026-10-04 run below ("every camera is now actually recorded … nvJPEG's Huffman stage on the CPU") was mostly this spinning: with a blocking sync, four cameras and four interpolating channels take under a core. The GPU stays the limit for interpolation: at 8 channels it is saturated and the cameras drop about half their grains with either version.

## Lab run 2026-10-04: per-thread GPU pipeline

Same host, GPU and cameras as above; storage the OS disk. Image built from this repository with the per-thread GPU pipeline (1.0.1) against `mxl-replay:lab` (1.0.0 plus the recorder fix). Each case: the four test-player outputs routed to cameras 1–4 (all recording), every channel set to `interpolate` and played at 0.5× from 10 s back, 30 s measured. Recorded and dropped grains come from `/api/v1/status`, output grains/s and late reads from `mxl-verify` on each channel's video flow, latency from `mxl-info`, CPU from the container cgroup, SM load from `nvidia-smi dmon`.

| Image | Channels | Recorded / dropped per camera per s | Output grains/s per channel | Output latency (grains) | CPU | GPU SM |
| --- | --- | --- | --- | --- | --- | --- |
| before | 2 | 18–20 / 36–39 | 50.0, 50.0 | 0–2 | 1.2 cores | 63 % |
| before | 4 | 34 / 20–22 for cameras 1–2; cameras 3–4 none ("Too many open files") | 49.7–50.0 | 2–5 | 1.2 cores | 60 % |
| this tree | 2 | 52–53 / 0 | 50.0, 50.0 | 2 | 3.7 cores | 92 % |
| this tree | 4 | 53 / 0 | 49.9–50.1 | 1–3 | 6.2 cores | 98 % |
| this tree | 8 | 33 / 22 | 49.8–50.1 | 1–12 | 12.3 cores | 97 % |

Recorded counts above 50 per second are cameras catching up on grains still in the input ring. The CPU rises because every camera is now actually recorded (200 encodes per second instead of about 75) and the nvJPEG decoder runs its Huffman stage on the CPU on this GPU.

Result: four 1080p50 cameras record without drops while four channels interpolate, and the limit is now the GPU itself: dense optical flow (`dis-cuda` `balanced`) for one new frame pair per output pair at 0.5× keeps the GA107 at 97–98 % with four channels. Eight channels on this GPU take recording below full rate. A larger GPU (A4000, L4) is expected to scale with its SM count; that run is still open, as are 2160p50, OFA and the Futatabi comparison.

## Lab run 2026-11-24: CPU codecs, JPEG and VMX

VMX, the codec of Open Media Transport, as the stored picture codec (`REPLAY_CODEC=vmx`, README "VMX") against libjpeg-turbo, on a host without a GPU. Host: the lmjob build VM, 16 vCPU AMD EPYC (Genoa), Debian sid, GCC; no libmxl, so the cameras are fed by `REPLAY_SYNTHETIC_FILE`. Pictures: the first 100 of three 1080p50 sequences of SVT's test set (Xiph's derf collection: `crowd_run` and `park_joy` are hard, `old_town_cross` easy), 4:2:0 8-bit sources converted to v210. Repeat with `tests/bench/lab.sh`.

### The codecs, one thread

`mxl-replay-codec-bench`: each picture encoded from v210 and decoded back to v210 as the replay records and plays them (`encodePictureV210`, `decodePictureToV210`, so the v210 conversions count), the UI preview (`decodePicturePreview`, at least 384 wide), size, and PSNR of the 10-bit samples against the source. Milliseconds per 1080 picture on one thread; the second of two passes. `crowd_run`:

| Codec | Encode ms | Decode ms | Preview ms | KB/picture | GB/h at 50p | PSNR Y dB | PSNR C dB |
| --- | --- | --- | --- | --- | --- | --- | --- |
| JPEG q92 (the default) | 8.14 | 10.64 | 7.97 | 867 | 160 | 41.10 | 42.24 |
| JPEG q80 | 6.79 | 7.82 | 5.43 | 505 | 93 | 36.79 | 39.10 |
| VMX `hq` (the default) | 4.65 | 5.71 | 5.41 | 570 | 105 | 36.54 | 40.75 |
| VMX `sq` | 4.37 | 4.00 | 3.78 | 285 | 53 | 31.97 | 37.43 |
| VMX `omt_sq` | 4.29 | 4.69 | 4.54 | 421 | 78 | 34.04 | 38.87 |
| VMX quality 95 | 5.72 | 7.49 | 7.40 | 896 | 165 | 40.60 | 44.13 |
| VMX quality 98 | 7.44 | 10.91 | 10.94 | 1484 | 274 | 46.87 | 49.65 |

`park_joy` gave the same picture (JPEG q92 8.44 / 10.88 ms, 887 KB, 40.97 dB; VMX `hq` 4.75 / 5.79 ms, 551 KB, 35.20 dB; VMX quality 95 5.64 / 7.73 ms, 909 KB, 40.64 dB). On `old_town_cross` JPEG shrinks with the content (q92 695 KB, q80 360 KB) and VMX's profiles do not (`hq` 547 KB, 38.43 dB): their rate control aims at a size per frame, quality 90 there gives 375 KB at 36.86 dB.

- At the same quality as the default JPEG (VMX quality 95 against JPEG q92: luma 0.4 dB below, chroma 2 dB above, the same size) VMX encodes in 30 % less time and decodes in 30 % less.
- At the defaults (`hq` against q92) it takes 43 % and 46 % less, for pictures a third smaller and 4.5 dB lower in luma: `hq` is near JPEG q80 in quality, and 13 % larger than it.
- The PSNR is against 8-bit sources, so VMX's 10 bits (no rounding to 8 bits, JPEG's floor) do not show; on 10-bit cameras the gap in VMX's favour would be larger.
- libvmx alone (`tests/bench/vmx_probe.cpp`, no conversions) takes 3.4 ms to encode and 3.9 ms to decode a 10-bit `crowd_run` picture at its default size, 3.0 / 3.4 ms in 8-bit UYVY; GCC and clang builds are equally fast. The v210 conversions add about 1.2 ms to an encode and 1.8 ms to a decode.
- Two defects of libvmx had to be fixed first (`third_party/libvmx/README.md`): with one thread it still coded on a worker thread, which made a picture take 4–10 ms on this VM; and its thread pool could hang in `join()` when destroyed right after it was made.

### The replay

`tests/bench/replay_bench.py`: the whole process, every camera recording the three sequences (each from another place), channel n playing camera n from 10 s back, 30 s measured after 18 s. Two rounds, each case with the codecs one after the other. CPU is the process's, in cores; recorded pictures per second per camera (50 when the recorder keeps up), output grains per second per channel, output resyncs, and the render time per output grain from `playout_seconds{stage="render"}`. VMX q95 is `REPLAY_VMX_QUALITY=95`, measured in the second round only.

| Cameras, channels, playout | JPEG q92: CPU cores | VMX `hq` | VMX q95 | Keeps up |
| --- | --- | --- | --- | --- |
| 4, 2, 1× repeat | 3.07, 3.18 | 3.03, 3.07 | 3.04 | every codec: 50 recorded, 50 out, no resync |
| 8, 4, 1× repeat | 6.52, 6.40 | 6.52, 6.80 | 6.38 | every codec |
| 12, 4, 1× repeat | 8.25, 8.10 | 7.42, 7.35 | 7.42 | every codec |
| 12, 8, 1× repeat | 11.08, 10.68 | 8.48, 8.38 | 9.40 | every codec records 49.6–49.9 a second per camera; 2–8 resyncs in 30 s for every codec |
| 4, 4, 0.5× blend | 5.64, 5.58 | 5.25, 5.58 | 5.52 | none reliably: render 24–26 ms a grain against 20 ms (38–40 output grains a second, 400–460 resyncs); VMX `hq` kept up in round 2 (15.6 ms) and not in round 1 |

- With few cameras the codec is a small part of the process: the CPU is the same. From 12 cameras on VMX saves 9 % (4 channels) to 12–21 % (8 channels) of the process's CPU, less than the codec's own 30–45 %: the rest (v210 copies, the rings and their writes, playout, the feeders) does not change with the codec.
- Blending at 0.5× on the CPU is not limited by the codec. The blend decodes two pictures into 16-bit frames, blends and packs them on the output thread: about 25 ms a grain for either codec on this VM. On this host it needs a GPU (JPEG only) or a faster CPU blend; with VMX the decode is shorter, which is why `hq` crossed the line once.
- Render time per grain at 1× is 0.2–1.7 ms higher with VMX. Not investigated; it stays far below the 20 ms period.

Not measured: a host with a GPU (VMX uses none of it: recording and playout stay on the CPU and interpolation is not available, see README "VMX"), 2160p, HFR, and VMX's 10-bit gain on a 10-bit source.
