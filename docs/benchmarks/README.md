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

VMX, the codec of Open Media Transport, as the stored picture codec (`REPLAY_CODEC=vmx`, README "VMX"), against libjpeg-turbo, on a host without a GPU. The host was the lmjob build VM: 16 vCPU AMD EPYC (Genoa), Debian sid, GCC. It has no libmxl, so `REPLAY_SYNTHETIC_FILE` feeds the cameras. The pictures are the first 100 of three 1080p50 sequences from SVT's test set (Xiph's derf collection), 4:2:0 8-bit sources converted to v210. `crowd_run` and `park_joy` are hard to code, and `old_town_cross` is easy. `tests/bench/lab.sh` repeats the run.

### The codecs, one thread

`mxl-replay-codec-bench` encodes each picture from v210 and decodes it back to v210 the way the replay records and plays (`encodePictureV210`, `decodePictureToV210`), so the v210 conversions count. It also times the UI preview (`decodePicturePreview`, at least 384 wide) and measures the size and the PSNR of the 10-bit samples against the source. Times are milliseconds per 1080 picture on one thread, from the second of two passes. For `crowd_run`:

| Codec | Encode ms | Decode ms | Preview ms | KB/picture | GB/h at 50p | PSNR Y dB | PSNR C dB |
| --- | --- | --- | --- | --- | --- | --- | --- |
| JPEG q92 (the default) | 8.14 | 10.64 | 7.97 | 867 | 160 | 41.10 | 42.24 |
| JPEG q80 | 6.79 | 7.82 | 5.43 | 505 | 93 | 36.79 | 39.10 |
| VMX `hq` (the default) | 4.65 | 5.71 | 5.41 | 570 | 105 | 36.54 | 40.75 |
| VMX `sq` | 4.37 | 4.00 | 3.78 | 285 | 53 | 31.97 | 37.43 |
| VMX `omt_sq` | 4.29 | 4.69 | 4.54 | 421 | 78 | 34.04 | 38.87 |
| VMX quality 95 | 5.72 | 7.49 | 7.40 | 896 | 165 | 40.60 | 44.13 |
| VMX quality 98 | 7.44 | 10.91 | 10.94 | 1484 | 274 | 46.87 | 49.65 |

`park_joy` gave the same result (JPEG q92 8.44 ms to encode and 10.88 to decode, 887 KB, 40.97 dB; VMX `hq` 4.75 and 5.79 ms, 551 KB, 35.20 dB; VMX quality 95 5.64 and 7.73 ms, 909 KB, 40.64 dB). On `old_town_cross` JPEG shrinks with the content (q92 695 KB, q80 360 KB) and VMX's profiles don't (`hq` 547 KB, 38.43 dB), because their rate control aims at a size per frame. Quality 90 gives 375 KB at 36.86 dB there.

