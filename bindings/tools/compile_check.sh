#!/usr/bin/env bash
# compile_check.sh — verify the generated + native + companion binding sources
# compile with the REAL Tufty firmware toolchain (arm-none-eabi-g++, Cortex-M33)
# and flags. The bindings/ tree is self-contained, so this compiles the files in
# place (no staging) using the exact CXX flags/defines/includes from a configured
# build tree, plus the bindings' own include dirs.
#
# Usage:  tools/compile_check.sh [BUILD_DIR]
#   BUILD_DIR defaults to ../../build-tufty (a configured rp2 cmake tree).
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"           # .../picovector/bindings
PICOVECTOR="$(cd "$HERE/.." && pwd)"                # .../picovector
BUILD="${1:-$PICOVECTOR/../build-tufty}"
FLAGS="$BUILD/CMakeFiles/firmware.dir/flags.make"

if [ ! -f "$FLAGS" ]; then
  echo "no flags.make at $FLAGS — point me at a configured build tree" >&2
  exit 2
fi

python3 "$HERE/generate.py" >/dev/null

# bindings' own include dirs (the toolchain flags already carry the picovector
# root + pngdec/jpegdec include paths, since the firmware compiles those)
INC="-I$HERE/generated -I$HERE/runtime"

MK="$(mktemp)"
cat > "$MK" <<MKEOF
include $FLAGS
INC=$INC
W=-Wno-unused-variable
cxx:
	@for f in $HERE/generated/*.cpp $HERE/runtime/pv_support.cpp $HERE/native/*.cpp; do \\
	  if arm-none-eabi-g++ \$(CXX_DEFINES) \$(CXX_INCLUDES) \$(CXX_FLAGS) \$(W) \$(INC) -fsyntax-only \$\$f 2>/tmp/pverr; then \\
	    echo "OK  \$\$(basename \$\$f)"; \\
	  else echo "FAIL \$\$(basename \$\$f)"; head -25 /tmp/pverr; exit 1; fi; \\
	done
c:
	@arm-none-eabi-gcc \$(C_DEFINES) \$(C_INCLUDES) \$(C_FLAGS) \$(INC) -fsyntax-only $HERE/generated/picovector_bindings.c && echo "OK  picovector_bindings.c"
MKEOF

echo "== syntax-checking bindings against $BUILD critical flags =="
make -f "$MK" cxx
make -f "$MK" c
rm -f "$MK"
echo "== all sources compile with the real toolchain =="
