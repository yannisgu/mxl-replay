#!/usr/bin/env bash
# Routes restored from REPLAY_STATE_DIR/routes.json after a restart: each restored receiver shows the
# route in IS-05 /active and in its IS-04 subscription (active, sender_id), as a live activation would.
# Before the fix only IS-05 was restored, and the registry showed every restored receiver as unconnected.
# An unrouted receiver keeps active false and no sender in both.
set -euo pipefail
BIN=${1:-./build/mxl-replay}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CURL=${CURL:-curl}
DIR=$(mktemp -d)
PORT=${REPLAY_TEST_PORT:-18175}
NMOS_PORT_VALUE=${REPLAY_TEST_NMOS_PORT:-18177}
REG_PORT=${REPLAY_TEST_REG_PORT:-18178}
QUERY_PORT=${REPLAY_TEST_QUERY_PORT:-18179}
PID=""
REGPID=""
cleanup() {
  if [[ -n "${PID}" ]] && kill -0 "$PID" 2>/dev/null; then
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" || true
  fi
  if [[ -n "${REGPID}" ]] && kill -0 "$REGPID" 2>/dev/null; then
    kill "$REGPID" 2>/dev/null || true
    wait "$REGPID" || true
  fi
  rm -rf "$DIR"
}
trap cleanup EXIT

python3 "$ROOT/tests/integration/registry_stub.py" "$REG_PORT" "$QUERY_PORT" "$DIR/registry.log" >"$DIR/registry.out" 2>&1 &
REGPID=$!
for _ in $(seq 1 50); do
  if "$CURL" -sf "http://127.0.0.1:${REG_PORT}/" >/dev/null; then
    break
  fi
  sleep 0.05
done
"$CURL" -sf "http://127.0.0.1:${REG_PORT}/" >/dev/null

HOST_ADDRESS=$(ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -n 1 || true)
if [[ -z "$HOST_ADDRESS" ]]; then
  HOST_ADDRESS=10.255.0.10
fi

# Camera 1 video and audio routed, camera 2 not.
DOMAIN=55555555-5555-5555-5555-555555555555
VSENDER=66666666-6666-6666-6666-666666666661
ASENDER=66666666-6666-6666-6666-666666666662
mkdir -p "$DIR/state"
cat >"$DIR/state/routes.json" <<EOF
[{"camera":1,"phase":1,"video":true,"active":true,"domain_id":"$DOMAIN","flow_id":"77777777-7777-7777-7777-777777777771","sender_id":"$VSENDER","state":"waiting"},
 {"camera":1,"phase":1,"video":false,"active":true,"domain_id":"$DOMAIN","flow_id":"77777777-7777-7777-7777-777777777772","sender_id":"$ASENDER","state":"waiting"}]
EOF

REPLAY_FORMAT=64x32p50 \
REPLAY_INPUTS=2 \
REPLAY_CHANNELS=1 \
REPLAY_BUFFER_HOURS=0.01 \
REPLAY_STORAGE_DIR="$DIR/media" \
REPLAY_STATE_DIR="$DIR/state" \
REPLAY_STORAGE_MIN_MBPS=0 \
REPLAY_SYNTHETIC=true \
WEB_PORT="$PORT" \
NMOS_ENABLE=true \
NMOS_DNS_SD=false \
NMOS_PORT="$NMOS_PORT_VALUE" \
NMOS_REGISTRY_ADDRESS=127.0.0.1 \
NMOS_REGISTRY_PORT="$REG_PORT" \
NMOS_QUERY_ADDRESS=127.0.0.1 \
NMOS_QUERY_PORT="$QUERY_PORT" \
NMOS_HOST_ADDRESS="$HOST_ADDRESS" \
NMOS_SEED=ci-replay-routes \
NMOS_LABEL="CI Replay" \
MXL_DOMAIN_SCAN_PATH="$DIR/mxl" \
MXL_OUTPUT_DOMAIN_DIR="$DIR/mxl/replay-ci" \
MXL_OUTPUT_DOMAIN_ID=44444444-4444-4444-4444-444444444446 \
MXL_CLEANUP_ON_EXIT=true \
SHUTDOWN_TIMEOUT_S=10 \
HOST_ID=ci-replay \
TSL_ENABLE=false \
"$BIN" >"$DIR/log.txt" 2>&1 &
PID=$!

ready=0
for _ in $(seq 1 200); do
  if "$CURL" -sf "http://127.0.0.1:${PORT}/readyz" | grep -q ready; then
    ready=1
    break
  fi
  if ! kill -0 "$PID" 2>/dev/null; then
    echo "replay exited before ready"
    cat "$DIR/log.txt"
    exit 1
  fi
  sleep 0.1
done
if [[ "$ready" != 1 ]]; then
  echo "readyz did not succeed"
  cat "$DIR/log.txt"
  exit 1
fi

python3 - "http://127.0.0.1:${NMOS_PORT_VALUE}" "$VSENDER" "$ASENDER" <<'EOF'
import json, sys, urllib.request
base, vsender, asender = sys.argv[1:]
get = lambda path: json.load(urllib.request.urlopen(base + path, timeout=5))
expected = {"Cam 1 Video": vsender, "Cam 1 Audio": asender, "Cam 2 Video": None, "Cam 2 Audio": None}
# The newest IS-05 version the node offers: nmos-cpp refuses an older one for a newer resource (409).
conn_version = max((v.strip("/") for v in get("/x-nmos/connection/")), key=lambda v: tuple(int(x) for x in v[1:].split(".")))
receivers = {r["label"]: r for r in get("/x-nmos/node/v1.3/receivers")}
failed = False
for label, sender in expected.items():
    r = receivers[label]
    active = get(f"/x-nmos/connection/{conn_version}/single/receivers/{r['id']}/active")
    is04 = (r["subscription"]["active"], r["subscription"]["sender_id"])
    is05 = (active["master_enable"], active["sender_id"])
    want = (sender is not None, sender)
    ok = is04 == want and is05 == want
    failed |= not ok
    print(f"{label}: IS-04 {is04} IS-05 {is05} expected {want} {'ok' if ok else 'FAILED'}")
sys.exit(1 if failed else 0)
EOF
echo "route restore ok"
