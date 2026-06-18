#!/usr/bin/env bash
# run_device_bench.sh — run tests/device_bench.py on a connected badge and print
# the timing table. Times the picovector API directly (time.ticks_us); it does
# not use the firmware's "[pv] fps=" line.
#
# Usage:
#   tools/run_device_bench.sh [PORT_OR_SERIAL]
# With no argument it auto-selects the Tufty 2350 (USB 2e8a:1101).
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="$HERE/tests/device_bench.py"

MPR="$(command -v mpremote || true)"
[ -z "$MPR" ] && [ -x "$HOME/.virtualenvs/micropython/bin/mpremote" ] \
  && MPR="$HOME/.virtualenvs/micropython/bin/mpremote"
if [ -z "$MPR" ]; then echo "mpremote not found" >&2; exit 2; fi

if [ "$#" -ge 1 ]; then
  TARGET="$1"
else
  TARGET="$("$MPR" connect list 2>/dev/null | awk '/2e8a:1101/{print $1; exit}')"
  if [ -z "$TARGET" ]; then
    echo "No Tufty 2350 (2e8a:1101) found. Connected RP-series boards:" >&2
    "$MPR" connect list 2>/dev/null | grep -i "2e8a" >&2 || echo "  (none)" >&2
    echo "Pass a port/serial explicitly." >&2
    exit 2
  fi
fi

echo "picovector device benchmarks on: $TARGET"
OUT="$("$MPR" connect "$TARGET" run "$BENCH" 2>&1)"
# drop the firmware's interleaved render-debug lines; they are not our timings
printf '%s\n' "$OUT" | grep -v '^\[pv\] fps='
printf '%s\n' "$OUT" | grep -q '^DEVICE BENCH: done' || { echo "benchmark did not finish" >&2; exit 1; }
