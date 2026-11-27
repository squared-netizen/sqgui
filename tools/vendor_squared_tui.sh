#!/usr/bin/env bash
# tools/vendor_squared_tui.sh SQUARED: copy squared-tui (sq.tui, squared's
# packages/package.squared-tui) into third_party/squared-tui/, and record
# where from.
#
#   tools/vendor_squared_tui.sh ../squared
#
# sqgui's terminal face draws with sq.tui. It left squared-core for a
# package of its own (squared-core 1.3.0), which no installed squared
# carries, so sqgui carries the copy it is built and tested with; build.lua
# builds it on squared-core's sq.term and sq.keys. The copy is committed, so
# a checkout builds offline. Run this again when squared-tui changes, then
# build, test and commit third_party/squared-tui/ with its PINNED.
set -euo pipefail
SRC=${1:?"a squared checkout"}
TUI=$SRC/packages/package.squared-tui/tree/sq_pkg/squared_tui
[ -f "$TUI/module.cppm" ] || { echo "no packages/package.squared-tui under $SRC" >&2; exit 1; }
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
OUT=$ROOT/third_party/squared-tui
rm -rf "$OUT"
mkdir -p "$OUT/src"
cp "$TUI"/*.cppm "$OUT/"
cp "$TUI"/src/*.cpp "$OUT/src/"
cp "$SRC/LICENSE" "$OUT/LICENSE"
commit=$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)
{
  echo "# squared-tui (sq.tui), copied by tools/vendor_squared_tui.sh from squared's"
  echo "# packages/package.squared-tui. Do not edit: change it in squared, then copy it again."
  echo "from: squared $commit"
  echo "copied: $(date -u +%Y-%m-%d)"
  (cd "$OUT" && find . -name '*.cppm' -o -name '*.cpp' | LC_ALL=C sort | xargs sha256sum)
} > "$OUT/PINNED"
echo "squared-tui copied into third_party/squared-tui ($commit)"
