#!/usr/bin/env bash
# The JPEG / VMX comparison of docs/benchmarks/README.md ("CPU codecs"), on a Linux x86-64 host
# with AVX2 and no GPU:
#
#   tests/bench/lab.sh [build-dir]
#
# 1. Builds the replay (CPU, no MXL), runs the unit tests.
# 2. Takes the first 100 pictures of three 1080p50 test sequences (Xiph's derf collection,
#    SVT's crowd_run, park_joy and old_town_cross; 4:2:0 8-bit, converted to v210).
# 3. mxl-replay-codec-bench on each: encode and decode time on one thread, size and PSNR.
# 4. replay_bench.py: the whole replay, every camera recording the three sequences, channels
#    playing, for each codec. LAB_CASES overrides the cases ("inputs:channels:speed:motion ..."),
#    LAB_CODECS the codecs ("jpeg vmx vmx,REPLAY_VMX_QUALITY=95"), LAB_ROUNDS the repeats,
#    LAB_PARTS the parts ("codecs replay").
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build-lab}
FOOTAGE=${LAB_FOOTAGE:-$ROOT/footage}
SECONDS_PER_CASE=${LAB_SECONDS:-30}
CASES=${LAB_CASES:-"4:2:1:repeat 4:4:0.5:blend 8:4:1:repeat 12:4:1:repeat 12:8:1:repeat"}
CODECS=${LAB_CODECS:-"jpeg vmx vmx,REPLAY_VMX_QUALITY=95"}
ROUNDS=${LAB_ROUNDS:-2}
# LAB_PARTS: which parts run ("codecs replay"); a host that limits a job's time can run them apart.
PARTS=${LAB_PARTS:-"codecs replay"}

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" >/dev/null
"$BUILD/unit-tests" | tail -3

mkdir -p "$FOOTAGE"
for seq in crowd_run park_joy old_town_cross; do
  if [[ ! -s "$FOOTAGE/$seq.v210" ]]; then
    # 100 pictures of 1920×1080 4:2:0 are 311 MB; the header is a few bytes.
    curl -sf -r 0-315000000 "https://media.xiph.org/video/derf/y4m/${seq}_1080p50.y4m" -o "$FOOTAGE/$seq.y4m"
    ffmpeg -loglevel error -i "$FOOTAGE/$seq.y4m" -frames:v 100 -c:v v210 -f rawvideo -y "$FOOTAGE/$seq.v210"
    rm -f "$FOOTAGE/$seq.y4m"
  fi
done

if [[ " $PARTS " == *" codecs "* ]]; then
echo
echo "## Codecs, one thread"
lscpu | grep -E "Model name|^CPU\(s\)" | sed 's/^/    /'
for seq in crowd_run park_joy old_town_cross; do
  echo
  "$BUILD/mxl-replay-codec-bench" "$FOOTAGE/$seq.v210" 1920 1080 "$seq"
done
fi

# The cameras record all three sequences, 50 pictures of each.
MIX="$FOOTAGE/mix.v210"
if [[ ! -s "$MIX" ]]; then
  for seq in crowd_run park_joy old_town_cross; do
    head -c $((50 * 5529600)) "$FOOTAGE/$seq.v210"
  done >"$MIX"
fi

if [[ " $PARTS " != *" replay "* ]]; then
  exit 0
fi
echo
echo "## The replay, ${SECONDS_PER_CASE} s per case, ${ROUNDS} rounds"
echo
echo "| Codec | Cameras | Channels | Playout | Recorded/s per camera (min / avg) | Output grains/s per channel | Resyncs | Render ms per grain | CPU cores |"
echo "| --- | --- | --- | --- | --- | --- | --- | --- | --- |"
# Every case ROUNDS times, the codecs one after the other, so a busy moment of the host hits both.
for round in $(seq 1 "$ROUNDS"); do
for item in $CASES; do
  IFS=: read -r inputs channels speed motion <<<"$item"
  for spec in $CODECS; do
    # A codec, then settings for it: "vmx,REPLAY_VMX_QUALITY=95".
    IFS=, read -r codec extra <<<"$spec"
    python3 "$ROOT/tests/bench/replay_bench.py" "$BUILD/mxl-replay" "$MIX" --codec "$codec" --inputs "$inputs" --channels "$channels" \
      --speed "$speed" --motion "$motion" --seconds "$SECONDS_PER_CASE" ${extra:+"$extra"} 2>/dev/null || echo "| $spec | $inputs | $channels | ${speed}× $motion | failed |"
  done
done
done