- At the same quality as the default JPEG (VMX quality 95 against JPEG q92, with luma 0.4 dB lower, chroma 2 dB higher and the same size), VMX encodes in 30 % less time and decodes in 30 % less.
- At the defaults (`hq` against q92) it takes 43 % less time to encode and 46 % less to decode. Its pictures are a third smaller and 4.5 dB lower in luma. `hq` is close to JPEG q80 in quality and 13 % larger.
- The PSNR is measured against 8-bit sources, so VMX's 10 bits don't show (JPEG rounds to 8 bits and VMX doesn't). On 10-bit cameras the gap in VMX's favour would be larger.
- libvmx alone (`tests/bench/vmx_probe.cpp`, no conversions) takes 3.4 ms to encode and 3.9 ms to decode a 10-bit `crowd_run` picture at its default size, and 3.0 and 3.4 ms in 8-bit UYVY. GCC and clang builds are equally fast. The v210 conversions add about 1.2 ms to an encode and 1.8 ms to a decode.
- Two defects of libvmx had to be fixed first (`third_party/libvmx/README.md`). With one thread it still coded on a worker thread, which made a picture take 4–10 ms on this VM. Its thread pool could also hang in `join()` when destroyed right after it was created.

### The replay

`tests/bench/replay_bench.py` runs the whole process. Every camera records the three sequences, each from another place, and channel n plays camera n from 10 s back. It measures 30 s after 18 s of recording. There were two rounds, and in each the codecs ran one after the other for every case. The codec columns give the process's CPU in cores. The last column says whether every camera recorded 50 pictures a second, every channel output 50 grains a second, and no channel resynced; the render time per output grain comes from `playout_seconds{stage="render"}`. VMX q95 is `REPLAY_VMX_QUALITY=95`, measured in the second round only. This VM has shared vCPUs, which slow down a paced encode (see the run of 2026-11-25), so these figures understate VMX's saving.

| Cameras, channels, playout | JPEG q92: CPU cores | VMX `hq` | VMX q95 | Keeps up |
| --- | --- | --- | --- | --- |
| 4, 2, 1× repeat | 3.07, 3.18 | 3.03, 3.07 | 3.04 | yes, every codec |
| 8, 4, 1× repeat | 6.52, 6.40 | 6.52, 6.80 | 6.38 | yes, every codec |
| 12, 4, 1× repeat | 8.25, 8.10 | 7.42, 7.35 | 7.42 | yes, every codec |
| 12, 8, 1× repeat | 11.08, 10.68 | 8.48, 8.38 | 9.40 | almost. Every codec records 49.6–49.9 pictures a second per camera, with 2–8 resyncs in 30 s |
| 4, 4, 0.5× blend | 5.64, 5.58 | 5.25, 5.58 | 5.52 | no. Rendering takes 24–26 ms a grain against 20 ms (38–40 output grains a second, 400–460 resyncs). VMX `hq` kept up in round 2 (15.6 ms) but not in round 1 |

- With few cameras the codec is a small part of the process, and the CPU is the same for both. From 12 cameras on, VMX saves 9 % (4 channels) to 12–21 % (8 channels) of the process's CPU. That is less than the codec's own 30–45 %, because the rest of the process (v210 copies, the rings and their writes, playout, the feeders) costs the same with either codec.
- The codec doesn't limit blending at 0.5× on the CPU. The blend decodes two pictures into 16-bit frames, blends them and packs the result on the output thread, which takes about 25 ms a grain with either codec on this VM. This host needs a GPU (JPEG only) or a faster CPU blend for it. VMX decodes faster, which is why `hq` kept up once.
- At 1×, render time per grain is 0.2–1.7 ms higher with VMX. The cause is not known yet, and it stays far below the 20 ms period.

Not measured: a host with a GPU (VMX uses none of it: recording and playout stay on the CPU and interpolation is not available, see README "VMX"), 2160p, HFR, and VMX's 10-bit gain on a 10-bit source.

## Lab run 2026-11-25: dedicated cores

On the lmjob VM (Hetzner, shared vCPUs), VMX saved no CPU with 8 cameras and 2 channels, although each encode takes 30–45 % less time. The host caused it. On shared vCPUs, an encode that starts after the thread slept 8–14 ms or more runs at about half speed, with either codec. `vmx-pace` measured VMX `hq` at 4.5 ms flat out and 8–11 ms after a sleep of 8–20 ms, and JPEG q92 at 8 ms flat out and 15–18 ms after a sleep of 14–20 ms. A camera thread encodes once every 20 ms and sleeps the rest. VMX's faster encode leaves a sleep of about 15 ms, past the threshold, so it lost its gain. JPEG's sleep of about 12 ms mostly stayed under it.

The same runs repeated on an Infomaniak Public Cloud VM with dedicated cores (`a16-ram32-disk80-perf1`, 16 vCPU AMD EPYC Genoa, no SMT, nothing else running), with Debian sid in Docker and the same pictures.

### A paced encode

| | JPEG q92 | VMX `hq` | VMX q95 |
| --- | --- | --- | --- |
| one encode, flat out (`vmx-pace`) | 7.47 ms | 4.62 ms | |
| one encode after a 14 ms sleep | 7.58 ms | 4.78 ms | |
| 8 cameras at 50/s (`mxl-replay-encode-scale`): CPU per encode | 7.42 ms | 4.95 ms | 5.97 ms |
| 8 cameras at 50/s: cores | 2.97 | 1.98 (−33 %) | 2.39 (−20 %) |

The codec bench gave the same per-picture times as on the lmjob VM. For `crowd_run`, JPEG q92 took 7.57 ms to encode and 9.54 to decode, VMX `hq` 4.75 and 6.22, and VMX q95 5.52 and 7.85.

### The replay, 8 cameras, 2 channels

`replay_bench.py` ran 30 s per case, in two rounds. This flavour caps the disk at 200 MB/s and 500 IOPS, and `dd` wrote 173 MB/s. 8 cameras of JPEG q92 need about 330 MB/s on these pictures, and VMX `hq` about 225 MB/s.

On the VM's disk:

| Codec | Playout | Recorded/s per camera | Output/s | Resyncs | Render ms | CPU cores |
| --- | --- | --- | --- | --- | --- | --- |
| JPEG q92 | 1× repeat | 41.7, 42.0 | 50.0 | 0 | 1.1–1.2 | 3.80, 3.85 |
| VMX `hq` | 1× repeat | 50.0, 50.0 | 50.0 | 0 | 1.3–1.6 | 3.11, 3.15 |
| VMX q95 | 1× repeat | 39.9, 39.9 | 50.0 | 0 | 1.3–1.4 | 3.09, 3.13 |
| JPEG q92 | 0.5× blend | 41.9, 42.0 | 50.0 | 0 | 1.3 | 3.46, 3.42 |
| VMX `hq` | 0.5× blend | 49.9, 50.0 | 50.0 | 0 | 1.5–1.6 | 2.84, 2.85 |
| VMX q95 | 0.5× blend | 40.0, 40.0 | 50.0 | 0 | 1.5–1.6 | 2.81, 2.78 |

With the ring in RAM (`--storage /dev/shm/media`, `REPLAY_BUFFER_HOURS=0.01`), 1× repeat:

| Codec | Recorded/s per camera | Output/s | Resyncs | Render ms | CPU cores |
| --- | --- | --- | --- | --- | --- |
| JPEG q92 | 50.0, 50.0 | 50.0 | 0 | 1.2, 1.3 | 4.28, 4.23 |
| VMX `hq` | 50.0, 50.0 | 50.0 | 0 | 1.3, 1.6 | 2.94, 3.09 (−29 %) |
| VMX q95 | 50.0, 50.0 | 50.0 | 0 | 1.3, 1.5 | 3.47, 3.52 (−18 %) |

Split by thread group (from `/proc`, over 10 s, on the disk), the camera threads used 3.06 cores with JPEG and 2.29 with VMX `hq`, the decode workers 0.91 and 0.68, and the output threads 0.12 and 0.15.

- On dedicated cores, VMX saves what its encode and decode save. With 8 cameras and 2 channels the process uses 29 % less CPU with `hq`, and 18 % less at JPEG's quality (q95).
- On this flavour's disk, only VMX `hq` records 8 cameras in full, because its pictures are a third smaller. JPEG q92 and VMX q95 record 40–42 pictures a second per camera. The disk limits them, not the CPU.
- Measure CPU on hosts with dedicated cores. Shared vCPUs slow down a thread that sleeps between pictures, and all of the replay's threads do.

## Lab run 2026-11-24: the CPU blend in v210

Where a CPU slow motion lost its time, and the blend that works in v210 instead. The host was a 16 vCPU AMD EPYC (Genoa) VM without a GPU, with Debian sid and GCC. The pictures are `crowd_run` from SVT's 1080p50 test set (Xiph's derf collection), converted to v210.

