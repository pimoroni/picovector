#!/usr/bin/env bash
# run_device_tests.sh — run tests/device_test.py on a connected badge and report.
#
# Streams the on-device picovector API tests over mpremote (the script never
# touches flash; `mpremote run` executes it from RAM). Exits non-zero if any
# test fails or the device can't be reached.
#
# Usage:
#   tools/run_device_tests.sh [PORT_OR_SERIAL]
# With no argument it auto-selects the Tufty 2350 (USB 2e8a:1101). Pass a port
# (e.g. /dev/cu.usbmodem11301) or a serial to target a specific board — handy
# when several RP-series boards are connected.
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
TEST="$HERE/tests/device_test.py"

MPR="$(command -v mpremote || true)"
[ -z "$MPR" ] && [ -x "$HOME/.virtualenvs/micropython/bin/mpremote" ] \
  && MPR="$HOME/.virtualenvs/micropython/bin/mpremote"
if [ -z "$MPR" ]; then echo "mpremote not found" >&2; exit 2; fi

# resolve the target device
if [ "$#" -ge 1 ]; then
  TARGET="$1"
else
  TARGET="$("$MPR" connect list 2>/dev/null | awk '/2e8a:1101/{print $1; exit}')"
  if [ -z "$TARGET" ]; then
    echo "No Tufty 2350 (2e8a:1101) found. Connected RP-series boards:" >&2
    "$MPR" connect list 2>/dev/null | grep -i "2e8a" >&2 || echo "  (none)" >&2
    echo "Pass a port/serial explicitly, e.g. tools/run_device_tests.sh /dev/cu.usbmodemXXXX" >&2
    exit 2
  fi
fi

echo "Running picovector device tests on: $TARGET"
echo "----------------------------------------------------------------"
OUT="$("$MPR" connect "$TARGET" run "$TEST" 2>&1)"
echo "$OUT"
echo "----------------------------------------------------------------"

# summary line: "DEVICE TESTS: <p> passed, <f> failed"
SUMMARY="$(printf '%s\n' "$OUT" | grep '^DEVICE TESTS:')"
if [ -z "$SUMMARY" ]; then
  echo "FAILED: tests did not run to completion (no summary line)." >&2
  exit 1
fi
FAILED="$(printf '%s' "$SUMMARY" | sed -E 's/.* ([0-9]+) failed.*/\1/')"
if [ "$FAILED" = "0" ]; then
  echo "OK — all device tests passed."
  exit 0
fi
echo "FAILED — $FAILED test(s) failed (see FAIL lines above)." >&2
exit 1
