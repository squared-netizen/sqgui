# sqgui

The visual UI editor of the [squared SDK](https://squared-netizen.github.io/squared-netizen/):
it edits a squared app's screens and panes by reading and writing the files
the build already compiles, `sq_app/layouts/<name>.json` and
`sq_app/names.json`.

**Status: phase 1 built** (2026-11-24): `sqgui-gen`, the headless layout
compiler (sqgen, moved here from squared), byte for byte what sqgen writes.
Phase 2, the viewer, has begun: `sqgui`'s read commands, which write
nothing:

```sh
sqgui list                    # every layout: its kind, its nodes, whether it checks
sqgui show settings           # its nodes as an outline, each with its path
sqgui get settings 'rows[1][0].bind'
sqgui check                   # every problem the build would stop for (exit 1)
sqgui map                     # which layout pushes, replaces, opens or closes which
sqgui names                   # names.json's addresses, and the layouts using each
```

Each answers as words or, with `--json`, as JSON. sqgui finds the files by
asking square (`square --json info`: the workspace's app and layouts), or
`--app DIR` says. sqgui-gen and sqgui read through one module
(`src/reading.cppm`), so `sqgui check` says what the build would, word for
word. The terminal face and then the window come next.

```sh
sqmake           # build/sqgui-gen (squared: $SQUARED, ./squared, or the SDK's)
sqmake test      # sqgui-gen on test/gen/, and a program built with what it wrote
sqgui-gen --names sq_app/names.json --layouts sq_app/layouts --out build/gen
```

`tools/gate.sh SQGEN SQGUI_GEN DIR...` compares the two on app folders, byte
for byte (phase 1's gate; CI runs it on squared-examples).

## What it will be

- **The preview is the app.** Layouts are drawn with squared's own
  `instantiate` and the app's own skin, so the editor shows what the phone
  shows.
- **The checks are the build's.** Every problem the build would stop for
  shows as you edit, from the same `layout_file` code.
- **One editor, three faces.** A window (phone or Linux, GLES), a terminal
  (character cells, in sqterm or over ssh), and the command line
  (`sqgui set`, `sqgui add`, `sqgui rename-name`, `sqgui show --json`).
  Every face edits through the same text commands, so anything done by
  hand can be scripted, replayed or undone.
- **Files stay yours.** A file opened and saved unchanged is byte for byte
  the same; an edit rewrites only what it touched.
- **sqgen lives here.** The build step that compiles layouts becomes
  `sqgui-gen`, headless, with no gui or GLES linked in.

sqgui asks square where the app, its layouts and its skin are, and shows
what each dex shim does through `square explain`.

## Licence

MIT: see [LICENSE](LICENSE). Security issues: see [SECURITY.md](SECURITY.md).
