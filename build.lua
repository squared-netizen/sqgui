-- build.lua: sqgui's build, for sqmake (0.10 or later).
--
--   sqmake                 build/sqgui-gen: the headless layout compiler, and
--                          build/sqgui: the editor (phase 2: the viewer)
--   sqmake test            build/test/gen_test: sqgui-gen on test/gen/, and
--                          a program compiled with what it wrote; and
--                          test/viewer.sh: sqgui's read commands on test/gen/
--
-- tools/gate.sh SQGEN SQGUI_GEN DIR... runs both on each app folder's
-- names.json and layouts/ and compares what they write, byte for byte
-- (sqgui's phase 1 gate).
--
-- squared: $SQUARED (a squared/ folder: the framework's own, as in squared's
-- repository at packages/package.squared-core/tree/squared), else a squared/
-- beside this file, else the SDK's installed one ($PREFIX/share/squared/
-- <version>/squared, the newest: pkg install squared).
--
-- The toolchain: the SDK's (squared's build/toolchain.lua) where its sysroot
-- is installed (sqterm: pkg install sysroot runtimes llvm) or $SQ_SYSROOT
-- names one (sqterm's package build, for Android), else sqmake's
-- own, $CXX with libc++ and `import std` (CI: CXX="clang++-23 -stdlib=libc++").
--
-- sqgui-gen is squared's gui and messaging modules for the layout reader and
-- checker, the null graphics backend, and no GLES: a build machine with no
-- display builds an app with it (the design: "two binaries from one source
-- tree"). The editor, sqgui, comes in phase 2.

local function version_less(x, y)
  local xs, ys = {}, {}
  for n in x:gmatch("%d+") do xs[#xs + 1] = tonumber(n) end
  for n in y:gmatch("%d+") do ys[#ys + 1] = tonumber(n) end
  for i = 1, math.max(#xs, #ys) do
    if (xs[i] or 0) ~= (ys[i] or 0) then return (xs[i] or 0) < (ys[i] or 0) end
  end
  return false
end
local function newest_installed()
  local base = (os.getenv("PREFIX") or "") .. "/share/squared"
  local newest
  for _, f in ipairs(glob(base .. "/*/squared/build/squared.lua")) do
    local v = f:sub(#base + 2):match("^([^/]+)/")
    if v and (not newest or version_less(newest, v)) then newest = v end
  end
  return newest and (base .. "/" .. newest .. "/squared") or nil
end

local sq = os.getenv("SQUARED")
if not sq or sq == "" then
  if #glob("squared/build/squared.lua") > 0 then
    sq = "squared"
  else
    sq = newest_installed() or error("build.lua: no squared: set $SQUARED, put a squared/ here, or pkg install squared", 0)
  end
end

local prefix = os.getenv("PREFIX")
local sysroot = os.getenv("SQ_SYSROOT")
if (sysroot and sysroot ~= "") or (prefix and #glob(prefix .. "/share/sysroot/usr/include/stdio.h") > 0) then
  toolchain.sdk(sq .. "/build/toolchain.lua")
end
squared_settings = {root = sq, platform = "host", graphics = "null"}
local squared = include(sq .. "/build/squared.lua")
local deps = {squared.gui, squared.messaging}

-- The reader both share (src/reading.cppm, module sqgui.reading): the
-- editor's problems are the build's, word for word.
local reading = library{name = "sqgui_reading", srcs = {"src/reading.cppm"}, deps = deps, flags = {"-O2"}}
local gen = exe{name = "sqgui-gen", srcs = {"gen/sqgui-gen.cpp"}, deps = {reading}, flags = {"-O2"}}
-- The editor: phase 2, the viewer's read commands (src/main.cpp).
local viewer = library{name = "sqgui_viewer", srcs = {"src/app.cppm", "src/face.cppm"},
                       deps = {reading, squared.tui}, flags = {"-O2"}}
local sqgui = exe{name = "sqgui", srcs = {"src/main.cpp"}, deps = {viewer}, flags = {"-O2"}}

-- The test: sqgui-gen on the fixture, then a program compiled with what it
-- wrote, which runs sqgui-gen again on the wrong files.
local out = toolchain.build .. "/test/gen"
local layouts_cpp = out .. "/layouts.cpp"
local fixture = "test/gen"
target{out = layouts_cpp, ins = {gen, fixture .. "/names.json", glob(fixture .. "/layouts/*.json")},
       desc = "sqgui-gen test/gen", cmds = {
  {gen, "--names", fixture .. "/names.json", "--layouts", fixture .. "/layouts", "--out", out},
  {"touch", layouts_cpp},
}}
local gen_test = exe{
  name = "test/gen_test",
  srcs = {"test/gen_test.cpp", {layouts_cpp, imports = {"sq.gui", "sq.messaging", "std"}}},
  deps = {reading},
  after = {layouts_cpp},
  flags = {"-I" .. out, '-DSQGEN_PATH="' .. gen .. '"', '-DSQGEN_FIXTURE="' .. fixture .. '"'},
  default = false,
}
-- The terminal face, driven through squared-tui's test bench (no terminal).
local face_test = exe{name = "test/face_test", srcs = {"test/face_test.cpp"}, deps = {viewer},
                      flags = {'-DFIXTURE="' .. fixture .. '"'}, default = false}
verb("test", {needs = {gen_test, sqgui, face_test, glob(fixture .. "/bad/**/*"), glob(fixture .. "/keyword/*")},
              call = function()
                local failed = exec{gen_test} ~= 0
                if exec{face_test} ~= 0 then failed = true end
                if exec{"bash", "test/viewer.sh", sqgui} ~= 0 then failed = true end
                return failed and 1 or 0
              end})

-- The reference for the gate: squared's own sqgen, built the same way,
-- while squared still carries it (squared/tools/sqgen; `sqmake
-- build/reference/sqgen`).
local reference = sq .. "/tools/sqgen/sqgen.cpp"
if #glob(reference) > 0 then
  exe{name = "reference/sqgen", srcs = {reference}, deps = deps, flags = {"-O2"}, default = false}
end

return gen
