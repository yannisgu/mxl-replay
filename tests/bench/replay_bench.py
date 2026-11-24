#!/usr/bin/env python3
"""End-to-end load of the replay without MXL: every camera records real pictures, every channel plays.

    replay_bench.py <mxl-replay> <pictures.v210> [--codec jpeg|vmx] [--inputs 4] [--channels 2]
                    [--speed 1.0] [--motion repeat|blend] [--seconds 30] [env KEY=VALUE ...]

The pictures (raw 1080p50 v210, ffmpeg -pix_fmt v210 -f rawvideo) are looped into every camera
by REPLAY_SYNTHETIC_FILE, one thread per camera on the TAI grid, like the MXL readers. After the
buffer has 15 s, channel n plays camera n (modulo the inputs) from 10 s back at --speed with
--motion. Over --seconds it measures, from /metrics and /proc:

- recorded pictures per second per camera (50 when the recorder keeps up),
- output resyncs (a channel that fell a grain behind and jumped),
- render time per output grain (decode + mix), mean and worst,
- the process's CPU in cores, and what it wrote to disk.

It prints one Markdown table row and the same as JSON on stderr.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request

METRIC = re.compile(r'^mxl_replay_([a-z_]+)(\{[^}]*\})? ([-0-9.e+]+)$')


def metrics(port):
    out = {}
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/metrics", timeout=5) as r:
        for line in r.read().decode().splitlines():
            m = METRIC.match(line)
            if m:
                out[(m.group(1), m.group(2) or "")] = float(m.group(3))
    return out


def by_label(snapshot, name, label):
    found = {}
    for (n, labels), value in snapshot.items():
        if n == name:
            m = re.search(label + r'="([^"]*)"', labels)
            if m:
                found.setdefault(m.group(1), 0.0)
                found[m.group(1)] += value
    return found


def render_seconds(snapshot):
    total, count = 0.0, 0.0
    for (n, labels), value in snapshot.items():
        if 'stage="render"' in labels:
            if n == "playout_seconds_sum":
                total += value
            elif n == "playout_seconds_count":
                count += value
    return total, count


def request(port, method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read().decode() or "null")


def cpu_seconds(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("pictures")
    ap.add_argument("--codec", default="jpeg")
    ap.add_argument("--inputs", type=int, default=4)
    ap.add_argument("--channels", type=int, default=2)
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--motion", default="repeat")
    ap.add_argument("--seconds", type=int, default=30)
    ap.add_argument("--port", type=int, default=18250)
    ap.add_argument("--storage", default=None, help="REPLAY_STORAGE_DIR (default: a temporary directory)")
    ap.add_argument("env", nargs="*", help="more KEY=VALUE for the replay")
    a = ap.parse_args()

    work = tempfile.mkdtemp(prefix="replay-bench-")
    storage = a.storage or os.path.join(work, "media")
    os.makedirs(storage, exist_ok=True)
    env = dict(os.environ)
    env.update({
        "REPLAY_FORMAT": "1080p50", "REPLAY_INPUTS": str(a.inputs), "REPLAY_CHANNELS": str(a.channels),
        "REPLAY_BUFFER_HOURS": "0.02", "REPLAY_STORAGE_DIR": storage, "REPLAY_STATE_DIR": os.path.join(work, "state"),
        "REPLAY_STORAGE_MIN_MBPS": "0", "REPLAY_SYNTHETIC": "true", "REPLAY_SYNTHETIC_FILE": a.pictures,
        "REPLAY_CODEC": a.codec, "NMOS_ENABLE": "false", "NMOS_HOST_ADDRESS": "10.255.0.10", "HOST_ID": "bench",
        "WEB_PORT": str(a.port), "MXL_DOMAIN_SCAN_PATH": os.path.join(work, "mxl"), "LOG_LEVEL": "warn",
    })
    for kv in a.env:
        k, v = kv.split("=", 1)
        env[k] = v
    log = open(os.path.join(work, "replay.log"), "w")
    proc = subprocess.Popen([a.binary], env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(300):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{a.port}/readyz", timeout=1)
                break
            except Exception:
                if proc.poll() is not None:
                    raise SystemExit(f"the replay exited with {proc.returncode}: see {work}/replay.log")
                time.sleep(0.1)
        time.sleep(15)
        status = request(a.port, "GET", "/api/v1/status")
        back = int(status["tai_ns"]) - 10_000_000_000
        for c in range(1, a.channels + 1):
            base = f"/api/v1/channels/{c}"
            request(a.port, "POST", f"{base}/angle", {"camera": (c - 1) % a.inputs + 1})
            request(a.port, "POST", f"{base}/mode", {"motion": a.motion})
            request(a.port, "POST", f"{base}/position", {"tai_ns": back})
            request(a.port, "POST", f"{base}/speed", {"speed": a.speed})
            request(a.port, "POST", f"{base}/transport", {"command": "play"})
        time.sleep(3)
        m0, c0, t0 = metrics(a.port), cpu_seconds(proc.pid), time.time()
        time.sleep(a.seconds)
        m1, c1, t1 = metrics(a.port), cpu_seconds(proc.pid), time.time()
        elapsed = t1 - t0
        rec0, rec1 = by_label(m0, "record_frames_total", "camera"), by_label(m1, "record_frames_total", "camera")
        rates = [(rec1[k] - rec0.get(k, 0)) / elapsed for k in rec1]
        resyncs = sum(by_label(m1, "output_resyncs_total", "channel").values()) - sum(by_label(m0, "output_resyncs_total", "channel").values())
        failed = sum(by_label(m1, "storage_write_failed_total", "camera").values()) - sum(by_label(m0, "storage_write_failed_total", "camera").values())
        (s0, n0), (s1, n1) = render_seconds(m0), render_seconds(m1)
        render_ms = (s1 - s0) / max(1.0, n1 - n0) * 1000
        outputs = (n1 - n0) / elapsed / max(1, a.channels)
        status = request(a.port, "GET", "/api/v1/status")
        result = {
            "codec": a.codec + (" " + " ".join(a.env) if a.env else ""), "inputs": a.inputs, "channels": a.channels,
            "speed": a.speed, "motion": a.motion, "seconds": round(elapsed, 1),
            "recorded_fps_min": round(min(rates), 2) if rates else 0, "recorded_fps_avg": round(sum(rates) / max(1, len(rates)), 2),
            "outputs_per_channel_fps": round(outputs, 2), "resyncs": int(resyncs), "write_failed": int(failed),
            "render_ms": round(render_ms, 2), "cpu_cores": round((c1 - c0) / elapsed, 2),
            "status_codec": status.get("codec"),
        }
        print(f"| {result['codec']} | {a.inputs} | {a.channels} | {a.speed}× {a.motion} | {result['recorded_fps_min']} / {result['recorded_fps_avg']} "
              f"| {result['outputs_per_channel_fps']} | {result['resyncs']} | {result['render_ms']} | {result['cpu_cores']} |")
        print(json.dumps(result), file=sys.stderr)
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()
        if "--keep" not in sys.argv:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
