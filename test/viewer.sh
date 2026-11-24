#!/usr/bin/env bash
# test/viewer.sh SQGUI: sqgui's read commands (phase 2, the viewer) on the
# test app, test/gen/ (names.json and layouts/), and on its wrong files,
# test/gen/bad/: what each says, as words and as JSON, and its exit status.
set -euo pipefail
sqgui=$(realpath "$1")
here=$(cd "$(dirname "$0")" && pwd)
app=$here/gen
bad=$here/gen/bad
ok() { printf '  ok   %s\n' "$1"; }
die() { printf '  FAIL %s\n' "$1" >&2; exit 1; }
s() { "$sqgui" --app "$app" "$@"; }

out=$(s list)
grep -q '^discard  *pane ' <<< "$out" && grep -q '^landing  *screen ' <<< "$out" && grep -q '^settings ' <<< "$out" ||
    die "list: $out"
s --json list | grep -q '"kind": "pane"' || die "list --json"
ok "list: every layout, its kind and nodes"

out=$(s show settings)
grep -q '^settings (screen)$' <<< "$out" || die "show: the head: $out"
grep -q 'rows\[1\]\[0\]  *slider  bind="settings.volume"' <<< "$out" || die "show: a node: $out"
grep -q '    rows\[3\]\[0\]\.rows\[0\]\[1\]  *button  text="Loud"  tap="set settings.volume 11.0"' <<< "$out" ||
    die "show: a nested table's node, deeper: $out"
grep -q 'rows\[4\]\[0\]\.node  *label' <<< "$out" && grep -q 'rows\[4\]\[1\]\.nodes\[1\]  *custom' <<< "$out" &&
    grep -q 'rows\[6\]\[0\]\.row  *label  bind="item.name"' <<< "$out" || die "show: scroll, stack and list: $out"
s show discard | grep -q '^discard (pane, bottom, modal)$' || die "show: a pane's head"
! s show nothing 2> /dev/null || die "show: an unknown layout"
s --json show landing | grep -q '"path": "rows\[2\]\[0\]"' || die "show --json"
ok "show: the outline, each node with its path; nested tables, scroll, stack, list"

[ "$(s get settings 'rows[1][0].bind')" = settings.volume ] || die "get: a string"
[ "$(s get settings 'rows[1][0].max')" = 11 ] || die "get: a number"
[ "$(s get landing kind)" = screen ] || die "get: a top-level key"
[ "$(s --json get settings 'rows[1][0].bind')" = '"settings.volume"' ] || die "get --json"
! s get settings 'rows[9][0]' 2> "$here/../build/err.txt" && grep -q 'no \[9\] there' "$here/../build/err.txt" ||
    die "get: past the end"
ok "get: a value by its path; past the end refused"

out=$(s check) || die "check on the good app exited non-zero"
grep -q '^no problems: 11 names, 3 layouts$' <<< "$out" || die "check: $out"
out=$("$sqgui" --app "$bad" check) && die "check on the wrong files exited 0"
# The build's words, without its "sqgen: " (sqgui-gen prints the same lines).
grep -q '^fine.json: rows\[0\]\[1\].bind: a.b_c carries no state to show$' <<< "$out" ||
    die "check: the build's problem: $out"
grep -q '^4 problems$' <<< "$out" || die "check: the count: $out"
out=$("$sqgui" --app "$bad" --json check) && die "check --json on the wrong files exited 0"
grep -q '"where": "broken.json:2:1"' <<< "$out" || die "check --json: $out"
ok "check: the build's problems, word for word; non-zero"

out=$(s map)
grep -q '^  push  *settings$' <<< "$out" && grep -q '^  replace  *workspace  (C++)$' <<< "$out" &&
    grep -q '^  open  *discard$' <<< "$out" && grep -q '^workspace (screen, C++)$' <<< "$out" || die "map: $out"
s --json map | grep -q '"verb": "push"' || die "map --json"
ok "map: push, replace, open, close; C++ screens marked"

out=$(s names)
grep -q '^settings.volume  *number state,set  *settings$' <<< "$out" ||
    die "names: $out"
grep -q '^player.pos .*(unused)$' <<< "$out" || die "names: an unused address: $out"
ok "names: every address, its type and verbs, and the layouts using it"

( cd "$here" && ! PATH=/nonexistent "$sqgui" list 2> "$here/../build/err.txt" ) &&
    grep -q 'square' "$here/../build/err.txt" || die "no --app and no square: said so"
ok "no --app and no square: says to use --app or install square"
echo "viewer test passed"
