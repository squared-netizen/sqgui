#!/usr/bin/env bash
# tools/gate.sh SQGEN SQGUI_GEN DIR...: sqgui's phase 1 gate. For each app
# folder (one holding names.json and/or layouts/, as an app's sq_app/ does),
# run sqgen and sqgui-gen into folders of their own and compare what each
# wrote, file by file, byte for byte; and what each printed. Exit 1 on the
# first difference.
set -euo pipefail
[ $# -ge 3 ] || { echo "usage: tools/gate.sh SQGEN SQGUI_GEN DIR..." >&2; exit 2; }
old=$(realpath "$1") new=$(realpath "$2")
shift 2
work=$(mktemp -d "${TMPDIR:-/tmp}/sqgui-gate.XXXXXX")
trap 'rm -rf "$work"' EXIT
n=0
for dir in "$@"; do
  args=()
  [ -f "$dir/names.json" ] && args+=(--names "$dir/names.json")
  [ -d "$dir/layouts" ] && args+=(--layouts "$dir/layouts")
  [ ${#args[@]} -gt 0 ] || { echo "gate: $dir has no names.json or layouts/" >&2; exit 2; }
  n=$((n + 1))
  a=$work/$n/sqgen b=$work/$n/sqgui-gen
  set +e
  "$old" "${args[@]}" --out "$a" > "$work/$n.old.txt" 2>&1; sa=$?
  "$new" "${args[@]}" --out "$b" > "$work/$n.new.txt" 2>&1; sb=$?
  set -e
  [ "$sa" = "$sb" ] || { echo "gate: $dir: sqgen exits $sa, sqgui-gen $sb" >&2; exit 1; }
  cmp -s "$work/$n.old.txt" "$work/$n.new.txt" || { echo "gate: $dir: they print differently" >&2;
    diff "$work/$n.old.txt" "$work/$n.new.txt" >&2 || true; exit 1; }
  if [ -d "$a" ] || [ -d "$b" ]; then
    diff -r "$a" "$b" > /dev/null || { echo "gate: $dir: they write differently" >&2; diff -r "$a" "$b" >&2 || true; exit 1; }
  fi
  echo "  same  $dir ($(ls "$a" 2>/dev/null | wc -l) files)"
done
echo "gate passed: $n app folder(s), byte for byte"
