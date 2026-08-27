#!/bin/bash
# Boots the already-built simulator headless, waits for the web server, runs
# every HTTP API suite in this directory, and shuts the simulator down again.
# Exits non-zero if the simulator fails to come up or any suite fails.
#
#   pio run -e display-sim && ./sim/tests/run.sh
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROGRAM="$REPO_ROOT/.pio/build/display-sim/program"
BASE_URL="${GM_SIM_URL:-http://localhost:8080}"

if [ ! -x "$PROGRAM" ]; then
  echo "run.sh: simulator binary not found — build it first: pio run -e display-sim" >&2
  exit 1
fi

# Fresh state so the suites are deterministic.
WORKDIR="$(mktemp -d)"
cd "$WORKDIR"

SDL_VIDEODRIVER=dummy "$PROGRAM" >sim.log 2>&1 &
SIM_PID=$!
trap 'kill "$SIM_PID" 2>/dev/null; wait "$SIM_PID" 2>/dev/null' EXIT

up=0
for _ in $(seq 1 120); do
  if curl -s -m 1 -o /dev/null "$BASE_URL/api/status"; then
    up=1
    break
  fi
  kill -0 "$SIM_PID" 2>/dev/null || break
  sleep 0.5
done
if [ "$up" != 1 ]; then
  echo "run.sh: simulator did not come up; last log lines:" >&2
  tail -20 sim.log >&2
  exit 1
fi

failures=0
for suite in "$REPO_ROOT"/sim/tests/test-*.sh; do
  echo "=== $(basename "$suite")"
  if ! bash "$suite"; then
    failures=$((failures + 1))
  fi
done

echo
if [ "$failures" -eq 0 ]; then
  echo "run.sh: all suites passed"
else
  echo "run.sh: $failures suite(s) failed" >&2
fi
[ "$failures" -eq 0 ]