### Where the time went

`mxl-replay-blend-bench` ran on 40 pictures. It times one blended grain of a 0.5× slow motion (frames k and k+1 at 50 %), stage by stage, in ms on one thread, with JPEG q92.

| Stage | ms |
| --- | --- |
| **Before (1.8.7)**, all on the output thread | |
| decode A into a 16-bit frame | 9.54 |
| decode B into a 16-bit frame | 9.59 |
| blend the 16-bit frames (`blendInto`) | 1.68 |
| pack to v210 (`packV210`) | 2.34 |
| copy to the channel's last grain | 0.32 |
| **output thread** | **23.47** |
| **After** | |
| the worker decodes B to v210 | 9.70 |
| B out of the worker's slot (copy) | 0.31 |
| blend A and B into the grain (`blendV210`) | 0.49 |
| copy to the channel's last grain | 0.28 |
| **output thread** | **1.08** |
| **CPU in all** | **10.78** |

- The old path needed more than a 50p grain (20 ms) on the output thread for every blended grain. The two decodes were 82 % of that, and it decoded each frame twice, as B of one grain and again as the exact frame of the next.
- Unpacking into 16-bit frames and packing back cost another 4 ms, and the blend itself 1.7 ms. v210 needs neither step.
- Now the decode worker decodes each frame once, straight into v210. The output thread copies it out of the slot and blends. `blendV210` takes 0.5 ms with AVX2 and 2.0 ms in the baseline x86-64 build, which can't multiply 32-bit lanes, hence `target_clones`. A blended grain now costs less than half the CPU in all, and the output thread about 1 ms.
- What remains is libjpeg-turbo's decode, 9.7 ms. Decoding straight to v210 costs the same as decoding to 16 bits, and the faster line packing (`line8ToV210` without its per-pixel branch) made no difference.

### The replay

The whole process ran without MXL. Every camera recorded real 1080p50 pictures and channel n played camera n (channels 5–8 played cameras 1–4 again). Each case ran 25 s, before and after the change in one job.

| Case | Before: output grains/s, resyncs, render ms, CPU | After |
| --- | --- | --- |
| 4 cameras, 4 channels, 0.5× blend | 38.9, 371, 25.5 ms, 5.60 cores | 50.0, 0, 3.4 ms, 4.02 cores |
| 4 cameras, 8 channels, 0.5× blend | 50.0, 3, 17.6 ms, 9.37 | 50.0, 0, 4.4 ms, 6.60 |
| 12 cameras, 8 channels, 1× repeat | 49.9, 6, 5.4 ms, 10.56 | 49.8, 8, 5.9 ms, 10.35 |

- Slow motion on the CPU now keeps up with 8 channels on 4 cameras and takes about a quarter less CPU. Before, 4 channels fell behind, and the 8-channel run that kept up had 17.6 ms renders, close to the limit.
- Playout at 1× is unchanged. It takes the same path, and the few resyncs at 12 cameras and 8 channels occur before and after.
