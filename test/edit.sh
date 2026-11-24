#!/usr/bin/env bash
# test/edit.sh SQGUI: sqgui's editing commands (phase 3) on a copy of the test
# app: set, unset and undo change only the bytes of the value they touch,
# refuse an edit the build would stop for, and undo back to the file's bytes.
set -euo pipefail
sqgui=$(realpath "$1")
here=$(cd "$(dirname "$0")" && pwd)
work=$here/../build/test/edit
rm -rf -- "$work"
mkdir -p "$work"
cp -r "$here/gen" "$work/app"
export XDG_STATE_HOME=$work/state
file=$work/app/layouts/settings.json
cp "$file" "$work/original.json"
ok() { printf '  ok   %s\n' "$1"; }
die() { printf '  FAIL %s\n' "$1" >&2; exit 1; }
s() { "$sqgui" --app "$work/app" "$@"; }
# The lines that differ from the original, as diff prints them.
changed() { diff "$work/original.json" "$file" | grep '^[<>]' || true; }

s set settings 'rows[1][0].max' 20 > /dev/null
[ "$(changed | wc -l)" = 2 ] || die "set: one line changed: $(changed)"
changed | grep -q '^> .*"min": 0, "max": 20, "step": 0.5' || die "set: the value in place: $(changed)"
[ "$(s get settings 'rows[1][0].max')" = 20 ] || die "set: get reads it back"
ok "set: replaces only the value's bytes"

s set settings 'rows[1][0].note' 'Volume, loud' > /dev/null
changed | grep -q '"fill_x": true, "note": "Volume, loud" }' || die "set: a new key: $(changed)"
[ "$(s get settings 'rows[1][0].note')" = 'Volume, loud' ] || die "set: text becomes a string"
s unset settings 'rows[1][0].note' > /dev/null
changed | grep -q '"fill_x": true }' || die "unset: the key and its comma: $(changed)"
ok "set adds a key after the last; unset removes it and its comma"

before=$(cat "$file")
! s set settings 'rows[1][0].bind' nope.thing 2> "$work/err.txt" || die "set: an unknown address saved"
grep -q 'nope.thing is not in names.json' "$work/err.txt" && grep -q 'not saved' "$work/err.txt" ||
    die "set: the build's words: $(cat "$work/err.txt")"
[ "$(cat "$file")" = "$before" ] || die "set: the refused edit left the file changed"
! s set settings 'rows[9][0].max' 1 2> /dev/null || die "set: past the end"
ok "set: refuses what the build would stop for; the file as it was"

s --force set settings 'rows[1][0].bind' nope.thing > /dev/null 2>&1 || die "set --force"
! s check > /dev/null || die "set --force: check finds the problem"
s undo settings > /dev/null
s undo settings > /dev/null
s undo settings > /dev/null
s undo settings > /dev/null
cmp -s "$work/original.json" "$file" || die "undo: the original's bytes: $(changed)"
! s undo settings 2> /dev/null || die "undo: nothing left"
ok "undo: each edit back, to the original's bytes"

s set settings 'rows[1][0].max' 7 > /dev/null
printf '\n' >> "$file"
! s undo settings 2> "$work/err.txt" && grep -q 'changed since' "$work/err.txt" ||
    die "undo: a file changed by hand"
ok "undo: refuses a file changed since sqgui's edit"

# 0.4: insert, remove, move: lists' elements, each list's own separator.
cp "$work/original.json" "$file"
s insert settings 'rows[0][1]' '{"type": "label", "text": "Hi"}' > /dev/null
changed | grep -q '"bind": "settings.sound" }, {"type": "label", "text": "Hi"} ],' ||
    die "insert: at a row's end, after a comma: $(changed)"
s insert settings 'rows[1]' '[ { "type": "label", "text": "Row" } ]' > /dev/null
grep -q '^    \[ { "type": "label", "text": "Row" } \],$' "$file" ||
    die "insert: a row, on a line of its own, indented as its neighbours: $(changed)"
s move settings 'rows[1]' 'rows[0]' > /dev/null
[ "$(s get settings 'rows[0][0].text')" = Row ] || die "move: a row to the top"
s remove settings 'rows[0]' > /dev/null
s remove settings 'rows[0][1]' > /dev/null
cmp -s "$work/original.json" "$file" || die "remove: back to the original's bytes: $(changed)"
ok "insert, move and remove: rows and widgets, each list's separator kept"

s move settings 'rows[3][0].rows[0][1]' 'rows[3][0].rows[0][0]' > /dev/null
[ "$(s get settings 'rows[3][0].rows[0][0].text')" = Loud ] &&
    [ "$(s get settings 'rows[3][0].rows[0][1].text')" = Mute ] || die "move: within a nested table's row"
grep -q '^        \[ { "type": "button", "text": "Loud", "tap": "set settings.volume 11.0" },$' "$file" ||
    die "move: the row's newline and indent kept: $(changed)"
! s move settings 'rows[3]' 'rows[3][0].rows[0]' 2> /dev/null || die "move: into itself"
! s insert settings 'rows[9]' '[]' 2> /dev/null || die "insert: past the end"
! s remove settings 'rows[1][0].max' 2> /dev/null || die "remove: a key (unset's)"
ok "move within a nested row; into itself, past the end and a key refused"
